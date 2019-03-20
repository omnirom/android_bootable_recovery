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

bool FuseAdbDataProvider::ReadBlockAlignedData(uint8_t* buffer, uint32_t fetch_size,
                                               uint32_t start_block) const {
  if (!WriteFdFmt(fd_, "%08u", start_block)) {
    fprintf(stderr, "failed to write to adb host: %s\n", strerror(errno));
    return false;
  }

  if (!ReadFdExactly(fd_, buffer, fetch_size)) {
    fprintf(stderr, "failed to read from adb host: %s\n", strerror(errno));
    return false;
  }

  return true;
}

void FuseAdbDataProvider::Close() {
  WriteFdExactly(fd_, "DONEDONE");
}

int run_adb_fuse(android::base::unique_fd&& sfd, uint64_t file_size, uint32_t block_size) {
  FuseAdbDataProvider adb_data_reader(std::move(sfd), file_size, block_size);

  provider_vtab vtab;
  vtab.read_block = std::bind(&FuseAdbDataProvider::ReadBlockAlignedData, &adb_data_reader,
                              std::placeholders::_2, std::placeholders::_3, std::placeholders::_1);
  vtab.close = [&adb_data_reader]() { adb_data_reader.Close(); };

  return run_fuse_sideload(vtab, file_size, block_size);
}
