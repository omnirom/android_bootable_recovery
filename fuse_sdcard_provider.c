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
#include <pthread.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "fuse_sideload.h"

struct file_data {
    int fd;  // the underlying sdcard file

    uint64_t file_size;
    uint32_t block_size;
};

static int read_block_file(void* cookie, uint32_t block, uint8_t* buffer, uint32_t fetch_size) {
    struct file_data* fd = (struct file_data*)cookie;

    off64_t offset = ((off64_t) block) * fd->block_size;
    if (TEMP_FAILURE_RETRY(lseek64(fd->fd, offset, SEEK_SET)) == -1) {
        fprintf(stderr, "seek on sdcard failed: %s\n", strerror(errno));
        return -EIO;
    }

    while (fetch_size > 0) {
        ssize_t r = TEMP_FAILURE_RETRY(read(fd->fd, buffer, fetch_size));
        if (r == -1) {
            fprintf(stderr, "read on sdcard failed: %s\n", strerror(errno));
            return -EIO;
        }
        fetch_size -= r;
        buffer += r;
    }

    return 0;
}

static void close_file(void* cookie) {
    struct file_data* fd = (struct file_data*)cookie;
    close(fd->fd);
}

struct token {
    pthread_t th;
    const char* path;
    int result;
};

static void* run_sdcard_fuse(void* cookie) {
    struct token* t = (struct token*)cookie;

    struct stat sb;
    if (stat(t->path, &sb) < 0) {
        fprintf(stderr, "failed to stat %s: %s\n", t->path, strerror(errno));
        t->result = -1;
        return NULL;
    }

    struct file_data fd;
    struct provider_vtab vtab;

    fd.fd = open(t->path, O_RDONLY);
    if (fd.fd < 0) {
        fprintf(stderr, "failed to open %s: %s\n", t->path, strerror(errno));
        t->result = -1;
        return NULL;
    }
    fd.file_size = sb.st_size;
    fd.block_size = 65536;

    vtab.read_block = read_block_file;
    vtab.close = close_file;

    t->result = run_fuse_sideload(&vtab, &fd, fd.file_size, fd.block_size);
    return NULL;
}

// How long (in seconds) we wait for the fuse-provided package file to
// appear, before timing out.
#define SDCARD_INSTALL_TIMEOUT 10

void* start_sdcard_fuse(const char* path) {
    struct token* t = malloc(sizeof(struct token));

    t->path = path;
    pthread_create(&(t->th), NULL, run_sdcard_fuse, t);

    struct stat st;
    int i;
    for (i = 0; i < SDCARD_INSTALL_TIMEOUT; ++i) {
        if (stat(FUSE_SIDELOAD_HOST_PATHNAME, &st) != 0) {
            if (errno == ENOENT && i < SDCARD_INSTALL_TIMEOUT-1) {
                sleep(1);
                continue;
            } else {
                return NULL;
            }
        }
    }

    // The installation process expects to find the sdcard unmounted.
    // Unmount it with MNT_DETACH so that our open file continues to
    // work but new references see it as unmounted.
    umount2("/sdcard", MNT_DETACH);

    return t;
}

void finish_sdcard_fuse(void* cookie) {
    if (cookie == NULL) return;
    struct token* t = (struct token*)cookie;

    // Calling stat() on this magic filename signals the fuse
    // filesystem to shut down.
    struct stat st;
    stat(FUSE_SIDELOAD_HOST_EXIT_PATHNAME, &st);

    pthread_join(t->th, NULL);
    free(t);
}
