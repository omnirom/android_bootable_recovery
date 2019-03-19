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

#include "fuse_sdcard_install.h"

#include <sys/mount.h>
#include <unistd.h>

#include <functional>

#include "fuse_provider.h"
#include "fuse_sideload.h"

bool start_sdcard_fuse(const char* path) {
  FuseFileDataProvider file_data_reader(path, 65536);

  if (!file_data_reader) {
    return false;
  }

  provider_vtab vtab;
  vtab.read_block = std::bind(&FuseFileDataProvider::ReadBlockAlignedData, &file_data_reader,
                              std::placeholders::_2, std::placeholders::_3, std::placeholders::_1);
  vtab.close = [&file_data_reader]() { file_data_reader.Close(); };

  // The installation process expects to find the sdcard unmounted. Unmount it with MNT_DETACH so
  // that our open file continues to work but new references see it as unmounted.
  umount2("/sdcard", MNT_DETACH);

  return run_fuse_sideload(vtab, file_data_reader.file_size(),
                           file_data_reader.fuse_block_size()) == 0;
}
