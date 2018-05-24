/*
 * Copyright (C) 2017 The Android Open Source Project
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

#ifndef _APPLYPATCH_IMGDIFF_IMAGE_H
#define _APPLYPATCH_IMGDIFF_IMAGE_H

#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>

#include <string>
#include <vector>

#include <bsdiff/bsdiff.h>
#include <ziparchive/zip_archive.h>
#include <zlib.h>

#include "imgdiff.h"
#include "otautil/rangeset.h"

class ImageChunk {
 public:
  static constexpr auto WINDOWBITS = -15;  // 32kb window; negative to indicate a raw stream.
  static constexpr auto MEMLEVEL = 8;      // the default value.
  static constexpr auto METHOD = Z_DEFLATED;
  static constexpr auto STRATEGY = Z_DEFAULT_STRATEGY;

  ImageChunk(int type, size_t start, const std::vector<uint8_t>* file_content, size_t raw_data_len,
             std::string entry_name = {});

  int GetType() const {
    return type_;
  }

  const uint8_t* GetRawData() const;
  size_t GetRawDataLength() const {
    return raw_data_len_;
  }
  const std::string& GetEntryName() const {
    return entry_name_;
  }
  size_t GetStartOffset() const {
    return start_;
  }
  int GetCompressLevel() const {
    return compress_level_;
  }

  // CHUNK_DEFLATE will return the uncompressed data for diff, while other types will simply return
  // the raw data.
  const uint8_t* DataForPatch() const;
  size_t DataLengthForPatch() const;

  void Dump(size_t index) const;

  void SetUncompressedData(std::vector<uint8_t> data);
  bool SetBonusData(const std::vector<uint8_t>& bonus_data);

  bool operator==(const ImageChunk& other) const;
  bool operator!=(const ImageChunk& other) const {
    return !(*this == other);
  }

  /*
   * Cause a gzip chunk to be treated as a normal chunk (ie, as a blob of uninterpreted data).
   * The resulting patch will likely be about as big as the target file, but it lets us handle
   * the case of images where some gzip chunks are reconstructible but others aren't (by treating
   * the ones that aren't as normal chunks).
   */
  void ChangeDeflateChunkToNormal();

  /*
   * Verify that we can reproduce exactly the same compressed data that we started with.  Sets the
   * level, method, windowBits, memLevel, and strategy fields in the chunk to the encoding
   * parameters needed to produce the right output.
   */
  bool ReconstructDeflateChunk();
  bool IsAdjacentNormal(const ImageChunk& other) const;
  void MergeAdjacentNormal(const ImageChunk& other);

  /*
   * Compute a bsdiff patch between |src| and |tgt|; Store the result in the patch_data.
   * |bsdiff_cache| can be used to cache the suffix array if the same |src| chunk is used
   * repeatedly, pass nullptr if not needed.
   */
  static bool MakePatch(const ImageChunk& tgt, const ImageChunk& src,
                        std::vector<uint8_t>* patch_data,
                        bsdiff::SuffixArrayIndexInterface** bsdiff_cache);

 private:
  bool TryReconstruction(int level);

  int type_;                                    // CHUNK_NORMAL, CHUNK_DEFLATE, CHUNK_RAW
  size_t start_;                                // offset of chunk in the original input file
  const std::vector<uint8_t>* input_file_ptr_;  // ptr to the full content of original input file
  size_t raw_data_len_;

  // deflate encoder parameters
  int compress_level_;

  // --- for CHUNK_DEFLATE chunks only: ---
  std::vector<uint8_t> uncompressed_data_;
  std::string entry_name_;  // used for zip entries
};

// PatchChunk stores the patch data between a source chunk and a target chunk. It also keeps track
// of the metadata of src&tgt chunks (e.g. offset, raw data length, uncompressed data length).
class PatchChunk {
 public:
  PatchChunk(const ImageChunk& tgt, const ImageChunk& src, std::vector<uint8_t> data);

  // Construct a CHUNK_RAW patch from the target data directly.
  explicit PatchChunk(const ImageChunk& tgt);

  // Return true if raw data size is smaller than the patch size.
  static bool RawDataIsSmaller(const ImageChunk& tgt, size_t patch_size);

  // Update the source start with the new offset within the source range.
  void UpdateSourceOffset(const SortedRangeSet& src_range);

  // Return the total size (header + data) of the patch.
  size_t PatchSize() const;

  static bool WritePatchDataToFd(const std::vector<PatchChunk>& patch_chunks, int patch_fd);

 private:
  size_t GetHeaderSize() const;
  size_t WriteHeaderToFd(int fd, size_t offset, size_t index) const;

  // The patch chunk type is the same as the target chunk type. The only exception is we change
  // the |type_| to CHUNK_RAW if target length is smaller than the patch size.
  int type_;

  size_t source_start_;
  size_t source_len_;
  size_t source_uncompressed_len_;

  size_t target_start_;  // offset of the target chunk within the target file
  size_t target_len_;
  size_t target_uncompressed_len_;
  size_t target_compress_level_;  // the deflate compression level of the target chunk.

  std::vector<uint8_t> data_;  // storage for the patch data
};

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

  void DumpChunks() const;

  // Non const iterators to access the stored ImageChunks.
  std::vector<ImageChunk>::iterator begin() {
    return chunks_.begin();
  }

  std::vector<ImageChunk>::iterator end() {
    return chunks_.end();
  }

  std::vector<ImageChunk>::const_iterator cbegin() const {
    return chunks_.cbegin();
  }

  std::vector<ImageChunk>::const_iterator cend() const {
    return chunks_.cend();
  }

  ImageChunk& operator[](size_t i);
  const ImageChunk& operator[](size_t i) const;

  size_t NumOfChunks() const {
    return chunks_.size();
  }

 protected:
  bool ReadFile(const std::string& filename, std::vector<uint8_t>* file_content);

  bool is_source_;                     // True if it's for source chunks.
  std::vector<ImageChunk> chunks_;     // Internal storage of ImageChunk.
  std::vector<uint8_t> file_content_;  // Store the whole input file in memory.
};

class ZipModeImage : public Image {
 public:
  explicit ZipModeImage(bool is_source, size_t limit = 0) : Image(is_source), limit_(limit) {}

  bool Initialize(const std::string& filename) override;

  // Initialize a dummy ZipModeImage from an existing ImageChunk vector. For src img pieces, we
  // reconstruct a new file_content based on the source ranges; but it's not needed for the tgt img
  // pieces; because for each chunk both the data and their offset within the file are unchanged.
  void Initialize(const std::vector<ImageChunk>& chunks, const std::vector<uint8_t>& file_content) {
    chunks_ = chunks;
    file_content_ = file_content;
  }

  // The pesudo source chunk for bsdiff if there's no match for the given target chunk. It's in
  // fact the whole source file.
  ImageChunk PseudoSource() const;

  // Find the matching deflate source chunk by entry name. Search for normal chunks also if
  // |find_normal| is true.
  ImageChunk* FindChunkByName(const std::string& name, bool find_normal = false);

  const ImageChunk* FindChunkByName(const std::string& name, bool find_normal = false) const;

  // Verify that we can reconstruct the deflate chunks; also change the type to CHUNK_NORMAL if
  // src and tgt are identical.
  static bool CheckAndProcessChunks(ZipModeImage* tgt_image, ZipModeImage* src_image);

  // Compute the patch between tgt & src images, and write the data into |patch_name|.
  static bool GeneratePatches(const ZipModeImage& tgt_image, const ZipModeImage& src_image,
                              const std::string& patch_name);

  // Compute the patch based on the lists of split src and tgt images. Generate patches for each
  // pair of split pieces and write the data to |patch_name|. If |debug_dir| is specified, write
  // each split src data and patch data into that directory.
  static bool GeneratePatches(const std::vector<ZipModeImage>& split_tgt_images,
                              const std::vector<ZipModeImage>& split_src_images,
                              const std::vector<SortedRangeSet>& split_src_ranges,
                              const std::string& patch_name, const std::string& split_info_file,
                              const std::string& debug_dir);

  // Split the tgt chunks and src chunks based on the size limit.
  static bool SplitZipModeImageWithLimit(const ZipModeImage& tgt_image,
                                         const ZipModeImage& src_image,
                                         std::vector<ZipModeImage>* split_tgt_images,
                                         std::vector<ZipModeImage>* split_src_images,
                                         std::vector<SortedRangeSet>* split_src_ranges);

 private:
  // Initialize image chunks based on the zip entries.
  bool InitializeChunks(const std::string& filename, ZipArchiveHandle handle);
  // Add the a zip entry to the list.
  bool AddZipEntryToChunks(ZipArchiveHandle handle, const std::string& entry_name, ZipEntry* entry);
  // Return the real size of the zip file. (omit the trailing zeros that used for alignment)
  bool GetZipFileSize(size_t* input_file_size);

  static void ValidateSplitImages(const std::vector<ZipModeImage>& split_tgt_images,
                                  const std::vector<ZipModeImage>& split_src_images,
                                  std::vector<SortedRangeSet>& split_src_ranges,
                                  size_t total_tgt_size);
  // Construct the dummy split images based on the chunks info and source ranges; and move them into
  // the given vectors. Return true if we add a new split image into |split_tgt_images|, and
  // false otherwise.
  static bool AddSplitImageFromChunkList(const ZipModeImage& tgt_image,
                                         const ZipModeImage& src_image,
                                         const SortedRangeSet& split_src_ranges,
                                         const std::vector<ImageChunk>& split_tgt_chunks,
                                         const std::vector<ImageChunk>& split_src_chunks,
                                         std::vector<ZipModeImage>* split_tgt_images,
                                         std::vector<ZipModeImage>* split_src_images);

  // Function that actually iterates the tgt_chunks and makes patches.
  static bool GeneratePatchesInternal(const ZipModeImage& tgt_image, const ZipModeImage& src_image,
                                      std::vector<PatchChunk>* patch_chunks);

  // size limit in bytes of each chunk. Also, if the length of one zip_entry exceeds the limit,
  // we'll split that entry into several smaller chunks in advance.
  size_t limit_;
};

class ImageModeImage : public Image {
 public:
  explicit ImageModeImage(bool is_source) : Image(is_source) {}

  // Initialize the image chunks list by searching the magic numbers in an image file.
  bool Initialize(const std::string& filename) override;

  bool SetBonusData(const std::vector<uint8_t>& bonus_data);

  // In Image Mode, verify that the source and target images have the same chunk structure (ie, the
  // same sequence of deflate and normal chunks).
  static bool CheckAndProcessChunks(ImageModeImage* tgt_image, ImageModeImage* src_image);

  // In image mode, generate patches against the given source chunks and bonus_data; write the
  // result to |patch_name|.
  static bool GeneratePatches(const ImageModeImage& tgt_image, const ImageModeImage& src_image,
                              const std::string& patch_name);
};

#endif  // _APPLYPATCH_IMGDIFF_IMAGE_H
