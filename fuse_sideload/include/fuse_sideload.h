/*
 * Copyright (C) 2014 The Android Open Source Project
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

#ifndef __FUSE_SIDELOAD_H
#define __FUSE_SIDELOAD_H

#include <functional>

// Define the filenames created by the sideload FUSE filesystem.
static constexpr const char* FUSE_SIDELOAD_HOST_MOUNTPOINT = "/sideload";
static constexpr const char* FUSE_SIDELOAD_HOST_FILENAME = "package.zip";
static constexpr const char* FUSE_SIDELOAD_HOST_PATHNAME = "/sideload/package.zip";
static constexpr const char* FUSE_SIDELOAD_HOST_EXIT_FLAG = "exit";
static constexpr const char* FUSE_SIDELOAD_HOST_EXIT_PATHNAME = "/sideload/exit";

struct provider_vtab {
  // read a block
  std::function<bool(uint32_t block, uint8_t* buffer, uint32_t fetch_size)> read_block;

  // close down
  std::function<void(void)> close;
};

int run_fuse_sideload(const provider_vtab& vtab, uint64_t file_size, uint32_t block_size,
                      const char* mount_point = FUSE_SIDELOAD_HOST_MOUNTPOINT);

#endif
