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

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include "adb.h"
#include "fuse_sideload.h"

struct adb_data {
    int sfd;  // file descriptor for the adb channel

    uint64_t file_size;
    uint32_t block_size;
};

static int read_block_adb(void* cookie, uint32_t block, uint8_t* buffer, uint32_t fetch_size) {
    struct adb_data* ad = (struct adb_data*)cookie;

    char buf[10];
    snprintf(buf, sizeof(buf), "%08u", block);
    if (writex(ad->sfd, buf, 8) < 0) {
        fprintf(stderr, "failed to write to adb host: %s\n", strerror(errno));
        return -EIO;
    }

    if (readx(ad->sfd, buffer, fetch_size) < 0) {
        fprintf(stderr, "failed to read from adb host: %s\n", strerror(errno));
        return -EIO;
    }

    return 0;
}

static void close_adb(void* cookie) {
    struct adb_data* ad = (struct adb_data*)cookie;

    writex(ad->sfd, "DONEDONE", 8);
}

int run_adb_fuse(int sfd, uint64_t file_size, uint32_t block_size) {
    struct adb_data ad;
    struct provider_vtab vtab;

    ad.sfd = sfd;
    ad.file_size = file_size;
    ad.block_size = block_size;

    vtab.read_block = read_block_adb;
    vtab.close = close_adb;

    return run_fuse_sideload(&vtab, &ad, file_size, block_size);
}
