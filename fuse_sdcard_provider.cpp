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
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include <android-base/file.h>

#include "fuse_sideload.h"

struct file_data {
    int fd;  // the underlying sdcard file

    uint64_t file_size;
    uint32_t block_size;
};

static int read_block_file(void* cookie, uint32_t block, uint8_t* buffer, uint32_t fetch_size) {
    file_data* fd = reinterpret_cast<file_data*>(cookie);

    off64_t offset = ((off64_t) block) * fd->block_size;
    if (TEMP_FAILURE_RETRY(lseek64(fd->fd, offset, SEEK_SET)) == -1) {
        fprintf(stderr, "seek on sdcard failed: %s\n", strerror(errno));
        return -EIO;
    }

    if (!android::base::ReadFully(fd->fd, buffer, fetch_size)) {
        fprintf(stderr, "read on sdcard failed: %s\n", strerror(errno));
        return -EIO;
    }

    return 0;
}

static void close_file(void* cookie) {
    file_data* fd = reinterpret_cast<file_data*>(cookie);
    close(fd->fd);
}

bool start_sdcard_fuse(const char* path) {
    struct stat sb;
    if (stat(path, &sb) == -1) {
        fprintf(stderr, "failed to stat %s: %s\n", path, strerror(errno));
        return false;
    }

    file_data fd;
    fd.fd = open(path, O_RDONLY);
    if (fd.fd == -1) {
        fprintf(stderr, "failed to open %s: %s\n", path, strerror(errno));
        return false;
    }
    fd.file_size = sb.st_size;
    fd.block_size = 65536;

    provider_vtab vtab;
    vtab.read_block = read_block_file;
    vtab.close = close_file;

    // The installation process expects to find the sdcard unmounted.
    // Unmount it with MNT_DETACH so that our open file continues to
    // work but new references see it as unmounted.
    umount2("/sdcard", MNT_DETACH);

    return run_fuse_sideload(&vtab, &fd, fd.file_size, fd.block_size) == 0;
}
