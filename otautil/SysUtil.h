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

#ifndef _OTAUTIL_SYSUTIL
#define _OTAUTIL_SYSUTIL

#include <sys/types.h>

#include <string>
#include <vector>

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

#endif  // _OTAUTIL_SYSUTIL
