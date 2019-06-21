/*
 * Copyright (C) 2019 The Android Open Source Project
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

#pragma once

#include <stdint.h>

#include <memory>
#include <string>

#include <android-base/unique_fd.h>

#include "otautil/rangeset.h"

// This is the base class to read data from source and provide the data to FUSE.
class FuseDataProvider {
 public:
  FuseDataProvider(uint64_t file_size, uint32_t block_size)
      : file_size_(file_size), fuse_block_size_(block_size) {}

  virtual ~FuseDataProvider() = default;

  uint64_t file_size() const {
    return file_size_;
  }
  uint32_t fuse_block_size() const {
    return fuse_block_size_;
  }

  // Reads |fetch_size| bytes data starting from |start_block|. Puts the result in |buffer|.
  virtual bool ReadBlockAlignedData(uint8_t* buffer, uint32_t fetch_size,
                                    uint32_t start_block) const = 0;

  virtual bool Valid() const = 0;

  virtual void Close() {}

 protected:
  FuseDataProvider() = default;

  // Size in bytes of the file to read.
  uint64_t file_size_ = 0;
  // Block size passed to the fuse, this is different from the block size of the block device.
  uint32_t fuse_block_size_ = 0;
};

// This class reads data from a file.
class FuseFileDataProvider : public FuseDataProvider {
 public:
  FuseFileDataProvider(const std::string& path, uint32_t block_size);

  static std::unique_ptr<FuseDataProvider> CreateFromFile(const std::string& path,
                                                          uint32_t block_size);

  bool ReadBlockAlignedData(uint8_t* buffer, uint32_t fetch_size,
                            uint32_t start_block) const override;

  bool Valid() const override {
    return fd_ != -1;
  }

  void Close() override;

 private:
  // The underlying source to read data from.
  android::base::unique_fd fd_;
};

// This class parses a block map and reads data from the underlying block device.
class FuseBlockDataProvider : public FuseDataProvider {
 public:
  // Constructs the fuse provider from the block map.
  static std::unique_ptr<FuseDataProvider> CreateFromBlockMap(const std::string& block_map_path,
                                                              uint32_t fuse_block_size);

  RangeSet ranges() const {
    return ranges_;
  }

  bool ReadBlockAlignedData(uint8_t* buffer, uint32_t fetch_size,
                            uint32_t start_block) const override;

  bool Valid() const override {
    return fd_ != -1;
  }

  void Close() override;

 private:
  FuseBlockDataProvider(uint64_t file_size, uint32_t fuse_block_size, android::base::unique_fd&& fd,
                        uint32_t source_block_size, RangeSet ranges);
  // The underlying block device to read data from.
  android::base::unique_fd fd_;
  // The block size of the source block device.
  uint32_t source_block_size_;
  // The block ranges from the source block device that consist of the file
  RangeSet ranges_;
};
