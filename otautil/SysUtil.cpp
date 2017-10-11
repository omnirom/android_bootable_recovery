/*
 * Copyright 2006 The Android Open Source Project
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

#include "otautil/SysUtil.h"

#include <errno.h>  // TEMP_FAILURE_RETRY
#include <fcntl.h>
#include <stdint.h>  // SIZE_MAX
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>
#include <android-base/unique_fd.h>

bool MemMapping::MapFD(int fd) {
  struct stat sb;
  if (fstat(fd, &sb) == -1) {
    PLOG(ERROR) << "fstat(" << fd << ") failed";
    return false;
  }

  void* memPtr = mmap(nullptr, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (memPtr == MAP_FAILED) {
    PLOG(ERROR) << "mmap(" << sb.st_size << ", R, PRIVATE, " << fd << ", 0) failed";
    return false;
  }

  addr = static_cast<unsigned char*>(memPtr);
  length = sb.st_size;
  ranges_.clear();
  ranges_.emplace_back(MappedRange{ memPtr, static_cast<size_t>(sb.st_size) });

  return true;
}

// A "block map" which looks like this (from uncrypt/uncrypt.cpp):
//
//   /dev/block/platform/msm_sdcc.1/by-name/userdata     # block device
//   49652 4096                                          # file size in bytes, block size
//   3                                                   # count of block ranges
//   1000 1008                                           # block range 0
//   2100 2102                                           # ... block range 1
//   30 33                                               # ... block range 2
//
// Each block range represents a half-open interval; the line "30 33" reprents the blocks
// [30, 31, 32].
bool MemMapping::MapBlockFile(const std::string& filename) {
  std::string content;
  if (!android::base::ReadFileToString(filename, &content)) {
    PLOG(ERROR) << "Failed to read " << filename;
    return false;
  }

  std::vector<std::string> lines = android::base::Split(android::base::Trim(content), "\n");
  if (lines.size() < 4) {
    LOG(ERROR) << "Block map file is too short: " << lines.size();
    return false;
  }

  size_t size;
  size_t blksize;
  if (sscanf(lines[1].c_str(), "%zu %zu", &size, &blksize) != 2) {
    LOG(ERROR) << "Failed to parse file size and block size: " << lines[1];
    return false;
  }

  size_t range_count;
  if (sscanf(lines[2].c_str(), "%zu", &range_count) != 1) {
    LOG(ERROR) << "Failed to parse block map header: " << lines[2];
    return false;
  }

  size_t blocks;
  if (blksize != 0) {
    blocks = ((size - 1) / blksize) + 1;
  }
  if (size == 0 || blksize == 0 || blocks > SIZE_MAX / blksize || range_count == 0 ||
      lines.size() != 3 + range_count) {
    LOG(ERROR) << "Invalid data in block map file: size " << size << ", blksize " << blksize
               << ", range_count " << range_count << ", lines " << lines.size();
    return false;
  }

  // Reserve enough contiguous address space for the whole file.
  void* reserve = mmap(nullptr, blocks * blksize, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
  if (reserve == MAP_FAILED) {
    PLOG(ERROR) << "failed to reserve address space";
    return false;
  }

  const std::string& block_dev = lines[0];
  android::base::unique_fd fd(TEMP_FAILURE_RETRY(open(block_dev.c_str(), O_RDONLY)));
  if (fd == -1) {
    PLOG(ERROR) << "failed to open block device " << block_dev;
    munmap(reserve, blocks * blksize);
    return false;
  }

  ranges_.clear();

  unsigned char* next = static_cast<unsigned char*>(reserve);
  size_t remaining_size = blocks * blksize;
  bool success = true;
  for (size_t i = 0; i < range_count; ++i) {
    const std::string& line = lines[i + 3];

    size_t start, end;
    if (sscanf(line.c_str(), "%zu %zu\n", &start, &end) != 2) {
      LOG(ERROR) << "failed to parse range " << i << ": " << line;
      success = false;
      break;
    }
    size_t range_size = (end - start) * blksize;
    if (end <= start || (end - start) > SIZE_MAX / blksize || range_size > remaining_size) {
      LOG(ERROR) << "Invalid range: " << start << " " << end;
      success = false;
      break;
    }

    void* range_start = mmap(next, range_size, PROT_READ, MAP_PRIVATE | MAP_FIXED, fd,
                             static_cast<off_t>(start) * blksize);
    if (range_start == MAP_FAILED) {
      PLOG(ERROR) << "failed to map range " << i << ": " << line;
      success = false;
      break;
    }
    ranges_.emplace_back(MappedRange{ range_start, range_size });

    next += range_size;
    remaining_size -= range_size;
  }
  if (success && remaining_size != 0) {
    LOG(ERROR) << "Invalid ranges: remaining_size " << remaining_size;
    success = false;
  }
  if (!success) {
    munmap(reserve, blocks * blksize);
    return false;
  }

  addr = static_cast<unsigned char*>(reserve);
  length = size;

  LOG(INFO) << "mmapped " << range_count << " ranges";

  return true;
}

bool MemMapping::MapFile(const std::string& fn) {
  if (fn.empty()) {
    LOG(ERROR) << "Empty filename";
    return false;
  }

  if (fn[0] == '@') {
    // Block map file "@/cache/recovery/block.map".
    if (!MapBlockFile(fn.substr(1))) {
      LOG(ERROR) << "Map of '" << fn << "' failed";
      return false;
    }
  } else {
    // This is a regular file.
    android::base::unique_fd fd(TEMP_FAILURE_RETRY(open(fn.c_str(), O_RDONLY)));
    if (fd == -1) {
      PLOG(ERROR) << "Unable to open '" << fn << "'";
      return false;
    }

    if (!MapFD(fd)) {
      LOG(ERROR) << "Map of '" << fn << "' failed";
      return false;
    }
  }
  return true;
}

MemMapping::~MemMapping() {
  for (const auto& range : ranges_) {
    if (munmap(range.addr, range.length) == -1) {
      PLOG(ERROR) << "Failed to munmap(" << range.addr << ", " << range.length << ")";
    }
  };
  ranges_.clear();
}
