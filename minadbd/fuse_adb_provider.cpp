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

#include "fuse_adb_provider.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <functional>

#include "adb.h"
#include "adb_io.h"
#include "fuse_sideload.h"

int read_block_adb(const adb_data& ad, uint32_t block, uint8_t* buffer, uint32_t fetch_size) {
  if (!WriteFdFmt(ad.sfd, "%08u", block)) {
    fprintf(stderr, "failed to write to adb host: %s\n", strerror(errno));
    return -EIO;
  }

  if (!ReadFdExactly(ad.sfd, buffer, fetch_size)) {
    fprintf(stderr, "failed to read from adb host: %s\n", strerror(errno));
    return -EIO;
  }

  return 0;
}

int run_adb_fuse(int sfd, uint64_t file_size, uint32_t block_size) {
  adb_data ad;
  ad.sfd = sfd;
  ad.file_size = file_size;
  ad.block_size = block_size;

  provider_vtab vtab;
  vtab.read_block = std::bind(read_block_adb, ad, std::placeholders::_1, std::placeholders::_2,
                              std::placeholders::_3);
  vtab.close = [&ad]() { WriteFdExactly(ad.sfd, "DONEDONE"); };

  return run_fuse_sideload(vtab, file_size, block_size);
}
