/*
 * Copyright (C) 2009 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * This program constructs binary patches for images -- such as boot.img
 * and recovery.img -- that consist primarily of large chunks of gzipped
 * data interspersed with uncompressed data.  Doing a naive bsdiff of
 * these files is not useful because small changes in the data lead to
 * large changes in the compressed bitstream; bsdiff patches of gzipped
 * data are typically as large as the data itself.
 *
 * To patch these usefully, we break the source and target images up into
 * chunks of two types: "normal" and "gzip".  Normal chunks are simply
 * patched using a plain bsdiff.  Gzip chunks are first expanded, then a
 * bsdiff is applied to the uncompressed data, then the patched data is
 * gzipped using the same encoder parameters.  Patched chunks are
 * concatenated together to create the output file; the output image
 * should be *exactly* the same series of bytes as the target image used
 * originally to generate the patch.
 *
 * To work well with this tool, the gzipped sections of the target
 * image must have been generated using the same deflate encoder that
 * is available in applypatch, namely, the one in the zlib library.
 * In practice this means that images should be compressed using the
 * "minigzip" tool included in the zlib distribution, not the GNU gzip
 * program.
 *
 * An "imgdiff" patch consists of a header describing the chunk structure
 * of the file and any encoding parameters needed for the gzipped
 * chunks, followed by N bsdiff patches, one per chunk.
 *
 * For a diff to be generated, the source and target images must have the
 * same "chunk" structure: that is, the same number of gzipped and normal
 * chunks in the same order.  Android boot and recovery images currently
 * consist of five chunks:  a small normal header, a gzipped kernel, a
 * small normal section, a gzipped ramdisk, and finally a small normal
 * footer.
 *
 * Caveats:  we locate gzipped sections within the source and target
 * images by searching for the byte sequence 1f8b0800:  1f8b is the gzip
 * magic number; 08 specifies the "deflate" encoding [the only encoding
 * supported by the gzip standard]; and 00 is the flags byte.  We do not
 * currently support any extra header fields (which would be indicated by
 * a nonzero flags byte).  We also don't handle the case when that byte
 * sequence appears spuriously in the file.  (Note that it would have to
 * occur spuriously within a normal chunk to be a problem.)
 *
 *
 * The imgdiff patch header looks like this:
 *
 *    "IMGDIFF1"                  (8)   [magic number and version]
 *    chunk count                 (4)
 *    for each chunk:
 *        chunk type              (4)   [CHUNK_{NORMAL, GZIP, DEFLATE, RAW}]
 *        if chunk type == CHUNK_NORMAL:
 *           source start         (8)
 *           source len           (8)
 *           bsdiff patch offset  (8)   [from start of patch file]
 *        if chunk type == CHUNK_GZIP:      (version 1 only)
 *           source start         (8)
 *           source len           (8)
 *           bsdiff patch offset  (8)   [from start of patch file]
 *           source expanded len  (8)   [size of uncompressed source]
 *           target expected len  (8)   [size of uncompressed target]
 *           gzip level           (4)
 *                method          (4)
 *                windowBits      (4)
 *                memLevel        (4)
 *                strategy        (4)
 *           gzip header len      (4)
 *           gzip header          (gzip header len)
 *           gzip footer          (8)
 *        if chunk type == CHUNK_DEFLATE:   (version 2 only)
 *           source start         (8)
 *           source len           (8)
 *           bsdiff patch offset  (8)   [from start of patch file]
 *           source expanded len  (8)   [size of uncompressed source]
 *           target expected len  (8)   [size of uncompressed target]
 *           gzip level           (4)
 *                method          (4)
 *                windowBits      (4)
 *                memLevel        (4)
 *                strategy        (4)
 *        if chunk type == RAW:             (version 2 only)
 *           target len           (4)
 *           data                 (target len)
 *
 * All integers are little-endian.  "source start" and "source len"
 * specify the section of the input image that comprises this chunk,
 * including the gzip header and footer for gzip chunks.  "source
 * expanded len" is the size of the uncompressed source data.  "target
 * expected len" is the size of the uncompressed data after applying
 * the bsdiff patch.  The next five parameters specify the zlib
 * parameters to be used when compressing the patched data, and the
 * next three specify the header and footer to be wrapped around the
 * compressed data to create the output chunk (so that header contents
 * like the timestamp are recreated exactly).
 *
 * After the header there are 'chunk count' bsdiff patches; the offset
 * of each from the beginning of the file is specified in the header.
 *
 * This tool can take an optional file of "bonus data".  This is an
 * extra file of data that is appended to chunk #1 after it is
 * compressed (it must be a CHUNK_DEFLATE chunk).  The same file must
 * be available (and passed to applypatch with -b) when applying the
 * patch.  This is used to reduce the size of recovery-from-boot
 * patches by combining the boot image with recovery ramdisk
 * information that is stored on the system partition.
 */

#include "applypatch/imgdiff.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/memory.h>
#include <android-base/unique_fd.h>
#include <ziparchive/zip_archive.h>

#include <bsdiff.h>
#include <zlib.h>

using android::base::get_unaligned;

static constexpr auto BUFFER_SIZE = 0x8000;

// If we use this function to write the offset and length (type size_t), their values should not
// exceed 2^63; because the signed bit will be casted away.
static inline bool Write8(int fd, int64_t value) {
  return android::base::WriteFully(fd, &value, sizeof(int64_t));
}

// Similarly, the value should not exceed 2^31 if we are casting from size_t (e.g. target chunk
// size).
static inline bool Write4(int fd, int32_t value) {
  return android::base::WriteFully(fd, &value, sizeof(int32_t));
}

class ImageChunk {
 public:
  static constexpr auto WINDOWBITS = -15;  // 32kb window; negative to indicate a raw stream.
  static constexpr auto MEMLEVEL = 8;      // the default value.
  static constexpr auto METHOD = Z_DEFLATED;
  static constexpr auto STRATEGY = Z_DEFAULT_STRATEGY;

  ImageChunk(int type, size_t start, const std::vector<uint8_t>* file_content, size_t raw_data_len)
      : type_(type),
        start_(start),
        input_file_ptr_(file_content),
        raw_data_len_(raw_data_len),
        compress_level_(6),
        source_start_(0),
        source_len_(0),
        source_uncompressed_len_(0) {
    CHECK(file_content != nullptr) << "input file container can't be nullptr";
  }

  int GetType() const {
    return type_;
  }
  size_t GetRawDataLength() const {
    return raw_data_len_;
  }
  const std::string& GetEntryName() const {
    return entry_name_;
  }

  // CHUNK_DEFLATE will return the uncompressed data for diff, while other types will simply return
  // the raw data.
  const uint8_t * DataForPatch() const;
  size_t DataLengthForPatch() const;

  void Dump() const {
    printf("type: %d, start: %zu, len: %zu, name: %s\n", type_, start_, DataLengthForPatch(),
           entry_name_.c_str());
  }

  void SetSourceInfo(const ImageChunk& other);
  void SetEntryName(std::string entryname);
  void SetUncompressedData(std::vector<uint8_t> data);
  bool SetBonusData(const std::vector<uint8_t>& bonus_data);

  bool operator==(const ImageChunk& other) const;
  bool operator!=(const ImageChunk& other) const {
    return !(*this == other);
  }

  size_t GetHeaderSize(size_t patch_size) const;
  // Return the offset of the next patch into the patch data.
  size_t WriteHeaderToFd(int fd, const std::vector<uint8_t>& patch, size_t offset) const;

  /*
   * Cause a gzip chunk to be treated as a normal chunk (ie, as a blob
   * of uninterpreted data).  The resulting patch will likely be about
   * as big as the target file, but it lets us handle the case of images
   * where some gzip chunks are reconstructible but others aren't (by
   * treating the ones that aren't as normal chunks).
   */
  void ChangeDeflateChunkToNormal();
  bool ChangeChunkToRaw(size_t patch_size);

  /*
   * Verify that we can reproduce exactly the same compressed data that
   * we started with.  Sets the level, method, windowBits, memLevel, and
   * strategy fields in the chunk to the encoding parameters needed to
   * produce the right output.
   */
  bool ReconstructDeflateChunk();
  bool IsAdjacentNormal(const ImageChunk& other) const;
  void MergeAdjacentNormal(const ImageChunk& other);

  /*
   * Compute a bsdiff patch between |this| and the input source chunks.
   * Store the result in the patch_data.
   * |bsdiff_cache| can be used to cache the suffix array if the same |src| chunk is used
   * repeatedly, pass nullptr if not needed.
   */
  bool MakePatch(const ImageChunk& src, std::vector<uint8_t>* patch_data, saidx_t** bsdiff_cache);

 private:
  int type_;                                    // CHUNK_NORMAL, CHUNK_DEFLATE, CHUNK_RAW
  size_t start_;                                // offset of chunk in the original input file
  const std::vector<uint8_t>* input_file_ptr_;  // ptr to the full content of original input file
  size_t raw_data_len_;

  // --- for CHUNK_DEFLATE chunks only: ---
  std::vector<uint8_t> uncompressed_data_;
  std::string entry_name_;  // used for zip entries

  // deflate encoder parameters
  int compress_level_;

  size_t source_start_;
  size_t source_len_;
  size_t source_uncompressed_len_;

  const uint8_t* GetRawData() const;
  bool TryReconstruction(int level);
};

const uint8_t* ImageChunk::GetRawData() const {
  CHECK_LE(start_ + raw_data_len_, input_file_ptr_->size());
  return input_file_ptr_->data() + start_;
}

const uint8_t * ImageChunk::DataForPatch() const {
  if (type_ == CHUNK_DEFLATE) {
    return uncompressed_data_.data();
  }
  return GetRawData();
}

size_t ImageChunk::DataLengthForPatch() const {
  if (type_ == CHUNK_DEFLATE) {
    return uncompressed_data_.size();
  }
  return raw_data_len_;
}

bool ImageChunk::operator==(const ImageChunk& other) const {
  if (type_ != other.type_) {
    return false;
  }
  return (raw_data_len_ == other.raw_data_len_ &&
          memcmp(GetRawData(), other.GetRawData(), raw_data_len_) == 0);
}

void ImageChunk::SetSourceInfo(const ImageChunk& src) {
  source_start_ = src.start_;
  if (type_ == CHUNK_NORMAL) {
    source_len_ = src.raw_data_len_;
  } else if (type_ == CHUNK_DEFLATE) {
    source_len_ = src.raw_data_len_;
    source_uncompressed_len_ = src.uncompressed_data_.size();
  }
}

void ImageChunk::SetEntryName(std::string entryname) {
  entry_name_ = std::move(entryname);
}

void ImageChunk::SetUncompressedData(std::vector<uint8_t> data) {
  uncompressed_data_ = std::move(data);
}

bool ImageChunk::SetBonusData(const std::vector<uint8_t>& bonus_data) {
  if (type_ != CHUNK_DEFLATE) {
    return false;
  }
  uncompressed_data_.insert(uncompressed_data_.end(), bonus_data.begin(), bonus_data.end());
  return true;
}

// Convert CHUNK_NORMAL & CHUNK_DEFLATE to CHUNK_RAW if the target size is
// smaller. Also take the header size into account during size comparison.
bool ImageChunk::ChangeChunkToRaw(size_t patch_size) {
  if (type_ == CHUNK_RAW) {
    return true;
  } else if (type_ == CHUNK_NORMAL && (raw_data_len_ <= 160 || raw_data_len_ < patch_size)) {
    type_ = CHUNK_RAW;
    return true;
  }
  return false;
}

void ImageChunk::ChangeDeflateChunkToNormal() {
  if (type_ != CHUNK_DEFLATE) return;
  type_ = CHUNK_NORMAL;
  // No need to clear the entry name.
  uncompressed_data_.clear();
}

// Header size:
// header_type    4 bytes
// CHUNK_NORMAL   8*3 = 24 bytes
// CHUNK_DEFLATE  8*5 + 4*5 = 60 bytes
// CHUNK_RAW      4 bytes + patch_size
size_t ImageChunk::GetHeaderSize(size_t patch_size) const {
  switch (type_) {
    case CHUNK_NORMAL:
      return 4 + 8 * 3;
    case CHUNK_DEFLATE:
      return 4 + 8 * 5 + 4 * 5;
    case CHUNK_RAW:
      return 4 + 4 + patch_size;
    default:
      CHECK(false) << "unexpected chunk type: " << type_;  // Should not reach here.
      return 0;
  }
}

size_t ImageChunk::WriteHeaderToFd(int fd, const std::vector<uint8_t>& patch, size_t offset) const {
  Write4(fd, type_);
  switch (type_) {
    case CHUNK_NORMAL:
      printf("normal   (%10zu, %10zu)  %10zu\n", start_, raw_data_len_, patch.size());
      Write8(fd, static_cast<int64_t>(source_start_));
      Write8(fd, static_cast<int64_t>(source_len_));
      Write8(fd, static_cast<int64_t>(offset));
      return offset + patch.size();
    case CHUNK_DEFLATE:
      printf("deflate  (%10zu, %10zu)  %10zu  %s\n", start_, raw_data_len_, patch.size(),
             entry_name_.c_str());
      Write8(fd, static_cast<int64_t>(source_start_));
      Write8(fd, static_cast<int64_t>(source_len_));
      Write8(fd, static_cast<int64_t>(offset));
      Write8(fd, static_cast<int64_t>(source_uncompressed_len_));
      Write8(fd, static_cast<int64_t>(uncompressed_data_.size()));
      Write4(fd, compress_level_);
      Write4(fd, METHOD);
      Write4(fd, WINDOWBITS);
      Write4(fd, MEMLEVEL);
      Write4(fd, STRATEGY);
      return offset + patch.size();
    case CHUNK_RAW:
      printf("raw      (%10zu, %10zu)\n", start_, raw_data_len_);
      Write4(fd, static_cast<int32_t>(patch.size()));
      if (!android::base::WriteFully(fd, patch.data(), patch.size())) {
        CHECK(false) << "failed to write " << patch.size() <<" bytes patch";
      }
      return offset;
    default:
      CHECK(false) << "unexpected chunk type: " << type_;
      return offset;
  }
}

bool ImageChunk::IsAdjacentNormal(const ImageChunk& other) const {
  if (type_ != CHUNK_NORMAL || other.type_ != CHUNK_NORMAL) {
    return false;
  }
  return (other.start_ == start_ + raw_data_len_);
}

void ImageChunk::MergeAdjacentNormal(const ImageChunk& other) {
  CHECK(IsAdjacentNormal(other));
  raw_data_len_ = raw_data_len_ + other.raw_data_len_;
}

bool ImageChunk::MakePatch(const ImageChunk& src, std::vector<uint8_t>* patch_data,
                           saidx_t** bsdiff_cache) {
  if (ChangeChunkToRaw(0)) {
    size_t patch_size = DataLengthForPatch();
    patch_data->resize(patch_size);
    std::copy(DataForPatch(), DataForPatch() + patch_size, patch_data->begin());
    return true;
  }

#if defined(__ANDROID__)
  char ptemp[] = "/data/local/tmp/imgdiff-patch-XXXXXX";
#else
  char ptemp[] = "/tmp/imgdiff-patch-XXXXXX";
#endif

  int fd = mkstemp(ptemp);
  if (fd == -1) {
    printf("MakePatch failed to create a temporary file: %s\n", strerror(errno));
    return false;
  }
  close(fd);

  int r = bsdiff::bsdiff(src.DataForPatch(), src.DataLengthForPatch(), DataForPatch(),
                         DataLengthForPatch(), ptemp, bsdiff_cache);
  if (r != 0) {
    printf("bsdiff() failed: %d\n", r);
    return false;
  }

  android::base::unique_fd patch_fd(open(ptemp, O_RDONLY));
  if (patch_fd == -1) {
    printf("failed to open %s: %s\n", ptemp, strerror(errno));
    return false;
  }
  struct stat st;
  if (fstat(patch_fd, &st) != 0) {
    printf("failed to stat patch file %s: %s\n", ptemp, strerror(errno));
    return false;
  }

  size_t sz = static_cast<size_t>(st.st_size);
  // Change the chunk type to raw if the patch takes less space that way.
  if (ChangeChunkToRaw(sz)) {
    unlink(ptemp);
    size_t patch_size = DataLengthForPatch();
    patch_data->resize(patch_size);
    std::copy(DataForPatch(), DataForPatch() + patch_size, patch_data->begin());
    return true;
  }
  patch_data->resize(sz);
  if (!android::base::ReadFully(patch_fd, patch_data->data(), sz)) {
    printf("failed to read \"%s\" %s\n", ptemp, strerror(errno));
    unlink(ptemp);
    return false;
  }

  unlink(ptemp);
  SetSourceInfo(src);

  return true;
}

bool ImageChunk::ReconstructDeflateChunk() {
  if (type_ != CHUNK_DEFLATE) {
    printf("attempt to reconstruct non-deflate chunk\n");
    return false;
  }

  // We only check two combinations of encoder parameters:  level 6
  // (the default) and level 9 (the maximum).
  for (int level = 6; level <= 9; level += 3) {
    if (TryReconstruction(level)) {
      compress_level_ = level;
      return true;
    }
  }

  return false;
}

/*
 * Takes the uncompressed data stored in the chunk, compresses it
 * using the zlib parameters stored in the chunk, and checks that it
 * matches exactly the compressed data we started with (also stored in
 * the chunk).
 */
bool ImageChunk::TryReconstruction(int level) {
  z_stream strm;
  strm.zalloc = Z_NULL;
  strm.zfree = Z_NULL;
  strm.opaque = Z_NULL;
  strm.avail_in = uncompressed_data_.size();
  strm.next_in = uncompressed_data_.data();
  int ret = deflateInit2(&strm, level, METHOD, WINDOWBITS, MEMLEVEL, STRATEGY);
  if (ret < 0) {
    printf("failed to initialize deflate: %d\n", ret);
    return false;
  }

  std::vector<uint8_t> buffer(BUFFER_SIZE);
  size_t offset = 0;
  do {
    strm.avail_out = buffer.size();
    strm.next_out = buffer.data();
    ret = deflate(&strm, Z_FINISH);
    if (ret < 0) {
      printf("failed to deflate: %d\n", ret);
      return false;
    }

    size_t compressed_size = buffer.size() - strm.avail_out;
    if (memcmp(buffer.data(), input_file_ptr_->data() + start_ + offset, compressed_size) != 0) {
      // mismatch; data isn't the same.
      deflateEnd(&strm);
      return false;
    }
    offset += compressed_size;
  } while (ret != Z_STREAM_END);
  deflateEnd(&strm);

  if (offset != raw_data_len_) {
    // mismatch; ran out of data before we should have.
    return false;
  }
  return true;
}

// Interface for zip_mode and image_mode images. We initialize the image from an input file and
// split the file content into a list of image chunks.
class Image {
 public:
  explicit Image(bool is_source) : is_source_(is_source) {}

  virtual ~Image() {}

  // Create a list of image chunks from input file.
  virtual bool Initialize(const std::string& filename) = 0;

  // Look for runs of adjacent normal chunks and compress them down into a single chunk.  (Such
  // runs can be produced when deflate chunks are changed to normal chunks.)
  void MergeAdjacentNormalChunks();

  // In zip mode, find the matching deflate source chunk by entry name. Search for normal chunks
  // also if |find_normal| is true.
  ImageChunk* FindChunkByName(const std::string& name, bool find_normal = false);

  // Write the contents of |patch_data| to |patch_fd|.
  bool WritePatchDataToFd(const std::vector<std::vector<uint8_t>>& patch_data, int patch_fd) const;

  void DumpChunks() const;

  // Non const iterators to access the stored ImageChunks.
  std::vector<ImageChunk>::iterator begin() {
    return chunks_.begin();
  }

  std::vector<ImageChunk>::iterator end() {
    return chunks_.end();
  }
  // Return a pointer to the ith ImageChunk.
  ImageChunk* Get(size_t i) {
    CHECK_LT(i, chunks_.size());
    return &chunks_[i];
  }

  size_t NumOfChunks() const {
    return chunks_.size();
  }

 protected:
  bool ReadFile(const std::string& filename, std::vector<uint8_t>* file_content);

  bool is_source_;                     // True if it's for source chunks.
  std::vector<ImageChunk> chunks_;     // Internal storage of ImageChunk.
  std::vector<uint8_t> file_content_;  // Store the whole input file in memory.
};

void Image::MergeAdjacentNormalChunks() {
  size_t merged_last = 0, cur = 0;
  while (cur < chunks_.size()) {
    // Look for normal chunks adjacent to the current one. If such chunk exists, extend the
    // length of the current normal chunk.
    size_t to_check = cur + 1;
    while (to_check < chunks_.size() && chunks_[cur].IsAdjacentNormal(chunks_[to_check])) {
      chunks_[cur].MergeAdjacentNormal(chunks_[to_check]);
      to_check++;
    }

    if (merged_last != cur) {
      chunks_[merged_last] = std::move(chunks_[cur]);
    }
    merged_last++;
    cur = to_check;
  }
  if (merged_last < chunks_.size()) {
    chunks_.erase(chunks_.begin() + merged_last, chunks_.end());
  }
}

ImageChunk* Image::FindChunkByName(const std::string& name, bool find_normal) {
  if (name.empty()) {
    return nullptr;
  }
  for (auto& chunk : chunks_) {
    if ((chunk.GetType() == CHUNK_DEFLATE || find_normal) && chunk.GetEntryName() == name) {
      return &chunk;
    }
  }
  return nullptr;
}

bool Image::WritePatchDataToFd(const std::vector<std::vector<uint8_t>>& patch_data,
                               int patch_fd) const {
  // Figure out how big the imgdiff file header is going to be, so that we can correctly compute
  // the offset of each bsdiff patch within the file.
  CHECK_EQ(chunks_.size(), patch_data.size());
  size_t total_header_size = 12;
  for (size_t i = 0; i < chunks_.size(); ++i) {
    total_header_size += chunks_[i].GetHeaderSize(patch_data[i].size());
  }

  size_t offset = total_header_size;

  // Write out the headers.
  if (!android::base::WriteStringToFd("IMGDIFF2", patch_fd)) {
    printf("failed to write \"IMGDIFF2\": %s\n", strerror(errno));
    return false;
  }
  Write4(patch_fd, static_cast<int32_t>(chunks_.size()));
  for (size_t i = 0; i < chunks_.size(); ++i) {
    printf("chunk %zu: ", i);
    offset = chunks_[i].WriteHeaderToFd(patch_fd, patch_data[i], offset);
  }

  // Append each chunk's bsdiff patch, in order.
  for (size_t i = 0; i < chunks_.size(); ++i) {
    if (chunks_[i].GetType() != CHUNK_RAW) {
      if (!android::base::WriteFully(patch_fd, patch_data[i].data(), patch_data[i].size())) {
        printf("failed to write %zu bytes patch for chunk %zu\n", patch_data[i].size(), i);
        return false;
      }
    }
  }

  return true;
}

void Image::DumpChunks() const {
  std::string type = is_source_ ? "source" : "target";
  printf("Dumping chunks for %s\n", type.c_str());
  for (size_t i = 0; i < chunks_.size(); ++i) {
    printf("chunk %zu: ", i);
    chunks_[i].Dump();
  }
}

bool Image::ReadFile(const std::string& filename, std::vector<uint8_t>* file_content) {
  CHECK(file_content != nullptr);

  android::base::unique_fd fd(open(filename.c_str(), O_RDONLY));
  if (fd == -1) {
    printf("failed to open \"%s\" %s\n", filename.c_str(), strerror(errno));
    return false;
  }
  struct stat st;
  if (fstat(fd, &st) != 0) {
    printf("failed to stat \"%s\": %s\n", filename.c_str(), strerror(errno));
    return false;
  }

  size_t sz = static_cast<size_t>(st.st_size);
  file_content->resize(sz);
  if (!android::base::ReadFully(fd, file_content->data(), sz)) {
    printf("failed to read \"%s\" %s\n", filename.c_str(), strerror(errno));
    return false;
  }
  fd.reset();

  return true;
}

class ZipModeImage : public Image {
 public:
  explicit ZipModeImage(bool is_source) : Image(is_source) {}

  bool Initialize(const std::string& filename) override;

  const ImageChunk& PseudoSource() const {
    CHECK(is_source_);
    CHECK(pseudo_source_ != nullptr);
    return *pseudo_source_;
  }

  // Verify that we can reconstruct the deflate chunks; also change the type to CHUNK_NORMAL if
  // src and tgt are identical.
  static bool CheckAndProcessChunks(ZipModeImage* tgt_image, ZipModeImage* src_image);

  // Compute the patches against the input image, and write the data into |patch_name|.
  static bool GeneratePatches(ZipModeImage* tgt_image, ZipModeImage* src_image,
                              const std::string& patch_name);

 private:
  // Initialize image chunks based on the zip entries.
  bool InitializeChunks(const std::string& filename, ZipArchiveHandle handle);
  // Add the a zip entry to the list.
  bool AddZipEntryToChunks(ZipArchiveHandle handle, const std::string& entry_name, ZipEntry* entry);
  // Return the real size of the zip file. (omit the trailing zeros that used for alignment)
  bool GetZipFileSize(size_t* input_file_size);

  // The pesudo source chunk for bsdiff if there's no match for the given target chunk. It's in
  // fact the whole source file.
  std::unique_ptr<ImageChunk> pseudo_source_;
};

bool ZipModeImage::Initialize(const std::string& filename) {
  if (!ReadFile(filename, &file_content_)) {
    return false;
  }

  // Omit the trailing zeros before we pass the file to ziparchive handler.
  size_t zipfile_size;
  if (!GetZipFileSize(&zipfile_size)) {
    printf("failed to parse the actual size of %s\n", filename.c_str());
    return false;
  }
  ZipArchiveHandle handle;
  int err = OpenArchiveFromMemory(const_cast<uint8_t*>(file_content_.data()), zipfile_size,
                                  filename.c_str(), &handle);
  if (err != 0) {
    printf("failed to open zip file %s: %s\n", filename.c_str(), ErrorCodeString(err));
    CloseArchive(handle);
    return false;
  }

  if (is_source_) {
    pseudo_source_ = std::make_unique<ImageChunk>(CHUNK_NORMAL, 0, &file_content_, zipfile_size);
  }
  if (!InitializeChunks(filename, handle)) {
    CloseArchive(handle);
    return false;
  }

  CloseArchive(handle);
  return true;
}

// Iterate the zip entries and compose the image chunks accordingly.
bool ZipModeImage::InitializeChunks(const std::string& filename, ZipArchiveHandle handle) {
  void* cookie;
  int ret = StartIteration(handle, &cookie, nullptr, nullptr);
  if (ret != 0) {
    printf("failed to iterate over entries in %s: %s\n", filename.c_str(), ErrorCodeString(ret));
    return false;
  }

  // Create a list of deflated zip entries, sorted by offset.
  std::vector<std::pair<std::string, ZipEntry>> temp_entries;
  ZipString name;
  ZipEntry entry;
  while ((ret = Next(cookie, &entry, &name)) == 0) {
    if (entry.method == kCompressDeflated) {
      std::string entry_name(name.name, name.name + name.name_length);
      temp_entries.emplace_back(entry_name, entry);
    }
  }

  if (ret != -1) {
    printf("Error while iterating over zip entries: %s\n", ErrorCodeString(ret));
    return false;
  }
  std::sort(temp_entries.begin(), temp_entries.end(),
            [](auto& entry1, auto& entry2) { return entry1.second.offset < entry2.second.offset; });

  EndIteration(cookie);

  // For source chunks, we don't need to compose chunks for the metadata.
  if (is_source_) {
    for (auto& entry : temp_entries) {
      if (!AddZipEntryToChunks(handle, entry.first, &entry.second)) {
        printf("Failed to add %s to source chunks\n", entry.first.c_str());
        return false;
      }
    }
    return true;
  }

  // For target chunks, add the deflate entries as CHUNK_DEFLATE and the contents between two
  // deflate entries as CHUNK_NORMAL.
  size_t pos = 0;
  size_t nextentry = 0;
  while (pos < file_content_.size()) {
    if (nextentry < temp_entries.size() &&
        static_cast<off64_t>(pos) == temp_entries[nextentry].second.offset) {
      // Add the next zip entry.
      std::string entry_name = temp_entries[nextentry].first;
      if (!AddZipEntryToChunks(handle, entry_name, &temp_entries[nextentry].second)) {
        printf("Failed to add %s to target chunks\n", entry_name.c_str());
        return false;
      }

      pos += temp_entries[nextentry].second.compressed_length;
      ++nextentry;
      continue;
    }

    // Use a normal chunk to take all the data up to the start of the next entry.
    size_t raw_data_len;
    if (nextentry < temp_entries.size()) {
      raw_data_len = temp_entries[nextentry].second.offset - pos;
    } else {
      raw_data_len = file_content_.size() - pos;
    }
    chunks_.emplace_back(CHUNK_NORMAL, pos, &file_content_, raw_data_len);

    pos += raw_data_len;
  }

  return true;
}

bool ZipModeImage::AddZipEntryToChunks(ZipArchiveHandle handle, const std::string& entry_name,
                                       ZipEntry* entry) {
  size_t compressed_len = entry->compressed_length;
  if (entry->method == kCompressDeflated) {
    size_t uncompressed_len = entry->uncompressed_length;
    std::vector<uint8_t> uncompressed_data(uncompressed_len);
    int ret = ExtractToMemory(handle, entry, uncompressed_data.data(), uncompressed_len);
    if (ret != 0) {
      printf("failed to extract %s with size %zu: %s\n", entry_name.c_str(), uncompressed_len,
             ErrorCodeString(ret));
      return false;
    }
    ImageChunk curr(CHUNK_DEFLATE, entry->offset, &file_content_, compressed_len);
    curr.SetEntryName(entry_name);
    curr.SetUncompressedData(std::move(uncompressed_data));
    chunks_.push_back(curr);
  } else {
    ImageChunk curr(CHUNK_NORMAL, entry->offset, &file_content_, compressed_len);
    curr.SetEntryName(entry_name);
    chunks_.push_back(curr);
  }

  return true;
}

// EOCD record
// offset 0: signature 0x06054b50, 4 bytes
// offset 4: number of this disk, 2 bytes
// ...
// offset 20: comment length, 2 bytes
// offset 22: comment, n bytes
bool ZipModeImage::GetZipFileSize(size_t* input_file_size) {
  if (file_content_.size() < 22) {
    printf("file is too small to be a zip file\n");
    return false;
  }

  // Look for End of central directory record of the zip file, and calculate the actual
  // zip_file size.
  for (int i = file_content_.size() - 22; i >= 0; i--) {
    if (file_content_[i] == 0x50) {
      if (get_unaligned<uint32_t>(&file_content_[i]) == 0x06054b50) {
        // double-check: this archive consists of a single "disk".
        CHECK_EQ(get_unaligned<uint16_t>(&file_content_[i + 4]), 0);

        uint16_t comment_length = get_unaligned<uint16_t>(&file_content_[i + 20]);
        size_t file_size = i + 22 + comment_length;
        CHECK_LE(file_size, file_content_.size());
        *input_file_size = file_size;
        return true;
      }
    }
  }

  // EOCD not found, this file is likely not a valid zip file.
  return false;
}

bool ZipModeImage::CheckAndProcessChunks(ZipModeImage* tgt_image, ZipModeImage* src_image) {
  for (auto& tgt_chunk : *tgt_image) {
    if (tgt_chunk.GetType() != CHUNK_DEFLATE) {
      continue;
    }

    ImageChunk* src_chunk = src_image->FindChunkByName(tgt_chunk.GetEntryName());
    if (src_chunk == nullptr) {
      tgt_chunk.ChangeDeflateChunkToNormal();
    } else if (tgt_chunk == *src_chunk) {
      // If two deflate chunks are identical (eg, the kernel has not changed between two builds),
      // treat them as normal chunks. This makes applypatch much faster -- it can apply a trivial
      // patch to the compressed data, rather than uncompressing and recompressing to apply the
      // trivial patch to the uncompressed data.
      tgt_chunk.ChangeDeflateChunkToNormal();
      src_chunk->ChangeDeflateChunkToNormal();
    } else if (!tgt_chunk.ReconstructDeflateChunk()) {
      // We cannot recompress the data and get exactly the same bits as are in the input target
      // image. Treat the chunk as a normal non-deflated chunk.
      printf("failed to reconstruct target deflate chunk [%s]; treating as normal\n",
             tgt_chunk.GetEntryName().c_str());

      tgt_chunk.ChangeDeflateChunkToNormal();
      src_chunk->ChangeDeflateChunkToNormal();
    }
  }

  return true;
}

bool ZipModeImage::GeneratePatches(ZipModeImage* tgt_image, ZipModeImage* src_image,
                                   const std::string& patch_name) {
  // For zips, we only need merge normal chunks for the target:  deflated chunks are matched via
  // filename, and normal chunks are patched using the entire source file as the source.
  tgt_image->MergeAdjacentNormalChunks();
  tgt_image->DumpChunks();

  printf("Construct patches for %zu chunks...\n", tgt_image->NumOfChunks());
  std::vector<std::vector<uint8_t>> patch_data(tgt_image->NumOfChunks());

  saidx_t* bsdiff_cache = nullptr;
  size_t i = 0;
  for (auto& tgt_chunk : *tgt_image) {
    ImageChunk* src_chunk = (tgt_chunk.GetType() != CHUNK_DEFLATE)
                                ? nullptr
                                : src_image->FindChunkByName(tgt_chunk.GetEntryName());

    const auto& src_ref = (src_chunk == nullptr) ? src_image->PseudoSource() : *src_chunk;
    saidx_t** bsdiff_cache_ptr = (src_chunk == nullptr) ? &bsdiff_cache : nullptr;

    if (!tgt_chunk.MakePatch(src_ref, &patch_data[i], bsdiff_cache_ptr)) {
      printf("Failed to generate patch, name: %s\n", tgt_chunk.GetEntryName().c_str());
      return false;
    }

    printf("patch %3zu is %zu bytes (of %zu)\n", i, patch_data[i].size(),
           tgt_chunk.GetRawDataLength());
    i++;
  }
  free(bsdiff_cache);

  android::base::unique_fd patch_fd(
      open(patch_name.c_str(), O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR));
  if (patch_fd == -1) {
    printf("failed to open \"%s\": %s\n", patch_name.c_str(), strerror(errno));
    return false;
  }

  return tgt_image->WritePatchDataToFd(patch_data, patch_fd);
}

class ImageModeImage : public Image {
 public:
  explicit ImageModeImage(bool is_source) : Image(is_source) {}

  // Initialize the image chunks list by searching the magic numbers in an image file.
  bool Initialize(const std::string& filename) override;

  // In Image Mode, verify that the source and target images have the same chunk structure (ie, the
  // same sequence of deflate and normal chunks).
  static bool CheckAndProcessChunks(ImageModeImage* tgt_image, ImageModeImage* src_image);

  // In image mode, generate patches against the given source chunks and bonus_data; write the
  // result to |patch_name|.
  static bool GeneratePatches(ImageModeImage* tgt_image, ImageModeImage* src_image,
                              const std::vector<uint8_t>& bonus_data, const std::string& patch_name);
};

bool ImageModeImage::Initialize(const std::string& filename) {
  if (!ReadFile(filename, &file_content_)) {
    return false;
  }

  size_t sz = file_content_.size();
  size_t pos = 0;
  while (pos < sz) {
    // 0x00 no header flags, 0x08 deflate compression, 0x1f8b gzip magic number
    if (sz - pos >= 4 && get_unaligned<uint32_t>(file_content_.data() + pos) == 0x00088b1f) {
      // 'pos' is the offset of the start of a gzip chunk.
      size_t chunk_offset = pos;

      // The remaining data is too small to be a gzip chunk; treat them as a normal chunk.
      if (sz - pos < GZIP_HEADER_LEN + GZIP_FOOTER_LEN) {
        chunks_.emplace_back(CHUNK_NORMAL, pos, &file_content_, sz - pos);
        break;
      }

      // We need three chunks for the deflated image in total, one normal chunk for the header,
      // one deflated chunk for the body, and another normal chunk for the footer.
      chunks_.emplace_back(CHUNK_NORMAL, pos, &file_content_, GZIP_HEADER_LEN);
      pos += GZIP_HEADER_LEN;

      // We must decompress this chunk in order to discover where it ends, and so we can update
      // the uncompressed_data of the image body and its length.

      z_stream strm;
      strm.zalloc = Z_NULL;
      strm.zfree = Z_NULL;
      strm.opaque = Z_NULL;
      strm.avail_in = sz - pos;
      strm.next_in = file_content_.data() + pos;

      // -15 means we are decoding a 'raw' deflate stream; zlib will
      // not expect zlib headers.
      int ret = inflateInit2(&strm, -15);
      if (ret < 0) {
        printf("failed to initialize inflate: %d\n", ret);
        return false;
      }

      size_t allocated = BUFFER_SIZE;
      std::vector<uint8_t> uncompressed_data(allocated);
      size_t uncompressed_len = 0, raw_data_len = 0;
      do {
        strm.avail_out = allocated - uncompressed_len;
        strm.next_out = uncompressed_data.data() + uncompressed_len;
        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret < 0) {
          printf("Warning: inflate failed [%s] at offset [%zu], treating as a normal chunk\n",
                 strm.msg, chunk_offset);
          break;
        }
        uncompressed_len = allocated - strm.avail_out;
        if (strm.avail_out == 0) {
          allocated *= 2;
          uncompressed_data.resize(allocated);
        }
      } while (ret != Z_STREAM_END);

      raw_data_len = sz - strm.avail_in - pos;
      inflateEnd(&strm);

      if (ret < 0) {
        continue;
      }

      // The footer contains the size of the uncompressed data.  Double-check to make sure that it
      // matches the size of the data we got when we actually did the decompression.
      size_t footer_index = pos + raw_data_len + GZIP_FOOTER_LEN - 4;
      if (sz - footer_index < 4) {
        printf("Warning: invalid footer position; treating as a nomal chunk\n");
        continue;
      }
      size_t footer_size = get_unaligned<uint32_t>(file_content_.data() + footer_index);
      if (footer_size != uncompressed_len) {
        printf("Warning: footer size %zu != decompressed size %zu; treating as a nomal chunk\n",
               footer_size, uncompressed_len);
        continue;
      }

      ImageChunk body(CHUNK_DEFLATE, pos, &file_content_, raw_data_len);
      uncompressed_data.resize(uncompressed_len);
      body.SetUncompressedData(std::move(uncompressed_data));
      chunks_.push_back(body);

      pos += raw_data_len;

      // create a normal chunk for the footer
      chunks_.emplace_back(CHUNK_NORMAL, pos, &file_content_, GZIP_FOOTER_LEN);

      pos += GZIP_FOOTER_LEN;
    } else {
      // Use a normal chunk to take all the contents until the next gzip chunk (or EOF); we expect
      // the number of chunks to be small (5 for typical boot and recovery images).

      // Scan forward until we find a gzip header.
      size_t data_len = 0;
      while (data_len + pos < sz) {
        if (data_len + pos + 4 <= sz &&
            get_unaligned<uint32_t>(file_content_.data() + pos + data_len) == 0x00088b1f) {
          break;
        }
        data_len++;
      }
      chunks_.emplace_back(CHUNK_NORMAL, pos, &file_content_, data_len);

      pos += data_len;
    }
  }

  return true;
}

// In Image Mode, verify that the source and target images have the same chunk structure (ie, the
// same sequence of deflate and normal chunks).
bool ImageModeImage::CheckAndProcessChunks(ImageModeImage* tgt_image, ImageModeImage* src_image) {
  // In image mode, merge the gzip header and footer in with any adjacent normal chunks.
  tgt_image->MergeAdjacentNormalChunks();
  src_image->MergeAdjacentNormalChunks();

  if (tgt_image->NumOfChunks() != src_image->NumOfChunks()) {
    printf("source and target don't have same number of chunks!\n");
    tgt_image->DumpChunks();
    src_image->DumpChunks();
    return false;
  }
  for (size_t i = 0; i < tgt_image->NumOfChunks(); ++i) {
    if (tgt_image->Get(i)->GetType() != src_image->Get(i)->GetType()) {
      printf("source and target don't have same chunk structure! (chunk %zu)\n", i);
      tgt_image->DumpChunks();
      src_image->DumpChunks();
      return false;
    }
  }

  for (size_t i = 0; i < tgt_image->NumOfChunks(); ++i) {
    auto& tgt_chunk = *tgt_image->Get(i);
    auto& src_chunk = *src_image->Get(i);
    if (tgt_chunk.GetType() != CHUNK_DEFLATE) {
      continue;
    }

    // Confirm that we can recompress the data and get exactly the same bits as are in the
    // input target image.
    if (!tgt_chunk.ReconstructDeflateChunk()) {
      printf("failed to reconstruct target deflate chunk %zu [%s]; treating as normal\n", i,
             tgt_chunk.GetEntryName().c_str());
      tgt_chunk.ChangeDeflateChunkToNormal();
      src_chunk.ChangeDeflateChunkToNormal();
      continue;
    }

    // If two deflate chunks are identical treat them as normal chunks.
    if (tgt_chunk == src_chunk) {
      tgt_chunk.ChangeDeflateChunkToNormal();
      src_chunk.ChangeDeflateChunkToNormal();
    }
  }

  // For images, we need to maintain the parallel structure of the chunk lists, so do the merging
  // in both the source and target lists.
  tgt_image->MergeAdjacentNormalChunks();
  src_image->MergeAdjacentNormalChunks();
  if (tgt_image->NumOfChunks() != src_image->NumOfChunks()) {
    // This shouldn't happen.
    printf("merging normal chunks went awry\n");
    return false;
  }

  return true;
}

// In image mode, generate patches against the given source chunks and bonus_data; write the
// result to |patch_name|.
bool ImageModeImage::GeneratePatches(ImageModeImage* tgt_image, ImageModeImage* src_image,
                                     const std::vector<uint8_t>& bonus_data,
                                     const std::string& patch_name) {
  printf("Construct patches for %zu chunks...\n", tgt_image->NumOfChunks());
  std::vector<std::vector<uint8_t>> patch_data(tgt_image->NumOfChunks());

  for (size_t i = 0; i < tgt_image->NumOfChunks(); i++) {
    auto& tgt_chunk = *tgt_image->Get(i);
    auto& src_chunk = *src_image->Get(i);

    if (i == 1 && !bonus_data.empty()) {
      printf("  using %zu bytes of bonus data for chunk %zu\n", bonus_data.size(), i);
      src_chunk.SetBonusData(bonus_data);
    }

    if (!tgt_chunk.MakePatch(src_chunk, &patch_data[i], nullptr)) {
      printf("Failed to generate patch for target chunk %zu: ", i);
      return false;
    }
    printf("patch %3zu is %zu bytes (of %zu)\n", i, patch_data[i].size(),
           tgt_chunk.GetRawDataLength());
  }

  android::base::unique_fd patch_fd(
      open(patch_name.c_str(), O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR));
  if (patch_fd == -1) {
    printf("failed to open \"%s\": %s\n", patch_name.c_str(), strerror(errno));
    return false;
  }

  return tgt_image->WritePatchDataToFd(patch_data, patch_fd);
}

int imgdiff(int argc, const char** argv) {
  bool zip_mode = false;
  std::vector<uint8_t> bonus_data;

  int opt;
  optind = 1;  // Reset the getopt state so that we can call it multiple times for test.

  while ((opt = getopt(argc, const_cast<char**>(argv), "zb:")) != -1) {
    switch (opt) {
      case 'z':
        zip_mode = true;
        break;
      case 'b': {
        android::base::unique_fd fd(open(optarg, O_RDONLY));
        if (fd == -1) {
          printf("failed to open bonus file %s: %s\n", optarg, strerror(errno));
          return 1;
        }
        struct stat st;
        if (fstat(fd, &st) != 0) {
          printf("failed to stat bonus file %s: %s\n", optarg, strerror(errno));
          return 1;
        }

        size_t bonus_size = st.st_size;
        bonus_data.resize(bonus_size);
        if (!android::base::ReadFully(fd, bonus_data.data(), bonus_size)) {
          printf("failed to read bonus file %s: %s\n", optarg, strerror(errno));
          return 1;
        }
        break;
      }
      default:
        printf("unexpected opt: %s\n", optarg);
        return 2;
    }
  }

  if (argc - optind != 3) {
    printf("usage: %s [-z] [-b <bonus-file>] <src-img> <tgt-img> <patch-file>\n", argv[0]);
    return 2;
  }

  if (zip_mode) {
    ZipModeImage src_image(true);
    ZipModeImage tgt_image(false);

    if (!src_image.Initialize(argv[optind])) {
      return 1;
    }
    if (!tgt_image.Initialize(argv[optind + 1])) {
      return 1;
    }

    if (!ZipModeImage::CheckAndProcessChunks(&tgt_image, &src_image)) {
      return 1;
    }
    // Compute bsdiff patches for each chunk's data (the uncompressed data, in the case of
    // deflate chunks).
    if (!ZipModeImage::GeneratePatches(&tgt_image, &src_image, argv[optind + 2])) {
      return 1;
    }
  } else {
    ImageModeImage src_image(true);
    ImageModeImage tgt_image(false);

    if (!src_image.Initialize(argv[optind])) {
      return 1;
    }
    if (!tgt_image.Initialize(argv[optind + 1])) {
      return 1;
    }

    if (!ImageModeImage::CheckAndProcessChunks(&tgt_image, &src_image)) {
      return 1;
    }
    if (!ImageModeImage::GeneratePatches(&tgt_image, &src_image, bonus_data, argv[optind + 2])) {
      return 1;
    }
  }

  return 0;
}
