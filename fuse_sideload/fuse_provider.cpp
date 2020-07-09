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

#include "fuse_provider.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <functional>

#include <android-base/file.h>

#include "fuse_sideload.h"

FuseFileDataProvider::FuseFileDataProvider(const std::string& path, uint32_t block_size) {
  struct stat sb;
  if (stat(path.c_str(), &sb) == -1) {
    fprintf(stderr, "failed to stat %s: %s\n", path.c_str(), strerror(errno));
    return;
  }

  fd_.reset(open(path.c_str(), O_RDONLY));
  if (fd_ == -1) {
    fprintf(stderr, "failed to open %s: %s\n", path.c_str(), strerror(errno));
    return;
  }
  file_size_ = sb.st_size;
  fuse_block_size_ = block_size;
}

bool FuseFileDataProvider::ReadBlockAlignedData(uint8_t* buffer, uint32_t fetch_size,
                                                uint32_t start_block) const {
  uint64_t offset = static_cast<uint64_t>(start_block) * fuse_block_size_;
  if (fetch_size > file_size_ || offset > file_size_ - fetch_size) {
    fprintf(stderr,
            "Out of bound read, start block: %" PRIu32 ", fetch size: %" PRIu32
            ", file size %" PRIu64 "\n",
            start_block, fetch_size, file_size_);
    return false;
  }

  if (!android::base::ReadFullyAtOffset(fd_, buffer, fetch_size, offset)) {
    fprintf(stderr, "Failed to read fetch size: %" PRIu32 " bytes data at offset %" PRIu64 ": %s\n",
            fetch_size, offset, strerror(errno));
    return false;
  }

  return true;
}

void FuseFileDataProvider::Close() {
  fd_.reset();
}
