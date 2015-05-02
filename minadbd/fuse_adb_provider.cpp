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
#include <string.h>
#include <errno.h>

#include "sysdeps.h"

#include "adb.h"
#include "adb_io.h"
#include "fuse_adb_provider.h"
#include "fuse_sideload.h"

int read_block_adb(void* data, uint32_t block, uint8_t* buffer, uint32_t fetch_size) {
    adb_data* ad = reinterpret_cast<adb_data*>(data);

    if (!WriteFdFmt(ad->sfd, "%08u", block)) {
        fprintf(stderr, "failed to write to adb host: %s\n", strerror(errno));
        return -EIO;
    }

    if (!ReadFdExactly(ad->sfd, buffer, fetch_size)) {
        fprintf(stderr, "failed to read from adb host: %s\n", strerror(errno));
        return -EIO;
    }

    return 0;
}

static void close_adb(void* data) {
    adb_data* ad = reinterpret_cast<adb_data*>(data);
    WriteFdExactly(ad->sfd, "DONEDONE");
}

int run_adb_fuse(int sfd, uint64_t file_size, uint32_t block_size) {
    adb_data ad;
    ad.sfd = sfd;
    ad.file_size = file_size;
    ad.block_size = block_size;

    provider_vtab vtab;
    vtab.read_block = read_block_adb;
    vtab.close = close_adb;

    return run_fuse_sideload(&vtab, &ad, file_size, block_size);
}
