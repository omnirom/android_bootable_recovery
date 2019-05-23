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
 * This program constructs binary patches for images -- such as boot.img and recovery.img -- that
 * consist primarily of large chunks of gzipped data interspersed with uncompressed data.  Doing a
 * naive bsdiff of these files is not useful because small changes in the data lead to large
 * changes in the compressed bitstream; bsdiff patches of gzipped data are typically as large as
 * the data itself.
 *
 * To patch these usefully, we break the source and target images up into chunks of two types:
 * "normal" and "gzip".  Normal chunks are simply patched using a plain bsdiff.  Gzip chunks are
 * first expanded, then a bsdiff is applied to the uncompressed data, then the patched data is
 * gzipped using the same encoder parameters.  Patched chunks are concatenated together to create
 * the output file; the output image should be *exactly* the same series of bytes as the target
 * image used originally to generate the patch.
 *
 * To work well with this tool, the gzipped sections of the target image must have been generated
 * using the same deflate encoder that is available in applypatch, namely, the one in the zlib
 * library.  In practice this means that images should be compressed using the "minigzip" tool
 * included in the zlib distribution, not the GNU gzip program.
 *
 * An "imgdiff" patch consists of a header describing the chunk structure of the file and any
 * encoding parameters needed for the gzipped chunks, followed by N bsdiff patches, one per chunk.
 *
 * For a diff to be generated, the source and target must be in well-formed zip archive format;
 * or they are image files with the same "chunk" structure: that is, the same number of gzipped and
 * normal chunks in the same order.  Android boot and recovery images currently consist of five
 * chunks: a small normal header, a gzipped kernel, a small normal section, a gzipped ramdisk, and
 * finally a small normal footer.
 *
 * Caveats:  we locate gzipped sections within the source and target images by searching for the
 * byte sequence 1f8b0800:  1f8b is the gzip magic number; 08 specifies the "deflate" encoding
 * [the only encoding supported by the gzip standard]; and 00 is the flags byte.  We do not
 * currently support any extra header fields (which would be indicated by a nonzero flags byte).
 * We also don't handle the case when that byte sequence appears spuriously in the file.  (Note
 * that it would have to occur spuriously within a normal chunk to be a problem.)
 *
 *
 * The imgdiff patch header looks like this:
 *
 *    "IMGDIFF2"                  (8)   [magic number and version]
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
 * All integers are little-endian.  "source start" and "source len" specify the section of the
 * input image that comprises this chunk, including the gzip header and footer for gzip chunks.
 * "source expanded len" is the size of the uncompressed source data.  "target expected len" is the
 * size of the uncompressed data after applying the bsdiff patch.  The next five parameters
 * specify the zlib parameters to be used when compressing the patched data, and the next three
 * specify the header and footer to be wrapped around the compressed data to create the output
 * chunk (so that header contents like the timestamp are recreated exactly).
 *
 * After the header there are 'chunk count' bsdiff patches; the offset of each from the beginning
 * of the file is specified in the header.
 *
 * This tool can take an optional file of "bonus data".  This is an extra file of data that is
 * appended to chunk #1 after it is compressed (it must be a CHUNK_DEFLATE chunk).  The same file
 * must be available (and passed to applypatch with -b) when applying the patch.  This is used to
 * reduce the size of recovery-from-boot patches by combining the boot image with recovery ramdisk
 * information that is stored on the system partition.
 *
 * When generating the patch between two zip files, this tool has an option "--block-limit" to
 * split the large source/target files into several pair of pieces, with each piece has at most
 * *limit* blocks.  When this option is used, we also need to output the split info into the file
 * path specified by "--split-info".
 *
 * Format of split info file:
 *   2                                      [version of imgdiff]
 *   n                                      [count of split pieces]
 *   <patch_size>, <tgt_size>, <src_range>  [size and ranges for split piece#1]
 *   ...
 *   <patch_size>, <tgt_size>, <src_range>  [size and ranges for split piece#n]
 *
 * To split a pair of large zip files, we walk through the chunks in target zip and search by its
 * entry_name in the source zip.  If the entry_name is non-empty and a matching entry in source
 * is found, we'll add the source entry to the current split source image; otherwise we'll skip
 * this chunk and later do bsdiff between all the skipped trunks and the whole split source image.
 * We move on to the next pair of pieces if the size of the split source image reaches the block
 * limit.
 *
 * After the split, the target pieces are continuous and block aligned, while the source pieces
 * are mutually exclusive.  Some of the source blocks may not be used if there's no matching
 * entry_name in the target; as a result, they won't be included in any of these split source
 * images.  Then we will generate patches accordingly between each split image pairs; in particular,
 * the unmatched trunks in the split target will diff against the entire split source image.
 *
 * For example:
 * Input: [src_image, tgt_image]
 * Split: [src-0, tgt-0; src-1, tgt-1, src-2, tgt-2]
 * Diff:  [  patch-0;      patch-1;      patch-2]
 *
 * Patch: [(src-0, patch-0) = tgt-0; (src-1, patch-1) = tgt-1; (src-2, patch-2) = tgt-2]
 * Concatenate: [tgt-0 + tgt-1 + tgt-2 = tgt_image]
 */

#include "applypatch/imgdiff.h"

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
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
#include <android-base/parseint.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#include <android-base/unique_fd.h>
#include <bsdiff/bsdiff.h>
#include <ziparchive/zip_archive.h>
#include <zlib.h>

#include "applypatch/imgdiff_image.h"
#include "otautil/rangeset.h"

using android::base::get_unaligned;

static constexpr size_t VERSION = 2;

// We assume the header "IMGDIFF#" is 8 bytes.
static_assert(VERSION <= 9, "VERSION occupies more than one byte");

static constexpr size_t BLOCK_SIZE = 4096;
static constexpr size_t BUFFER_SIZE = 0x8000;

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

// Trim the head or tail to align with the block size. Return false if the chunk has nothing left
// after alignment.
static bool AlignHead(size_t* start, size_t* length) {
  size_t residual = (*start % BLOCK_SIZE == 0) ? 0 : BLOCK_SIZE - *start % BLOCK_SIZE;

  if (*length <= residual) {
    *length = 0;
    return false;
  }

  // Trim the data in the beginning.
  *start += residual;
  *length -= residual;
  return true;
}

static bool AlignTail(size_t* start, size_t* length) {
  size_t residual = (*start + *length) % BLOCK_SIZE;
  if (*length <= residual) {
    *length = 0;
    return false;
  }

  // Trim the data in the end.
  *length -= residual;
  return true;
}

// Remove the used blocks from the source chunk to make sure the source ranges are mutually
// exclusive after split. Return false if we fail to get the non-overlapped ranges. In such
// a case, we'll skip the entire source chunk.
static bool RemoveUsedBlocks(size_t* start, size_t* length, const SortedRangeSet& used_ranges) {
  if (!used_ranges.Overlaps(*start, *length)) {
    return true;
  }

  // TODO find the largest non-overlap chunk.
  LOG(INFO) << "Removing block " << used_ranges.ToString() << " from " << *start << " - "
            << *start + *length - 1;

  // If there's no duplicate entry name, we should only overlap in the head or tail block. Try to
  // trim both blocks. Skip this source chunk in case it still overlaps with the used ranges.
  if (AlignHead(start, length) && !used_ranges.Overlaps(*start, *length)) {
    return true;
  }
  if (AlignTail(start, length) && !used_ranges.Overlaps(*start, *length)) {
    return true;
  }

  LOG(WARNING) << "Failed to remove the overlapped block ranges; skip the source";
  return false;
}

static const struct option OPTIONS[] = {
  { "zip-mode", no_argument, nullptr, 'z' },
  { "bonus-file", required_argument, nullptr, 'b' },
  { "block-limit", required_argument, nullptr, 0 },
  { "debug-dir", required_argument, nullptr, 0 },
  { "split-info", required_argument, nullptr, 0 },
  { "verbose", no_argument, nullptr, 'v' },
  { nullptr, 0, nullptr, 0 },
};

ImageChunk::ImageChunk(int type, size_t start, const std::vector<uint8_t>* file_content,
                       size_t raw_data_len, std::string entry_name)
    : type_(type),
      start_(start),
      input_file_ptr_(file_content),
      raw_data_len_(raw_data_len),
      compress_level_(6),
      entry_name_(std::move(entry_name)) {
  CHECK(file_content != nullptr) << "input file container can't be nullptr";
}

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

void ImageChunk::Dump(size_t index) const {
  LOG(INFO) << "chunk: " << index << ", type: " << type_ << ", start: " << start_
            << ", len: " << DataLengthForPatch() << ", name: " << entry_name_;
}

bool ImageChunk::operator==(const ImageChunk& other) const {
  if (type_ != other.type_) {
    return false;
  }
  return (raw_data_len_ == other.raw_data_len_ &&
          memcmp(GetRawData(), other.GetRawData(), raw_data_len_) == 0);
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

void ImageChunk::ChangeDeflateChunkToNormal() {
  if (type_ != CHUNK_DEFLATE) return;
  type_ = CHUNK_NORMAL;
  // No need to clear the entry name.
  uncompressed_data_.clear();
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

bool ImageChunk::MakePatch(const ImageChunk& tgt, const ImageChunk& src,
                           std::vector<uint8_t>* patch_data,
                           bsdiff::SuffixArrayIndexInterface** bsdiff_cache) {
#if defined(__ANDROID__)
  char ptemp[] = "/data/local/tmp/imgdiff-patch-XXXXXX";
#else
  char ptemp[] = "/tmp/imgdiff-patch-XXXXXX";
#endif

  int fd = mkstemp(ptemp);
  if (fd == -1) {
    PLOG(ERROR) << "MakePatch failed to create a temporary file";
    return false;
  }
  close(fd);

  int r = bsdiff::bsdiff(src.DataForPatch(), src.DataLengthForPatch(), tgt.DataForPatch(),
                         tgt.DataLengthForPatch(), ptemp, bsdiff_cache);
  if (r != 0) {
    LOG(ERROR) << "bsdiff() failed: " << r;
    return false;
  }

  android::base::unique_fd patch_fd(open(ptemp, O_RDONLY));
  if (patch_fd == -1) {
    PLOG(ERROR) << "Failed to open " << ptemp;
    return false;
  }
  struct stat st;
  if (fstat(patch_fd, &st) != 0) {
    PLOG(ERROR) << "Failed to stat patch file " << ptemp;
    return false;
  }

  size_t sz = static_cast<size_t>(st.st_size);

  patch_data->resize(sz);
  if (!android::base::ReadFully(patch_fd, patch_data->data(), sz)) {
    PLOG(ERROR) << "Failed to read " << ptemp;
    unlink(ptemp);
    return false;
  }

  unlink(ptemp);

  return true;
}

bool ImageChunk::ReconstructDeflateChunk() {
  if (type_ != CHUNK_DEFLATE) {
    LOG(ERROR) << "Attempted to reconstruct non-deflate chunk";
    return false;
  }

  // We only check two combinations of encoder parameters:  level 6 (the default) and level 9
  // (the maximum).
  for (int level = 6; level <= 9; level += 3) {
    if (TryReconstruction(level)) {
      compress_level_ = level;
      return true;
    }
  }

  return false;
}

/*
 * Takes the uncompressed data stored in the chunk, compresses it using the zlib parameters stored
 * in the chunk, and checks that it matches exactly the compressed data we started with (also
 * stored in the chunk).
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
    LOG(ERROR) << "Failed to initialize deflate: " << ret;
    return false;
  }

  std::vector<uint8_t> buffer(BUFFER_SIZE);
  size_t offset = 0;
  do {
    strm.avail_out = buffer.size();
    strm.next_out = buffer.data();
    ret = deflate(&strm, Z_FINISH);
    if (ret < 0) {
      LOG(ERROR) << "Failed to deflate: " << ret;
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

PatchChunk::PatchChunk(const ImageChunk& tgt, const ImageChunk& src, std::vector<uint8_t> data)
    : type_(tgt.GetType()),
      source_start_(src.GetStartOffset()),
      source_len_(src.GetRawDataLength()),
      source_uncompressed_len_(src.DataLengthForPatch()),
      target_start_(tgt.GetStartOffset()),
      target_len_(tgt.GetRawDataLength()),
      target_uncompressed_len_(tgt.DataLengthForPatch()),
      target_compress_level_(tgt.GetCompressLevel()),
      data_(std::move(data)) {}

// Construct a CHUNK_RAW patch from the target data directly.
PatchChunk::PatchChunk(const ImageChunk& tgt)
    : type_(CHUNK_RAW),
      source_start_(0),
      source_len_(0),
      source_uncompressed_len_(0),
      target_start_(tgt.GetStartOffset()),
      target_len_(tgt.GetRawDataLength()),
      target_uncompressed_len_(tgt.DataLengthForPatch()),
      target_compress_level_(tgt.GetCompressLevel()),
      data_(tgt.GetRawData(), tgt.GetRawData() + tgt.GetRawDataLength()) {}

// Return true if raw data is smaller than the patch size.
bool PatchChunk::RawDataIsSmaller(const ImageChunk& tgt, size_t patch_size) {
  size_t target_len = tgt.GetRawDataLength();
  return target_len < patch_size || (tgt.GetType() == CHUNK_NORMAL && target_len <= 160);
}

void PatchChunk::UpdateSourceOffset(const SortedRangeSet& src_range) {
  if (type_ == CHUNK_DEFLATE) {
    source_start_ = src_range.GetOffsetInRangeSet(source_start_);
  }
}

// Header size:
// header_type    4 bytes
// CHUNK_NORMAL   8*3 = 24 bytes
// CHUNK_DEFLATE  8*5 + 4*5 = 60 bytes
// CHUNK_RAW      4 bytes + patch_size
size_t PatchChunk::GetHeaderSize() const {
  switch (type_) {
    case CHUNK_NORMAL:
      return 4 + 8 * 3;
    case CHUNK_DEFLATE:
      return 4 + 8 * 5 + 4 * 5;
    case CHUNK_RAW:
      return 4 + 4 + data_.size();
    default:
      CHECK(false) << "unexpected chunk type: " << type_;  // Should not reach here.
      return 0;
  }
}

// Return the offset of the next patch into the patch data.
size_t PatchChunk::WriteHeaderToFd(int fd, size_t offset, size_t index) const {
  Write4(fd, type_);
  switch (type_) {
    case CHUNK_NORMAL:
      LOG(INFO) << android::base::StringPrintf("chunk %zu: normal   (%10zu, %10zu)  %10zu", index,
                                               target_start_, target_len_, data_.size());
      Write8(fd, static_cast<int64_t>(source_start_));
      Write8(fd, static_cast<int64_t>(source_len_));
      Write8(fd, static_cast<int64_t>(offset));
      return offset + data_.size();
    case CHUNK_DEFLATE:
      LOG(INFO) << android::base::StringPrintf("chunk %zu: deflate  (%10zu, %10zu)  %10zu", index,
                                               target_start_, target_len_, data_.size());
      Write8(fd, static_cast<int64_t>(source_start_));
      Write8(fd, static_cast<int64_t>(source_len_));
      Write8(fd, static_cast<int64_t>(offset));
      Write8(fd, static_cast<int64_t>(source_uncompressed_len_));
      Write8(fd, static_cast<int64_t>(target_uncompressed_len_));
      Write4(fd, target_compress_level_);
      Write4(fd, ImageChunk::METHOD);
      Write4(fd, ImageChunk::WINDOWBITS);
      Write4(fd, ImageChunk::MEMLEVEL);
      Write4(fd, ImageChunk::STRATEGY);
      return offset + data_.size();
    case CHUNK_RAW:
      LOG(INFO) << android::base::StringPrintf("chunk %zu: raw      (%10zu, %10zu)", index,
                                               target_start_, target_len_);
      Write4(fd, static_cast<int32_t>(data_.size()));
      if (!android::base::WriteFully(fd, data_.data(), data_.size())) {
        CHECK(false) << "Failed to write " << data_.size() << " bytes patch";
      }
      return offset;
    default:
      CHECK(false) << "unexpected chunk type: " << type_;
      return offset;
  }
}

size_t PatchChunk::PatchSize() const {
  if (type_ == CHUNK_RAW) {
    return GetHeaderSize();
  }
  return GetHeaderSize() + data_.size();
}

// Write the contents of |patch_chunks| to |patch_fd|.
bool PatchChunk::WritePatchDataToFd(const std::vector<PatchChunk>& patch_chunks, int patch_fd) {
  // Figure out how big the imgdiff file header is going to be, so that we can correctly compute
  // the offset of each bsdiff patch within the file.
  size_t total_header_size = 12;
  for (const auto& patch : patch_chunks) {
    total_header_size += patch.GetHeaderSize();
  }

  size_t offset = total_header_size;

  // Write out the headers.
  if (!android::base::WriteStringToFd("IMGDIFF" + std::to_string(VERSION), patch_fd)) {
    PLOG(ERROR) << "Failed to write \"IMGDIFF" << VERSION << "\"";
    return false;
  }

  Write4(patch_fd, static_cast<int32_t>(patch_chunks.size()));
  LOG(INFO) << "Writing " << patch_chunks.size() << " patch headers...";
  for (size_t i = 0; i < patch_chunks.size(); ++i) {
    offset = patch_chunks[i].WriteHeaderToFd(patch_fd, offset, i);
  }

  // Append each chunk's bsdiff patch, in order.
  for (const auto& patch : patch_chunks) {
    if (patch.type_ == CHUNK_RAW) {
      continue;
    }
    if (!android::base::WriteFully(patch_fd, patch.data_.data(), patch.data_.size())) {
      PLOG(ERROR) << "Failed to write " << patch.data_.size() << " bytes patch to patch_fd";
      return false;
    }
  }

  return true;
}

ImageChunk& Image::operator[](size_t i) {
  CHECK_LT(i, chunks_.size());
  return chunks_[i];
}

const ImageChunk& Image::operator[](size_t i) const {
  CHECK_LT(i, chunks_.size());
  return chunks_[i];
}

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

void Image::DumpChunks() const {
  std::string type = is_source_ ? "source" : "target";
  LOG(INFO) << "Dumping chunks for " << type;
  for (size_t i = 0; i < chunks_.size(); ++i) {
    chunks_[i].Dump(i);
  }
}

bool Image::ReadFile(const std::string& filename, std::vector<uint8_t>* file_content) {
  CHECK(file_content != nullptr);

  android::base::unique_fd fd(open(filename.c_str(), O_RDONLY));
  if (fd == -1) {
    PLOG(ERROR) << "Failed to open " << filename;
    return false;
  }
  struct stat st;
  if (fstat(fd, &st) != 0) {
    PLOG(ERROR) << "Failed to stat " << filename;
    return false;
  }

  size_t sz = static_cast<size_t>(st.st_size);
  file_content->resize(sz);
  if (!android::base::ReadFully(fd, file_content->data(), sz)) {
    PLOG(ERROR) << "Failed to read " << filename;
    return false;
  }
  fd.reset();

  return true;
}

bool ZipModeImage::Initialize(const std::string& filename) {
  if (!ReadFile(filename, &file_content_)) {
    return false;
  }

  // Omit the trailing zeros before we pass the file to ziparchive handler.
  size_t zipfile_size;
  if (!GetZipFileSize(&zipfile_size)) {
    LOG(ERROR) << "Failed to parse the actual size of " << filename;
    return false;
  }
  ZipArchiveHandle handle;
  int err = OpenArchiveFromMemory(const_cast<uint8_t*>(file_content_.data()), zipfile_size,
                                  filename.c_str(), &handle);
  if (err != 0) {
    LOG(ERROR) << "Failed to open zip file " << filename << ": " << ErrorCodeString(err);
    CloseArchive(handle);
    return false;
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
  int ret = StartIteration(handle, &cookie);
  if (ret != 0) {
    LOG(ERROR) << "Failed to iterate over entries in " << filename << ": " << ErrorCodeString(ret);
    return false;
  }

  // Create a list of deflated zip entries, sorted by offset.
  std::vector<std::pair<std::string, ZipEntry>> temp_entries;
  std::string name;
  ZipEntry entry;
  while ((ret = Next(cookie, &entry, &name)) == 0) {
    if (entry.method == kCompressDeflated || limit_ > 0) {
      temp_entries.emplace_back(name, entry);
    }
  }

  if (ret != -1) {
    LOG(ERROR) << "Error while iterating over zip entries: " << ErrorCodeString(ret);
    return false;
  }
  std::sort(temp_entries.begin(), temp_entries.end(),
            [](auto& entry1, auto& entry2) { return entry1.second.offset < entry2.second.offset; });

  EndIteration(cookie);

  // For source chunks, we don't need to compose chunks for the metadata.
  if (is_source_) {
    for (auto& entry : temp_entries) {
      if (!AddZipEntryToChunks(handle, entry.first, &entry.second)) {
        LOG(ERROR) << "Failed to add " << entry.first << " to source chunks";
        return false;
      }
    }

    // Add the end of zip file (mainly central directory) as a normal chunk.
    size_t entries_end = 0;
    if (!temp_entries.empty()) {
      entries_end = static_cast<size_t>(temp_entries.back().second.offset +
                                        temp_entries.back().second.compressed_length);
    }
    CHECK_LT(entries_end, file_content_.size());
    chunks_.emplace_back(CHUNK_NORMAL, entries_end, &file_content_,
                         file_content_.size() - entries_end);

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
        LOG(ERROR) << "Failed to add " << entry_name << " to target chunks";
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
  if (compressed_len == 0) return true;

  // Split the entry into several normal chunks if it's too large.
  if (limit_ > 0 && compressed_len > limit_) {
    int count = 0;
    while (compressed_len > 0) {
      size_t length = std::min(limit_, compressed_len);
      std::string name = entry_name + "-" + std::to_string(count);
      chunks_.emplace_back(CHUNK_NORMAL, entry->offset + limit_ * count, &file_content_, length,
                           name);

      count++;
      compressed_len -= length;
    }
  } else if (entry->method == kCompressDeflated) {
    size_t uncompressed_len = entry->uncompressed_length;
    std::vector<uint8_t> uncompressed_data(uncompressed_len);
    int ret = ExtractToMemory(handle, entry, uncompressed_data.data(), uncompressed_len);
    if (ret != 0) {
      LOG(ERROR) << "Failed to extract " << entry_name << " with size " << uncompressed_len << ": "
                 << ErrorCodeString(ret);
      return false;
    }
    ImageChunk curr(CHUNK_DEFLATE, entry->offset, &file_content_, compressed_len, entry_name);
    curr.SetUncompressedData(std::move(uncompressed_data));
    chunks_.push_back(std::move(curr));
  } else {
    chunks_.emplace_back(CHUNK_NORMAL, entry->offset, &file_content_, compressed_len, entry_name);
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
    LOG(ERROR) << "File is too small to be a zip file";
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

ImageChunk ZipModeImage::PseudoSource() const {
  CHECK(is_source_);
  return ImageChunk(CHUNK_NORMAL, 0, &file_content_, file_content_.size());
}

const ImageChunk* ZipModeImage::FindChunkByName(const std::string& name, bool find_normal) const {
  if (name.empty()) {
    return nullptr;
  }
  for (auto& chunk : chunks_) {
    if (chunk.GetType() != CHUNK_DEFLATE && !find_normal) {
      continue;
    }

    if (chunk.GetEntryName() == name) {
      return &chunk;
    }

    // Edge case when target chunk is split due to size limit but source chunk isn't.
    if (name == (chunk.GetEntryName() + "-0") || chunk.GetEntryName() == (name + "-0")) {
      return &chunk;
    }

    // TODO handle the .so files with incremental version number.
    // (e.g. lib/arm64-v8a/libcronet.59.0.3050.4.so)
  }

  return nullptr;
}

ImageChunk* ZipModeImage::FindChunkByName(const std::string& name, bool find_normal) {
  return const_cast<ImageChunk*>(
      static_cast<const ZipModeImage*>(this)->FindChunkByName(name, find_normal));
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
      LOG(WARNING) << "Failed to reconstruct target deflate chunk [" << tgt_chunk.GetEntryName()
                   << "]; treating as normal";

      tgt_chunk.ChangeDeflateChunkToNormal();
      src_chunk->ChangeDeflateChunkToNormal();
    }
  }

  // For zips, we only need merge normal chunks for the target:  deflated chunks are matched via
  // filename, and normal chunks are patched using the entire source file as the source.
  if (tgt_image->limit_ == 0) {
    tgt_image->MergeAdjacentNormalChunks();
    tgt_image->DumpChunks();
  }

  return true;
}

// For each target chunk, look for the corresponding source chunk by the zip_entry name. If
// found, add the range of this chunk in the original source file to the block aligned source
// ranges. Construct the split src & tgt image once the size of source range reaches limit.
bool ZipModeImage::SplitZipModeImageWithLimit(const ZipModeImage& tgt_image,
                                              const ZipModeImage& src_image,
                                              std::vector<ZipModeImage>* split_tgt_images,
                                              std::vector<ZipModeImage>* split_src_images,
                                              std::vector<SortedRangeSet>* split_src_ranges) {
  CHECK_EQ(tgt_image.limit_, src_image.limit_);
  size_t limit = tgt_image.limit_;

  src_image.DumpChunks();
  LOG(INFO) << "Splitting " << tgt_image.NumOfChunks() << " tgt chunks...";

  SortedRangeSet used_src_ranges;  // ranges used for previous split source images.

  // Reserve the central directory in advance for the last split image.
  const auto& central_directory = src_image.cend() - 1;
  CHECK_EQ(CHUNK_NORMAL, central_directory->GetType());
  used_src_ranges.Insert(central_directory->GetStartOffset(),
                         central_directory->DataLengthForPatch());

  SortedRangeSet src_ranges;
  std::vector<ImageChunk> split_src_chunks;
  std::vector<ImageChunk> split_tgt_chunks;
  for (auto tgt = tgt_image.cbegin(); tgt != tgt_image.cend(); tgt++) {
    const ImageChunk* src = src_image.FindChunkByName(tgt->GetEntryName(), true);
    if (src == nullptr) {
      split_tgt_chunks.emplace_back(CHUNK_NORMAL, tgt->GetStartOffset(), &tgt_image.file_content_,
                                    tgt->GetRawDataLength());
      continue;
    }

    size_t src_offset = src->GetStartOffset();
    size_t src_length = src->GetRawDataLength();

    CHECK(src_length > 0);
    CHECK_LE(src_length, limit);

    // Make sure this source range hasn't been used before so that the src_range pieces don't
    // overlap with each other.
    if (!RemoveUsedBlocks(&src_offset, &src_length, used_src_ranges)) {
      split_tgt_chunks.emplace_back(CHUNK_NORMAL, tgt->GetStartOffset(), &tgt_image.file_content_,
                                    tgt->GetRawDataLength());
    } else if (src_ranges.blocks() * BLOCK_SIZE + src_length <= limit) {
      src_ranges.Insert(src_offset, src_length);

      // Add the deflate source chunk if it hasn't been aligned.
      if (src->GetType() == CHUNK_DEFLATE && src_length == src->GetRawDataLength()) {
        split_src_chunks.push_back(*src);
        split_tgt_chunks.push_back(*tgt);
      } else {
        // TODO split smarter to avoid alignment of large deflate chunks
        split_tgt_chunks.emplace_back(CHUNK_NORMAL, tgt->GetStartOffset(), &tgt_image.file_content_,
                                      tgt->GetRawDataLength());
      }
    } else {
      bool added_image = ZipModeImage::AddSplitImageFromChunkList(
          tgt_image, src_image, src_ranges, split_tgt_chunks, split_src_chunks, split_tgt_images,
          split_src_images);

      split_tgt_chunks.clear();
      split_src_chunks.clear();
      // No need to update the split_src_ranges if we don't update the split source images.
      if (added_image) {
        used_src_ranges.Insert(src_ranges);
        split_src_ranges->push_back(std::move(src_ranges));
      }
      src_ranges.Clear();

      // We don't have enough space for the current chunk; start a new split image and handle
      // this chunk there.
      tgt--;
    }
  }

  // TODO Trim it in case the CD exceeds limit too much.
  src_ranges.Insert(central_directory->GetStartOffset(), central_directory->DataLengthForPatch());
  bool added_image = ZipModeImage::AddSplitImageFromChunkList(tgt_image, src_image, src_ranges,
                                                              split_tgt_chunks, split_src_chunks,
                                                              split_tgt_images, split_src_images);
  if (added_image) {
    split_src_ranges->push_back(std::move(src_ranges));
  }

  ValidateSplitImages(*split_tgt_images, *split_src_images, *split_src_ranges,
                      tgt_image.file_content_.size());

  return true;
}

bool ZipModeImage::AddSplitImageFromChunkList(const ZipModeImage& tgt_image,
                                              const ZipModeImage& src_image,
                                              const SortedRangeSet& split_src_ranges,
                                              const std::vector<ImageChunk>& split_tgt_chunks,
                                              const std::vector<ImageChunk>& split_src_chunks,
                                              std::vector<ZipModeImage>* split_tgt_images,
                                              std::vector<ZipModeImage>* split_src_images) {
  CHECK(!split_tgt_chunks.empty());

  std::vector<ImageChunk> aligned_tgt_chunks;

  // Align the target chunks in the beginning with BLOCK_SIZE.
  size_t i = 0;
  while (i < split_tgt_chunks.size()) {
    size_t tgt_start = split_tgt_chunks[i].GetStartOffset();
    size_t tgt_length = split_tgt_chunks[i].GetRawDataLength();

    // Current ImageChunk is long enough to align.
    if (AlignHead(&tgt_start, &tgt_length)) {
      aligned_tgt_chunks.emplace_back(CHUNK_NORMAL, tgt_start, &tgt_image.file_content_,
                                      tgt_length);
      break;
    }

    i++;
  }

  // Nothing left after alignment in the current split tgt chunks; skip adding the split_tgt_image.
  if (i == split_tgt_chunks.size()) {
    return false;
  }

  aligned_tgt_chunks.insert(aligned_tgt_chunks.end(), split_tgt_chunks.begin() + i + 1,
                            split_tgt_chunks.end());
  CHECK(!aligned_tgt_chunks.empty());

  // Add a normal chunk to align the contents in the end.
  size_t end_offset =
      aligned_tgt_chunks.back().GetStartOffset() + aligned_tgt_chunks.back().GetRawDataLength();
  if (end_offset % BLOCK_SIZE != 0 && end_offset < tgt_image.file_content_.size()) {
    size_t tail_block_length = std::min<size_t>(tgt_image.file_content_.size() - end_offset,
                                                BLOCK_SIZE - (end_offset % BLOCK_SIZE));
    aligned_tgt_chunks.emplace_back(CHUNK_NORMAL, end_offset, &tgt_image.file_content_,
                                    tail_block_length);
  }

  ZipModeImage split_tgt_image(false);
  split_tgt_image.Initialize(std::move(aligned_tgt_chunks), {});
  split_tgt_image.MergeAdjacentNormalChunks();

  // Construct the dummy source file based on the src_ranges.
  std::vector<uint8_t> src_content;
  for (const auto& r : split_src_ranges) {
    size_t end = std::min(src_image.file_content_.size(), r.second * BLOCK_SIZE);
    src_content.insert(src_content.end(), src_image.file_content_.begin() + r.first * BLOCK_SIZE,
                       src_image.file_content_.begin() + end);
  }

  // We should not have an empty src in our design; otherwise we will encounter an error in
  // bsdiff since src_content.data() == nullptr.
  CHECK(!src_content.empty());

  ZipModeImage split_src_image(true);
  split_src_image.Initialize(split_src_chunks, std::move(src_content));

  split_tgt_images->push_back(std::move(split_tgt_image));
  split_src_images->push_back(std::move(split_src_image));

  return true;
}

void ZipModeImage::ValidateSplitImages(const std::vector<ZipModeImage>& split_tgt_images,
                                       const std::vector<ZipModeImage>& split_src_images,
                                       std::vector<SortedRangeSet>& split_src_ranges,
                                       size_t total_tgt_size) {
  CHECK_EQ(split_tgt_images.size(), split_src_images.size());

  LOG(INFO) << "Validating " << split_tgt_images.size() << " images";

  // Verify that the target image pieces is continuous and can add up to the total size.
  size_t last_offset = 0;
  for (const auto& tgt_image : split_tgt_images) {
    CHECK(!tgt_image.chunks_.empty());

    CHECK_EQ(last_offset, tgt_image.chunks_.front().GetStartOffset());
    CHECK(last_offset % BLOCK_SIZE == 0);

    // Check the target chunks within the split image are continuous.
    for (const auto& chunk : tgt_image.chunks_) {
      CHECK_EQ(last_offset, chunk.GetStartOffset());
      last_offset += chunk.GetRawDataLength();
    }
  }
  CHECK_EQ(total_tgt_size, last_offset);

  // Verify that the source ranges are mutually exclusive.
  CHECK_EQ(split_src_images.size(), split_src_ranges.size());
  SortedRangeSet used_src_ranges;
  for (size_t i = 0; i < split_src_ranges.size(); i++) {
    CHECK(!used_src_ranges.Overlaps(split_src_ranges[i]))
        << "src range " << split_src_ranges[i].ToString() << " overlaps "
        << used_src_ranges.ToString();
    used_src_ranges.Insert(split_src_ranges[i]);
  }
}

bool ZipModeImage::GeneratePatchesInternal(const ZipModeImage& tgt_image,
                                           const ZipModeImage& src_image,
                                           std::vector<PatchChunk>* patch_chunks) {
  LOG(INFO) << "Constructing patches for " << tgt_image.NumOfChunks() << " chunks...";
  patch_chunks->clear();

  bsdiff::SuffixArrayIndexInterface* bsdiff_cache = nullptr;
  for (size_t i = 0; i < tgt_image.NumOfChunks(); i++) {
    const auto& tgt_chunk = tgt_image[i];

    if (PatchChunk::RawDataIsSmaller(tgt_chunk, 0)) {
      patch_chunks->emplace_back(tgt_chunk);
      continue;
    }

    const ImageChunk* src_chunk = (tgt_chunk.GetType() != CHUNK_DEFLATE)
                                      ? nullptr
                                      : src_image.FindChunkByName(tgt_chunk.GetEntryName());

    const auto& src_ref = (src_chunk == nullptr) ? src_image.PseudoSource() : *src_chunk;
    bsdiff::SuffixArrayIndexInterface** bsdiff_cache_ptr =
        (src_chunk == nullptr) ? &bsdiff_cache : nullptr;

    std::vector<uint8_t> patch_data;
    if (!ImageChunk::MakePatch(tgt_chunk, src_ref, &patch_data, bsdiff_cache_ptr)) {
      LOG(ERROR) << "Failed to generate patch, name: " << tgt_chunk.GetEntryName();
      return false;
    }

    LOG(INFO) << "patch " << i << " is " << patch_data.size() << " bytes (of "
              << tgt_chunk.GetRawDataLength() << ")";

    if (PatchChunk::RawDataIsSmaller(tgt_chunk, patch_data.size())) {
      patch_chunks->emplace_back(tgt_chunk);
    } else {
      patch_chunks->emplace_back(tgt_chunk, src_ref, std::move(patch_data));
    }
  }
  delete bsdiff_cache;

  CHECK_EQ(patch_chunks->size(), tgt_image.NumOfChunks());
  return true;
}

bool ZipModeImage::GeneratePatches(const ZipModeImage& tgt_image, const ZipModeImage& src_image,
                                   const std::string& patch_name) {
  std::vector<PatchChunk> patch_chunks;

  ZipModeImage::GeneratePatchesInternal(tgt_image, src_image, &patch_chunks);

  CHECK_EQ(tgt_image.NumOfChunks(), patch_chunks.size());

  android::base::unique_fd patch_fd(
      open(patch_name.c_str(), O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR));
  if (patch_fd == -1) {
    PLOG(ERROR) << "Failed to open " << patch_name;
    return false;
  }

  return PatchChunk::WritePatchDataToFd(patch_chunks, patch_fd);
}

bool ZipModeImage::GeneratePatches(const std::vector<ZipModeImage>& split_tgt_images,
                                   const std::vector<ZipModeImage>& split_src_images,
                                   const std::vector<SortedRangeSet>& split_src_ranges,
                                   const std::string& patch_name,
                                   const std::string& split_info_file,
                                   const std::string& debug_dir) {
  LOG(INFO) << "Constructing patches for " << split_tgt_images.size() << " split images...";

  android::base::unique_fd patch_fd(
      open(patch_name.c_str(), O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR));
  if (patch_fd == -1) {
    PLOG(ERROR) << "Failed to open " << patch_name;
    return false;
  }

  std::vector<std::string> split_info_list;
  for (size_t i = 0; i < split_tgt_images.size(); i++) {
    std::vector<PatchChunk> patch_chunks;
    if (!ZipModeImage::GeneratePatchesInternal(split_tgt_images[i], split_src_images[i],
                                               &patch_chunks)) {
      LOG(ERROR) << "Failed to generate split patch";
      return false;
    }

    size_t total_patch_size = 12;
    for (auto& p : patch_chunks) {
      p.UpdateSourceOffset(split_src_ranges[i]);
      total_patch_size += p.PatchSize();
    }

    if (!PatchChunk::WritePatchDataToFd(patch_chunks, patch_fd)) {
      return false;
    }

    size_t split_tgt_size = split_tgt_images[i].chunks_.back().GetStartOffset() +
                            split_tgt_images[i].chunks_.back().GetRawDataLength() -
                            split_tgt_images[i].chunks_.front().GetStartOffset();
    std::string split_info = android::base::StringPrintf(
        "%zu %zu %s", total_patch_size, split_tgt_size, split_src_ranges[i].ToString().c_str());
    split_info_list.push_back(split_info);

    // Write the split source & patch into the debug directory.
    if (!debug_dir.empty()) {
      std::string src_name = android::base::StringPrintf("%s/src-%zu", debug_dir.c_str(), i);
      android::base::unique_fd fd(
          open(src_name.c_str(), O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR));

      if (fd == -1) {
        PLOG(ERROR) << "Failed to open " << src_name;
        return false;
      }
      if (!android::base::WriteFully(fd, split_src_images[i].PseudoSource().DataForPatch(),
                                     split_src_images[i].PseudoSource().DataLengthForPatch())) {
        PLOG(ERROR) << "Failed to write split source data into " << src_name;
        return false;
      }

      std::string patch_name = android::base::StringPrintf("%s/patch-%zu", debug_dir.c_str(), i);
      fd.reset(open(patch_name.c_str(), O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR));

      if (fd == -1) {
        PLOG(ERROR) << "Failed to open " << patch_name;
        return false;
      }
      if (!PatchChunk::WritePatchDataToFd(patch_chunks, fd)) {
        return false;
      }
    }
  }

  // Store the split in the following format:
  // Line 0:   imgdiff version#
  // Line 1:   number of pieces
  // Line 2:   patch_size_1 tgt_size_1 src_range_1
  // ...
  // Line n+1: patch_size_n tgt_size_n src_range_n
  std::string split_info_string = android::base::StringPrintf(
      "%zu\n%zu\n", VERSION, split_info_list.size()) + android::base::Join(split_info_list, '\n');
  if (!android::base::WriteStringToFile(split_info_string, split_info_file)) {
    PLOG(ERROR) << "Failed to write split info to " << split_info_file;
    return false;
  }

  return true;
}

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
        LOG(ERROR) << "Failed to initialize inflate: " << ret;
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
          LOG(WARNING) << "Inflate failed [" << strm.msg << "] at offset [" << chunk_offset
                       << "]; treating as a normal chunk";
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
        LOG(WARNING) << "invalid footer position; treating as a normal chunk";
        continue;
      }
      size_t footer_size = get_unaligned<uint32_t>(file_content_.data() + footer_index);
      if (footer_size != uncompressed_len) {
        LOG(WARNING) << "footer size " << footer_size << " != " << uncompressed_len
                     << "; treating as a normal chunk";
        continue;
      }

      ImageChunk body(CHUNK_DEFLATE, pos, &file_content_, raw_data_len);
      uncompressed_data.resize(uncompressed_len);
      body.SetUncompressedData(std::move(uncompressed_data));
      chunks_.push_back(std::move(body));

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

bool ImageModeImage::SetBonusData(const std::vector<uint8_t>& bonus_data) {
  CHECK(is_source_);
  if (chunks_.size() < 2 || !chunks_[1].SetBonusData(bonus_data)) {
    LOG(ERROR) << "Failed to set bonus data";
    DumpChunks();
    return false;
  }

  LOG(INFO) << "  using " << bonus_data.size() << " bytes of bonus data";
  return true;
}

// In Image Mode, verify that the source and target images have the same chunk structure (ie, the
// same sequence of deflate and normal chunks).
bool ImageModeImage::CheckAndProcessChunks(ImageModeImage* tgt_image, ImageModeImage* src_image) {
  // In image mode, merge the gzip header and footer in with any adjacent normal chunks.
  tgt_image->MergeAdjacentNormalChunks();
  src_image->MergeAdjacentNormalChunks();

  if (tgt_image->NumOfChunks() != src_image->NumOfChunks()) {
    LOG(ERROR) << "Source and target don't have same number of chunks!";
    tgt_image->DumpChunks();
    src_image->DumpChunks();
    return false;
  }
  for (size_t i = 0; i < tgt_image->NumOfChunks(); ++i) {
    if ((*tgt_image)[i].GetType() != (*src_image)[i].GetType()) {
      LOG(ERROR) << "Source and target don't have same chunk structure! (chunk " << i << ")";
      tgt_image->DumpChunks();
      src_image->DumpChunks();
      return false;
    }
  }

  for (size_t i = 0; i < tgt_image->NumOfChunks(); ++i) {
    auto& tgt_chunk = (*tgt_image)[i];
    auto& src_chunk = (*src_image)[i];
    if (tgt_chunk.GetType() != CHUNK_DEFLATE) {
      continue;
    }

    // If two deflate chunks are identical treat them as normal chunks.
    if (tgt_chunk == src_chunk) {
      tgt_chunk.ChangeDeflateChunkToNormal();
      src_chunk.ChangeDeflateChunkToNormal();
    } else if (!tgt_chunk.ReconstructDeflateChunk()) {
      // We cannot recompress the data and get exactly the same bits as are in the input target
      // image, fall back to normal
      LOG(WARNING) << "Failed to reconstruct target deflate chunk " << i << " ["
                   << tgt_chunk.GetEntryName() << "]; treating as normal";
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
    LOG(ERROR) << "Merging normal chunks went awry";
    return false;
  }

  return true;
}

// In image mode, generate patches against the given source chunks and bonus_data; write the
// result to |patch_name|.
bool ImageModeImage::GeneratePatches(const ImageModeImage& tgt_image,
                                     const ImageModeImage& src_image,
                                     const std::string& patch_name) {
  LOG(INFO) << "Constructing patches for " << tgt_image.NumOfChunks() << " chunks...";
  std::vector<PatchChunk> patch_chunks;
  patch_chunks.reserve(tgt_image.NumOfChunks());

  for (size_t i = 0; i < tgt_image.NumOfChunks(); i++) {
    const auto& tgt_chunk = tgt_image[i];
    const auto& src_chunk = src_image[i];

    if (PatchChunk::RawDataIsSmaller(tgt_chunk, 0)) {
      patch_chunks.emplace_back(tgt_chunk);
      continue;
    }

    std::vector<uint8_t> patch_data;
    if (!ImageChunk::MakePatch(tgt_chunk, src_chunk, &patch_data, nullptr)) {
      LOG(ERROR) << "Failed to generate patch for target chunk " << i;
      return false;
    }
    LOG(INFO) << "patch " << i << " is " << patch_data.size() << " bytes (of "
              << tgt_chunk.GetRawDataLength() << ")";

    if (PatchChunk::RawDataIsSmaller(tgt_chunk, patch_data.size())) {
      patch_chunks.emplace_back(tgt_chunk);
    } else {
      patch_chunks.emplace_back(tgt_chunk, src_chunk, std::move(patch_data));
    }
  }

  CHECK_EQ(tgt_image.NumOfChunks(), patch_chunks.size());

  android::base::unique_fd patch_fd(
      open(patch_name.c_str(), O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR));
  if (patch_fd == -1) {
    PLOG(ERROR) << "Failed to open " << patch_name;
    return false;
  }

  return PatchChunk::WritePatchDataToFd(patch_chunks, patch_fd);
}

int imgdiff(int argc, const char** argv) {
  bool verbose = false;
  bool zip_mode = false;
  std::vector<uint8_t> bonus_data;
  size_t blocks_limit = 0;
  std::string split_info_file;
  std::string debug_dir;

  int opt;
  int option_index;
  optind = 0;  // Reset the getopt state so that we can call it multiple times for test.

  while ((opt = getopt_long(argc, const_cast<char**>(argv), "zb:v", OPTIONS, &option_index)) !=
         -1) {
    switch (opt) {
      case 'z':
        zip_mode = true;
        break;
      case 'b': {
        android::base::unique_fd fd(open(optarg, O_RDONLY));
        if (fd == -1) {
          PLOG(ERROR) << "Failed to open bonus file " << optarg;
          return 1;
        }
        struct stat st;
        if (fstat(fd, &st) != 0) {
          PLOG(ERROR) << "Failed to stat bonus file " << optarg;
          return 1;
        }

        size_t bonus_size = st.st_size;
        bonus_data.resize(bonus_size);
        if (!android::base::ReadFully(fd, bonus_data.data(), bonus_size)) {
          PLOG(ERROR) << "Failed to read bonus file " << optarg;
          return 1;
        }
        break;
      }
      case 'v':
        verbose = true;
        break;
      case 0: {
        std::string name = OPTIONS[option_index].name;
        if (name == "block-limit" && !android::base::ParseUint(optarg, &blocks_limit)) {
          LOG(ERROR) << "Failed to parse size blocks_limit: " << optarg;
          return 1;
        } else if (name == "split-info") {
          split_info_file = optarg;
        } else if (name == "debug-dir") {
          debug_dir = optarg;
        }
        break;
      }
      default:
        LOG(ERROR) << "unexpected opt: " << static_cast<char>(opt);
        return 2;
    }
  }

  if (!verbose) {
    android::base::SetMinimumLogSeverity(android::base::WARNING);
  }

  if (argc - optind != 3) {
    LOG(ERROR) << "usage: " << argv[0] << " [options] <src-img> <tgt-img> <patch-file>";
    LOG(ERROR)
        << "  -z <zip-mode>,    Generate patches in zip mode, src and tgt should be zip files.\n"
           "  -b <bonus-file>,  Bonus file in addition to src, image mode only.\n"
           "  --block-limit,    For large zips, split the src and tgt based on the block limit;\n"
           "                    and generate patches between each pair of pieces. Concatenate "
           "these\n"
           "                    patches together and output them into <patch-file>.\n"
           "  --split-info,     Output the split information (patch_size, tgt_size, src_ranges);\n"
           "                    zip mode with block-limit only.\n"
           "  --debug-dir,      Debug directory to put the split srcs and patches, zip mode only.\n"
           "  -v, --verbose,    Enable verbose logging.";
    return 2;
  }

  if (zip_mode) {
    ZipModeImage src_image(true, blocks_limit * BLOCK_SIZE);
    ZipModeImage tgt_image(false, blocks_limit * BLOCK_SIZE);

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
    if (blocks_limit > 0) {
      if (split_info_file.empty()) {
        LOG(ERROR) << "split-info path cannot be empty when generating patches with a block-limit";
        return 1;
      }

      std::vector<ZipModeImage> split_tgt_images;
      std::vector<ZipModeImage> split_src_images;
      std::vector<SortedRangeSet> split_src_ranges;
      ZipModeImage::SplitZipModeImageWithLimit(tgt_image, src_image, &split_tgt_images,
                                               &split_src_images, &split_src_ranges);

      if (!ZipModeImage::GeneratePatches(split_tgt_images, split_src_images, split_src_ranges,
                                         argv[optind + 2], split_info_file, debug_dir)) {
        return 1;
      }

    } else if (!ZipModeImage::GeneratePatches(tgt_image, src_image, argv[optind + 2])) {
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

    if (!bonus_data.empty() && !src_image.SetBonusData(bonus_data)) {
      return 1;
    }

    if (!ImageModeImage::GeneratePatches(tgt_image, src_image, argv[optind + 2])) {
      return 1;
    }
  }

  return 0;
}
