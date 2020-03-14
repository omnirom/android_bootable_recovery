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

#pragma once

#include <sys/types.h>

#include <string>
#include <string_view>
#include <vector>

#include "rangeset.h"

// This class holds the content of a block map file.
class BlockMapData {
 public:
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
  static BlockMapData ParseBlockMapFile(const std::string& block_map_path);

  explicit operator bool() const {
    return !path_.empty();
  }

  std::string path() const {
    return path_;
  }
  uint64_t file_size() const {
    return file_size_;
  }
  uint32_t block_size() const {
    return block_size_;
  }
  RangeSet block_ranges() const {
    return block_ranges_;
  }

 private:
  BlockMapData() = default;

  BlockMapData(const std::string& path, uint64_t file_size, uint32_t block_size,
               RangeSet block_ranges)
      : path_(path),
        file_size_(file_size),
        block_size_(block_size),
        block_ranges_(std::move(block_ranges)) {}

  std::string path_;
  uint64_t file_size_ = 0;
  uint32_t block_size_ = 0;
  RangeSet block_ranges_;
};

/*
 * Use this to keep track of mapped segments.
 */
class MemMapping {
 public:
  ~MemMapping();
  // Map a file into a private, read-only memory segment. If 'filename' begins with an '@'
  // character, it is a map of blocks to be mapped, otherwise it is treated as an ordinary file.
  bool MapFile(const std::string& filename);
  size_t ranges() const {
    return ranges_.size();
  };

  unsigned char* addr;  // start of data
  size_t length;        // length of data

 private:
  struct MappedRange {
    void* addr;
    size_t length;
  };

  bool MapBlockFile(const std::string& filename);
  bool MapFD(int fd);

  std::vector<MappedRange> ranges_;
};

// Reboots the device into the specified target, by additionally handling quiescent reboot mode.
// All unknown targets reboot into Android.
[[noreturn]] void Reboot(std::string_view target);

// Triggers a shutdown.
bool Shutdown(std::string_view target);

// Returns a null-terminated char* array, where the elements point to the C-strings in the given
// vector, plus an additional nullptr at the end. This is a helper function that facilitates
// calling C functions (such as getopt(3)) that expect an array of C-strings.
std::vector<char*> StringVectorToNullTerminatedArray(const std::vector<std::string>& args);
