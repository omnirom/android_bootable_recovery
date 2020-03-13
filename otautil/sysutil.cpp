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

#include "otautil/sysutil.h"

#include <errno.h>  // TEMP_FAILURE_RETRY
#include <fcntl.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/strings.h>
#include <android-base/unique_fd.h>
#include <cutils/android_reboot.h>

BlockMapData BlockMapData::ParseBlockMapFile(const std::string& block_map_path) {
  std::string content;
  if (!android::base::ReadFileToString(block_map_path, &content)) {
    PLOG(ERROR) << "Failed to read " << block_map_path;
    return {};
  }

  std::vector<std::string> lines = android::base::Split(android::base::Trim(content), "\n");
  if (lines.size() < 4) {
    LOG(ERROR) << "Block map file is too short: " << lines.size();
    return {};
  }

  const std::string& block_dev = lines[0];

  uint64_t file_size;
  uint32_t blksize;
  if (sscanf(lines[1].c_str(), "%" SCNu64 "%" SCNu32, &file_size, &blksize) != 2) {
    LOG(ERROR) << "Failed to parse file size and block size: " << lines[1];
    return {};
  }

  if (file_size == 0 || blksize == 0) {
    LOG(ERROR) << "Invalid size in block map file: size " << file_size << ", blksize " << blksize;
    return {};
  }

  size_t range_count;
  if (sscanf(lines[2].c_str(), "%zu", &range_count) != 1) {
    LOG(ERROR) << "Failed to parse block map header: " << lines[2];
    return {};
  }

  uint64_t blocks = ((file_size - 1) / blksize) + 1;
  if (blocks > std::numeric_limits<uint32_t>::max() || range_count == 0 ||
      lines.size() != 3 + range_count) {
    LOG(ERROR) << "Invalid data in block map file: size " << file_size << ", blksize " << blksize
               << ", range_count " << range_count << ", lines " << lines.size();
    return {};
  }

  RangeSet ranges;
  uint64_t remaining_blocks = blocks;
  for (size_t i = 0; i < range_count; ++i) {
    const std::string& line = lines[i + 3];
    uint64_t start, end;
    if (sscanf(line.c_str(), "%" SCNu64 "%" SCNu64, &start, &end) != 2) {
      LOG(ERROR) << "failed to parse range " << i << ": " << line;
      return {};
    }
    uint64_t range_blocks = end - start;
    if (end <= start || range_blocks > remaining_blocks) {
      LOG(ERROR) << "Invalid range: " << start << " " << end;
      return {};
    }
    ranges.PushBack({ start, end });
    remaining_blocks -= range_blocks;
  }

  if (remaining_blocks != 0) {
    LOG(ERROR) << "Invalid ranges: remaining blocks " << remaining_blocks;
    return {};
  }

  return BlockMapData(block_dev, file_size, blksize, std::move(ranges));
}

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

bool MemMapping::MapBlockFile(const std::string& filename) {
  auto block_map_data = BlockMapData::ParseBlockMapFile(filename);
  if (!block_map_data) {
    return false;
  }

  if (block_map_data.file_size() > std::numeric_limits<size_t>::max()) {
    LOG(ERROR) << "File size is too large for mmap " << block_map_data.file_size();
    return false;
  }

  // Reserve enough contiguous address space for the whole file.
  uint32_t blksize = block_map_data.block_size();
  uint64_t blocks = ((block_map_data.file_size() - 1) / blksize) + 1;
  void* reserve = mmap(nullptr, blocks * blksize, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
  if (reserve == MAP_FAILED) {
    PLOG(ERROR) << "failed to reserve address space";
    return false;
  }

  android::base::unique_fd fd(TEMP_FAILURE_RETRY(open(block_map_data.path().c_str(), O_RDONLY)));
  if (fd == -1) {
    PLOG(ERROR) << "failed to open block device " << block_map_data.path();
    munmap(reserve, blocks * blksize);
    return false;
  }

  ranges_.clear();

  auto next = static_cast<unsigned char*>(reserve);
  size_t remaining_size = blocks * blksize;
  for (const auto& [start, end] : block_map_data.block_ranges()) {
    size_t range_size = (end - start) * blksize;
    void* range_start = mmap(next, range_size, PROT_READ, MAP_PRIVATE | MAP_FIXED, fd,
                             static_cast<off_t>(start) * blksize);
    if (range_start == MAP_FAILED) {
      PLOG(ERROR) << "failed to map range " << start << ": " << end;
      munmap(reserve, blocks * blksize);
      return false;
    }
    ranges_.emplace_back(MappedRange{ range_start, range_size });

    next += range_size;
    remaining_size -= range_size;
  }
  if (remaining_size != 0) {
    LOG(ERROR) << "Invalid ranges: remaining_size " << remaining_size;
    munmap(reserve, blocks * blksize);
    return false;
  }

  addr = static_cast<unsigned char*>(reserve);
  length = block_map_data.file_size();

  LOG(INFO) << "mmapped " << block_map_data.block_ranges().size() << " ranges";

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

void Reboot(std::string_view target) {
  std::string cmd = "reboot," + std::string(target);
  // Honor the quiescent mode if applicable.
  if (target != "bootloader" && target != "fastboot" &&
      android::base::GetBoolProperty("ro.boot.quiescent", false)) {
    cmd += ",quiescent";
  }
  if (!android::base::SetProperty(ANDROID_RB_PROPERTY, cmd)) {
    LOG(FATAL) << "Reboot failed";
  }

  while (true) pause();
}

bool Shutdown(std::string_view target) {
  std::string cmd = "shutdown," + std::string(target);
  return android::base::SetProperty(ANDROID_RB_PROPERTY, cmd);
}

std::vector<char*> StringVectorToNullTerminatedArray(const std::vector<std::string>& args) {
  std::vector<char*> result(args.size());
  std::transform(args.cbegin(), args.cend(), result.begin(),
                 [](const std::string& arg) { return const_cast<char*>(arg.c_str()); });
  result.push_back(nullptr);
  return result;
}
