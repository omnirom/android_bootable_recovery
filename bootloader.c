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

#include "bootloader.h"
#include "common.h"
#include "mtdutils/mtdutils.h"
#include "roots.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static const char *CACHE_NAME = "CACHE:";
static const char *MISC_NAME = "MISC:";

#ifdef LOG_VERBOSE
static void dump_data(const char *data, int len) {
    int pos;
    for (pos = 0; pos < len; ) {
        printf("%05x: %02x", pos, data[pos]);
        for (++pos; pos < len && (pos % 24) != 0; ++pos) {
            printf(" %02x", data[pos]);
        }
        printf("\n");
    }
}
#endif

#ifdef USE_EXT4
// Strictly speaking this doesn't have anything to do with ext4; we
// really just mean "misc is an emmc partition".  We should have a
// more configurable way have describing partitions, filesystems, etc.

static const char* MISC_PARTITION =
    "/dev/block/platform/sdhci-tegra.3/by-name/misc";

int get_bootloader_message(struct bootloader_message* out) {
    FILE* f = fopen(MISC_PARTITION, "rb");
    if (f == NULL) {
        LOGE("Can't open %s\n(%s)\n", MISC_PARTITION, strerror(errno));
        return -1;
    }
    struct bootloader_message temp;
    int count = fread(&temp, sizeof(temp), 1, f);
    if (count != 1) {
        LOGE("Failed reading %s\n(%s)\n", MISC_PARTITION, strerror(errno));
        return -1;
    }
    if (fclose(f) != 0) {
        LOGE("Failed closing %s\n(%s)\n", MISC_PARTITION, strerror(errno));
        return -1;
    }
    memcpy(out, &temp, sizeof(temp));
    return 0;
}

int set_bootloader_message(const struct bootloader_message* in) {
    FILE* f = fopen(MISC_PARTITION, "wb");
    if (f == NULL) {
        LOGE("Can't open %s\n(%s)\n", MISC_PARTITION, strerror(errno));
        return -1;
    }
    int count = fwrite(in, sizeof(*in), 1, f);
    if (count != 1) {
        LOGE("Failed writing %s\n(%s)\n", MISC_PARTITION, strerror(errno));
        return -1;
    }
    if (fclose(f) != 0) {
        LOGE("Failed closing %s\n(%s)\n", MISC_PARTITION, strerror(errno));
        return -1;
    }
    return 0;
}

#else  // MTD partitions

static const int MISC_PAGES = 3;         // number of pages to save
static const int MISC_COMMAND_PAGE = 1;  // bootloader command is this page

int get_bootloader_message(struct bootloader_message *out) {
    size_t write_size;
    const MtdPartition *part = get_root_mtd_partition(MISC_NAME);
    if (part == NULL || mtd_partition_info(part, NULL, NULL, &write_size)) {
        LOGE("Can't find %s\n", MISC_NAME);
        return -1;
    }

    MtdReadContext *read = mtd_read_partition(part);
    if (read == NULL) {
        LOGE("Can't open %s\n(%s)\n", MISC_NAME, strerror(errno));
        return -1;
    }

    const ssize_t size = write_size * MISC_PAGES;
    char data[size];
    ssize_t r = mtd_read_data(read, data, size);
    if (r != size) LOGE("Can't read %s\n(%s)\n", MISC_NAME, strerror(errno));
    mtd_read_close(read);
    if (r != size) return -1;

#ifdef LOG_VERBOSE
    printf("\n--- get_bootloader_message ---\n");
    dump_data(data, size);
    printf("\n");
#endif

    memcpy(out, &data[write_size * MISC_COMMAND_PAGE], sizeof(*out));
    return 0;
}

int set_bootloader_message(const struct bootloader_message *in) {
    size_t write_size;
    const MtdPartition *part = get_root_mtd_partition(MISC_NAME);
    if (part == NULL || mtd_partition_info(part, NULL, NULL, &write_size)) {
        LOGE("Can't find %s\n", MISC_NAME);
        return -1;
    }

    MtdReadContext *read = mtd_read_partition(part);
    if (read == NULL) {
        LOGE("Can't open %s\n(%s)\n", MISC_NAME, strerror(errno));
        return -1;
    }

    ssize_t size = write_size * MISC_PAGES;
    char data[size];
    ssize_t r = mtd_read_data(read, data, size);
    if (r != size) LOGE("Can't read %s\n(%s)\n", MISC_NAME, strerror(errno));
    mtd_read_close(read);
    if (r != size) return -1;

    memcpy(&data[write_size * MISC_COMMAND_PAGE], in, sizeof(*in));

#ifdef LOG_VERBOSE
    printf("\n--- set_bootloader_message ---\n");
    dump_data(data, size);
    printf("\n");
#endif

    MtdWriteContext *write = mtd_write_partition(part);
    if (write == NULL) {
        LOGE("Can't open %s\n(%s)\n", MISC_NAME, strerror(errno));
        return -1;
    }
    if (mtd_write_data(write, data, size) != size) {
        LOGE("Can't write %s\n(%s)\n", MISC_NAME, strerror(errno));
        mtd_write_close(write);
        return -1;
    }
    if (mtd_write_close(write)) {
        LOGE("Can't finish %s\n(%s)\n", MISC_NAME, strerror(errno));
        return -1;
    }

    LOGI("Set boot command \"%s\"\n", in->command[0] != 255 ? in->command : "");
    return 0;
}

#endif
