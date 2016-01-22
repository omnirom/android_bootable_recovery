/*
 * Copyright (C) 2008 The Android Open Source Project
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

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <fs_mgr.h>

#include "bootloader.h"
#include "common.h"
#include "mtdutils/mtdutils.h"
#include "roots.h"
#include "unique_fd.h"

static int get_bootloader_message_mtd(bootloader_message* out, const Volume* v);
static int set_bootloader_message_mtd(const bootloader_message* in, const Volume* v);
static int get_bootloader_message_block(bootloader_message* out, const Volume* v);
static int set_bootloader_message_block(const bootloader_message* in, const Volume* v);

int get_bootloader_message(bootloader_message* out) {
    Volume* v = volume_for_path("/misc");
    if (v == nullptr) {
        LOGE("Cannot load volume /misc!\n");
        return -1;
    }
    if (strcmp(v->fs_type, "mtd") == 0) {
        return get_bootloader_message_mtd(out, v);
    } else if (strcmp(v->fs_type, "emmc") == 0) {
        return get_bootloader_message_block(out, v);
    }
    LOGE("unknown misc partition fs_type \"%s\"\n", v->fs_type);
    return -1;
}

int set_bootloader_message(const bootloader_message* in) {
    Volume* v = volume_for_path("/misc");
    if (v == nullptr) {
        LOGE("Cannot load volume /misc!\n");
        return -1;
    }
    if (strcmp(v->fs_type, "mtd") == 0) {
        return set_bootloader_message_mtd(in, v);
    } else if (strcmp(v->fs_type, "emmc") == 0) {
        return set_bootloader_message_block(in, v);
    }
    LOGE("unknown misc partition fs_type \"%s\"\n", v->fs_type);
    return -1;
}

// ------------------------------
// for misc partitions on MTD
// ------------------------------

static const int MISC_PAGES = 3;         // number of pages to save
static const int MISC_COMMAND_PAGE = 1;  // bootloader command is this page

static int get_bootloader_message_mtd(bootloader_message* out,
                                      const Volume* v) {
    size_t write_size;
    mtd_scan_partitions();
    const MtdPartition* part = mtd_find_partition_by_name(v->blk_device);
    if (part == nullptr || mtd_partition_info(part, nullptr, nullptr, &write_size)) {
        LOGE("failed to find \"%s\"\n", v->blk_device);
        return -1;
    }

    MtdReadContext* read = mtd_read_partition(part);
    if (read == nullptr) {
        LOGE("failed to open \"%s\": %s\n", v->blk_device, strerror(errno));
        return -1;
    }

    const ssize_t size = write_size * MISC_PAGES;
    char data[size];
    ssize_t r = mtd_read_data(read, data, size);
    if (r != size) LOGE("failed to read \"%s\": %s\n", v->blk_device, strerror(errno));
    mtd_read_close(read);
    if (r != size) return -1;

    memcpy(out, &data[write_size * MISC_COMMAND_PAGE], sizeof(*out));
    return 0;
}
static int set_bootloader_message_mtd(const bootloader_message* in,
                                      const Volume* v) {
    size_t write_size;
    mtd_scan_partitions();
    const MtdPartition* part = mtd_find_partition_by_name(v->blk_device);
    if (part == nullptr || mtd_partition_info(part, nullptr, nullptr, &write_size)) {
        LOGE("failed to find \"%s\"\n", v->blk_device);
        return -1;
    }

    MtdReadContext* read = mtd_read_partition(part);
    if (read == nullptr) {
        LOGE("failed to open \"%s\": %s\n", v->blk_device, strerror(errno));
        return -1;
    }

    ssize_t size = write_size * MISC_PAGES;
    char data[size];
    ssize_t r = mtd_read_data(read, data, size);
    if (r != size) LOGE("failed to read \"%s\": %s\n", v->blk_device, strerror(errno));
    mtd_read_close(read);
    if (r != size) return -1;

    memcpy(&data[write_size * MISC_COMMAND_PAGE], in, sizeof(*in));

    MtdWriteContext* write = mtd_write_partition(part);
    if (write == nullptr) {
        LOGE("failed to open \"%s\": %s\n", v->blk_device, strerror(errno));
        return -1;
    }
    if (mtd_write_data(write, data, size) != size) {
        LOGE("failed to write \"%s\": %s\n", v->blk_device, strerror(errno));
        mtd_write_close(write);
        return -1;
    }
    if (mtd_write_close(write)) {
        LOGE("failed to finish \"%s\": %s\n", v->blk_device, strerror(errno));
        return -1;
    }

    LOGI("Set boot command \"%s\"\n", in->command[0] != 255 ? in->command : "");
    return 0;
}


// ------------------------------------
// for misc partitions on block devices
// ------------------------------------

static void wait_for_device(const char* fn) {
    int tries = 0;
    int ret;
    do {
        ++tries;
        struct stat buf;
        ret = stat(fn, &buf);
        if (ret == -1) {
            printf("failed to stat \"%s\" try %d: %s\n", fn, tries, strerror(errno));
            sleep(1);
        }
    } while (ret && tries < 10);

    if (ret) {
        printf("failed to stat \"%s\"\n", fn);
    }
}

static int get_bootloader_message_block(bootloader_message* out,
                                        const Volume* v) {
    wait_for_device(v->blk_device);
    FILE* f = fopen(v->blk_device, "rb");
    if (f == nullptr) {
        LOGE("failed to open \"%s\": %s\n", v->blk_device, strerror(errno));
        return -1;
    }
    bootloader_message temp;
    int count = fread(&temp, sizeof(temp), 1, f);
    if (count != 1) {
        LOGE("failed to read \"%s\": %s\n", v->blk_device, strerror(errno));
        return -1;
    }
    if (fclose(f) != 0) {
        LOGE("failed to close \"%s\": %s\n", v->blk_device, strerror(errno));
        return -1;
    }
    memcpy(out, &temp, sizeof(temp));
    return 0;
}

static int set_bootloader_message_block(const bootloader_message* in,
                                        const Volume* v) {
    wait_for_device(v->blk_device);
    unique_fd fd(open(v->blk_device, O_WRONLY | O_SYNC));
    if (fd.get() == -1) {
        LOGE("failed to open \"%s\": %s\n", v->blk_device, strerror(errno));
        return -1;
    }

    size_t written = 0;
    const uint8_t* start = reinterpret_cast<const uint8_t*>(in);
    size_t total = sizeof(*in);
    while (written < total) {
        ssize_t wrote = TEMP_FAILURE_RETRY(write(fd.get(), start + written, total - written));
        if (wrote == -1) {
            LOGE("failed to write %" PRId64 " bytes: %s\n",
                 static_cast<off64_t>(written), strerror(errno));
            return -1;
        }
        written += wrote;
    }

    if (fsync(fd.get()) == -1) {
        LOGE("failed to fsync \"%s\": %s\n", v->blk_device, strerror(errno));
        return -1;
    }
    return 0;
}
