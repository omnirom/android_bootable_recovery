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

#include <string>

#include <android-base/unique_fd.h>

// This is the base class to read data from source and provide the data to FUSE.
class FuseDataProvider {
 public:
  FuseDataProvider(android::base::unique_fd&& fd, uint64_t file_size, uint32_t block_size)
      : fd_(std::move(fd)), file_size_(file_size), fuse_block_size_(block_size) {}

  virtual ~FuseDataProvider() = default;

  uint64_t file_size() const {
    return file_size_;
  }
  uint32_t fuse_block_size() const {
    return fuse_block_size_;
  }

  bool Valid() const {
    return fd_ != -1;
  }

  // Reads |fetch_size| bytes data starting from |start_block|. Puts the result in |buffer|.
  virtual bool ReadBlockAlignedData(uint8_t* buffer, uint32_t fetch_size,
                                    uint32_t start_block) const = 0;

  virtual void Close() = 0;

 protected:
  FuseDataProvider() = default;

  // The underlying source to read data from.
  android::base::unique_fd fd_;
  // Size in bytes of the file to read.
  uint64_t file_size_ = 0;
  // Block size passed to the fuse, this is different from the block size of the block device.
  uint32_t fuse_block_size_ = 0;
};

// This class reads data from a file.
class FuseFileDataProvider : public FuseDataProvider {
 public:
  FuseFileDataProvider(android::base::unique_fd&& fd, uint64_t file_size, uint32_t block_size)
      : FuseDataProvider(std::move(fd), file_size, block_size) {}

  FuseFileDataProvider(const std::string& path, uint32_t block_size);

  bool ReadBlockAlignedData(uint8_t* buffer, uint32_t fetch_size,
                            uint32_t start_block) const override;

  void Close() override;
};
