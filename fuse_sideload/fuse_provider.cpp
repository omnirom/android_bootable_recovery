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

#include "fuse_provider.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <functional>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>

#include "fuse_sideload.h"
#include "otautil/sysutil.h"

FuseFileDataProvider::FuseFileDataProvider(const std::string& path, uint32_t block_size) {
  struct stat sb;
  if (stat(path.c_str(), &sb) == -1) {
    fprintf(stderr, "failed to stat %s: %s\n", path.c_str(), strerror(errno));
    return;
  }

  fd_.reset(open(path.c_str(), O_RDONLY));
  if (fd_ == -1) {
    fprintf(stderr, "failed to open %s: %s\n", path.c_str(), strerror(errno));
    return;
  }
  file_size_ = sb.st_size;
  fuse_block_size_ = block_size;
}

std::unique_ptr<FuseDataProvider> FuseFileDataProvider::CreateFromFile(const std::string& path,
                                                                       uint32_t block_size) {
  return std::make_unique<FuseFileDataProvider>(path, block_size);
}

bool FuseFileDataProvider::ReadBlockAlignedData(uint8_t* buffer, uint32_t fetch_size,
                                                uint32_t start_block) const {
  uint64_t offset = static_cast<uint64_t>(start_block) * fuse_block_size_;
  if (fetch_size > file_size_ || offset > file_size_ - fetch_size) {
    fprintf(stderr,
            "Out of bound read, start block: %" PRIu32 ", fetch size: %" PRIu32
            ", file size %" PRIu64 "\n",
            start_block, fetch_size, file_size_);
    return false;
  }

  if (!android::base::ReadFullyAtOffset(fd_, buffer, fetch_size, offset)) {
    fprintf(stderr, "Failed to read fetch size: %" PRIu32 " bytes data at offset %" PRIu64 ": %s\n",
            fetch_size, offset, strerror(errno));
    return false;
  }

  return true;
}

void FuseFileDataProvider::Close() {
  fd_.reset();
}

FuseBlockDataProvider::FuseBlockDataProvider(uint64_t file_size, uint32_t fuse_block_size,
                                             android::base::unique_fd&& fd,
                                             uint32_t source_block_size, RangeSet ranges)
    : FuseDataProvider(file_size, fuse_block_size),
      fd_(std::move(fd)),
      source_block_size_(source_block_size),
      ranges_(std::move(ranges)) {
  // Make sure the offset is also aligned with the blocks on the block device when we call
  // ReadBlockAlignedData().
  CHECK_EQ(0, fuse_block_size_ % source_block_size_);
}

bool FuseBlockDataProvider::ReadBlockAlignedData(uint8_t* buffer, uint32_t fetch_size,
                                                 uint32_t start_block) const {
  uint64_t offset = static_cast<uint64_t>(start_block) * fuse_block_size_;
  if (fetch_size > file_size_ || offset > file_size_ - fetch_size) {
    LOG(ERROR) << "Out of bound read, offset: " << offset << ", fetch size: " << fetch_size
               << ", file size " << file_size_;
    return false;
  }

  auto read_ranges =
      ranges_.GetSubRanges(offset / source_block_size_, fetch_size / source_block_size_);
  if (!read_ranges) {
    return false;
  }

  uint8_t* next_out = buffer;
  for (const auto& [range_start, range_end] : read_ranges.value()) {
    uint64_t bytes_start = static_cast<uint64_t>(range_start) * source_block_size_;
    uint64_t bytes_to_read = static_cast<uint64_t>(range_end - range_start) * source_block_size_;
    if (!android::base::ReadFullyAtOffset(fd_, next_out, bytes_to_read, bytes_start)) {
      PLOG(ERROR) << "Failed to read " << bytes_to_read << " bytes at offset " << bytes_start;
      return false;
    }

    next_out += bytes_to_read;
  }

  if (uint64_t tailing_bytes = fetch_size % source_block_size_; tailing_bytes != 0) {
    // Calculate the offset to last partial block.
    uint64_t tailing_offset =
        read_ranges.value()
            ? static_cast<uint64_t>((read_ranges->cend() - 1)->second) * source_block_size_
            : static_cast<uint64_t>(start_block) * source_block_size_;
    if (!android::base::ReadFullyAtOffset(fd_, next_out, tailing_bytes, tailing_offset)) {
      PLOG(ERROR) << "Failed to read tailing " << tailing_bytes << " bytes at offset "
                  << tailing_offset;
      return false;
    }
  }
  return true;
}

std::unique_ptr<FuseDataProvider> FuseBlockDataProvider::CreateFromBlockMap(
    const std::string& block_map_path, uint32_t fuse_block_size) {
  auto block_map = BlockMapData::ParseBlockMapFile(block_map_path);
  if (!block_map) {
    return nullptr;
  }

  android::base::unique_fd fd(TEMP_FAILURE_RETRY(open(block_map.path().c_str(), O_RDONLY)));
  if (fd == -1) {
    PLOG(ERROR) << "Failed to open " << block_map.path();
    return nullptr;
  }

  return std::unique_ptr<FuseBlockDataProvider>(
      new FuseBlockDataProvider(block_map.file_size(), fuse_block_size, std::move(fd),
                                block_map.block_size(), block_map.block_ranges()));
}

void FuseBlockDataProvider::Close() {
  fd_.reset();
}
