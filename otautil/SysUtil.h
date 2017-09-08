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

#include <vector>

struct MappedRange {
  void* addr;
  size_t length;
};

/*
 * Use this to keep track of mapped segments.
 */
struct MemMapping {
  unsigned char* addr; /* start of data */
  size_t length;       /* length of data */

  std::vector<MappedRange> ranges;
};

/*
 * Map a file into a private, read-only memory segment.  If 'fn'
 * begins with an '@' character, it is a map of blocks to be mapped,
 * otherwise it is treated as an ordinary file.
 *
 * On success, "pMap" is filled in, and zero is returned.
 */
int sysMapFile(const char* fn, MemMapping* pMap);

/*
 * Release the pages associated with a shared memory segment.
 *
 * This does not free "pMap"; it just releases the memory.
 */
void sysReleaseMap(MemMapping* pMap);

#endif  // _OTAUTIL_SYSUTIL
