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

#ifndef __FUSE_ADB_PROVIDER_H
#define __FUSE_ADB_PROVIDER_H

#include <stdint.h>

#include "android-base/unique_fd.h"

#include "fuse_provider.h"

// This class reads data from adb server.
class FuseAdbDataProvider : public FuseDataProvider {
 public:
  FuseAdbDataProvider(android::base::unique_fd&& fd, uint64_t file_size, uint32_t block_size)
      : FuseDataProvider(std::move(fd), file_size, block_size) {}

  bool ReadBlockAlignedData(uint8_t* buffer, uint32_t fetch_size,
                            uint32_t start_block) const override;

  void Close() override;
};

#endif
