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

struct adb_data {
    int sfd;  // file descriptor for the adb channel

    uint64_t file_size;
    uint32_t block_size;
};

int read_block_adb(void* cookie, uint32_t block, uint8_t* buffer, uint32_t fetch_size);
int run_adb_fuse(int sfd, uint64_t file_size, uint32_t block_size);

#endif
