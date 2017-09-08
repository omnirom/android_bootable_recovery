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

#include "SysUtil.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>
#include <android-base/unique_fd.h>

static bool sysMapFD(int fd, MemMapping* pMap) {
  CHECK(pMap != nullptr);

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

  pMap->addr = static_cast<unsigned char*>(memPtr);
  pMap->length = sb.st_size;
  pMap->ranges.push_back({ memPtr, static_cast<size_t>(sb.st_size) });

  return true;
}

// A "block map" which looks like this (from uncrypt/uncrypt.cpp):
//
//     /dev/block/platform/msm_sdcc.1/by-name/userdata     # block device
//     49652 4096                        # file size in bytes, block size
//     3                                 # count of block ranges
//     1000 1008                         # block range 0
//     2100 2102                         # ... block range 1
//     30 33                             # ... block range 2
//
// Each block range represents a half-open interval; the line "30 33"
// reprents the blocks [30, 31, 32].
static int sysMapBlockFile(const char* filename, MemMapping* pMap) {
  CHECK(pMap != nullptr);

  std::string content;
  if (!android::base::ReadFileToString(filename, &content)) {
    PLOG(ERROR) << "Failed to read " << filename;
    return -1;
  }

  std::vector<std::string> lines = android::base::Split(android::base::Trim(content), "\n");
  if (lines.size() < 4) {
    LOG(ERROR) << "Block map file is too short: " << lines.size();
    return -1;
  }

  size_t size;
  unsigned int blksize;
  if (sscanf(lines[1].c_str(), "%zu %u", &size, &blksize) != 2) {
    LOG(ERROR) << "Failed to parse file size and block size: " << lines[1];
    return -1;
  }

  size_t range_count;
  if (sscanf(lines[2].c_str(), "%zu", &range_count) != 1) {
    LOG(ERROR) << "Failed to parse block map header: " << lines[2];
    return -1;
  }

  size_t blocks;
  if (blksize != 0) {
    blocks = ((size - 1) / blksize) + 1;
  }
  if (size == 0 || blksize == 0 || blocks > SIZE_MAX / blksize || range_count == 0 ||
      lines.size() != 3 + range_count) {
    LOG(ERROR) << "Invalid data in block map file: size " << size << ", blksize " << blksize
               << ", range_count " << range_count << ", lines " << lines.size();
    return -1;
  }

  // Reserve enough contiguous address space for the whole file.
  void* reserve = mmap64(nullptr, blocks * blksize, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
  if (reserve == MAP_FAILED) {
    PLOG(ERROR) << "failed to reserve address space";
    return -1;
  }

  const std::string& block_dev = lines[0];
  android::base::unique_fd fd(TEMP_FAILURE_RETRY(open(block_dev.c_str(), O_RDONLY)));
  if (fd == -1) {
    PLOG(ERROR) << "failed to open block device " << block_dev;
    munmap(reserve, blocks * blksize);
    return -1;
  }

  pMap->ranges.resize(range_count);

  unsigned char* next = static_cast<unsigned char*>(reserve);
  size_t remaining_size = blocks * blksize;
  bool success = true;
  for (size_t i = 0; i < range_count; ++i) {
    const std::string& line = lines[i + 3];

    size_t start, end;
    if (sscanf(line.c_str(), "%zu %zu\n", &start, &end) != 2) {
      LOG(ERROR) << "failed to parse range " << i << " in block map: " << line;
      success = false;
      break;
    }
    size_t length = (end - start) * blksize;
    if (end <= start || (end - start) > SIZE_MAX / blksize || length > remaining_size) {
      LOG(ERROR) << "unexpected range in block map: " << start << " " << end;
      success = false;
      break;
    }

    void* addr = mmap64(next, length, PROT_READ, MAP_PRIVATE | MAP_FIXED, fd,
                        static_cast<off64_t>(start) * blksize);
    if (addr == MAP_FAILED) {
      PLOG(ERROR) << "failed to map block " << i;
      success = false;
      break;
    }
    pMap->ranges[i].addr = addr;
    pMap->ranges[i].length = length;

    next += length;
    remaining_size -= length;
  }
  if (success && remaining_size != 0) {
    LOG(ERROR) << "ranges in block map are invalid: remaining_size = " << remaining_size;
    success = false;
  }
  if (!success) {
    munmap(reserve, blocks * blksize);
    return -1;
  }

  pMap->addr = static_cast<unsigned char*>(reserve);
  pMap->length = size;

  LOG(INFO) << "mmapped " << range_count << " ranges";

  return 0;
}

int sysMapFile(const char* fn, MemMapping* pMap) {
  if (fn == nullptr || pMap == nullptr) {
    LOG(ERROR) << "Invalid argument(s)";
    return -1;
  }

  *pMap = {};

  if (fn[0] == '@') {
    if (sysMapBlockFile(fn + 1, pMap) != 0) {
      LOG(ERROR) << "Map of '" << fn << "' failed";
      return -1;
    }
  } else {
    // This is a regular file.
    android::base::unique_fd fd(TEMP_FAILURE_RETRY(open(fn, O_RDONLY)));
    if (fd == -1) {
      PLOG(ERROR) << "Unable to open '" << fn << "'";
      return -1;
    }

    if (!sysMapFD(fd, pMap)) {
      LOG(ERROR) << "Map of '" << fn << "' failed";
      return -1;
    }
  }
  return 0;
}

/*
 * Release a memory mapping.
 */
void sysReleaseMap(MemMapping* pMap) {
  std::for_each(pMap->ranges.cbegin(), pMap->ranges.cend(), [](const MappedRange& range) {
    if (munmap(range.addr, range.length) == -1) {
      PLOG(ERROR) << "munmap(" << range.addr << ", " << range.length << ") failed";
    }
  });
  pMap->ranges.clear();
}
