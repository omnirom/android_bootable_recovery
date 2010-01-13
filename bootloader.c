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
static const int MISC_PAGES = 3;         // number of pages to save
static const int MISC_COMMAND_PAGE = 1;  // bootloader command is this page

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

/* Update Image
 *
 * - will be stored in the "cache" partition
 * - bad blocks will be ignored, like boot.img and recovery.img
 * - the first block will be the image header (described below)
 * - the size is in BYTES, inclusive of the header
 * - offsets are in BYTES from the start of the update header
 * - two raw bitmaps will be included, the "busy" and "fail" bitmaps
 * - for dream, the bitmaps will be 320x480x16bpp RGB565
 */

#define UPDATE_MAGIC       "MSM-RADIO-UPDATE"
#define UPDATE_MAGIC_SIZE  16
#define UPDATE_VERSION     0x00010000

struct update_header {
    unsigned char MAGIC[UPDATE_MAGIC_SIZE];

    unsigned version;
    unsigned size;

    unsigned image_offset;
    unsigned image_length;

    unsigned bitmap_width;
    unsigned bitmap_height;
    unsigned bitmap_bpp;

    unsigned busy_bitmap_offset;
    unsigned busy_bitmap_length;

    unsigned fail_bitmap_offset;
    unsigned fail_bitmap_length;
};

int write_update_for_bootloader(
        const char *update, int update_length,
        int bitmap_width, int bitmap_height, int bitmap_bpp,
        const char *busy_bitmap, const char *fail_bitmap) {
    if (ensure_root_path_unmounted(CACHE_NAME)) {
        LOGE("Can't unmount %s\n", CACHE_NAME);
        return -1;
    }

    const MtdPartition *part = get_root_mtd_partition(CACHE_NAME);
    if (part == NULL) {
        LOGE("Can't find %s\n", CACHE_NAME);
        return -1;
    }

    MtdWriteContext *write = mtd_write_partition(part);
    if (write == NULL) {
        LOGE("Can't open %s\n(%s)\n", CACHE_NAME, strerror(errno));
        return -1;
    }

    /* Write an invalid (zero) header first, to disable any previous
     * update and any other structured contents (like a filesystem),
     * and as a placeholder for the amount of space required.
     */

    struct update_header header;
    memset(&header, 0, sizeof(header));
    const ssize_t header_size = sizeof(header);
    if (mtd_write_data(write, (char*) &header, header_size) != header_size) {
        LOGE("Can't write header to %s\n(%s)\n", CACHE_NAME, strerror(errno));
        mtd_write_close(write);
        return -1;
    }

    /* Write each section individually block-aligned, so we can write
     * each block independently without complicated buffering.
     */

    memcpy(&header.MAGIC, UPDATE_MAGIC, UPDATE_MAGIC_SIZE);
    header.version = UPDATE_VERSION;
    header.size = header_size;

    off_t image_start_pos = mtd_erase_blocks(write, 0);
    header.image_length = update_length;
    if ((int) header.image_offset == -1 ||
        mtd_write_data(write, update, update_length) != update_length) {
        LOGE("Can't write update to %s\n(%s)\n", CACHE_NAME, strerror(errno));
        mtd_write_close(write);
        return -1;
    }
    off_t busy_start_pos = mtd_erase_blocks(write, 0);
    header.image_offset = mtd_find_write_start(write, image_start_pos);

    header.bitmap_width = bitmap_width;
    header.bitmap_height = bitmap_height;
    header.bitmap_bpp = bitmap_bpp;

    int bitmap_length = (bitmap_bpp + 7) / 8 * bitmap_width * bitmap_height;

    header.busy_bitmap_length = busy_bitmap != NULL ? bitmap_length : 0;
    if ((int) header.busy_bitmap_offset == -1 ||
        mtd_write_data(write, busy_bitmap, bitmap_length) != bitmap_length) {
        LOGE("Can't write bitmap to %s\n(%s)\n", CACHE_NAME, strerror(errno));
        mtd_write_close(write);
        return -1;
    }
    off_t fail_start_pos = mtd_erase_blocks(write, 0);
    header.busy_bitmap_offset = mtd_find_write_start(write, busy_start_pos);

    header.fail_bitmap_length = fail_bitmap != NULL ? bitmap_length : 0;
    if ((int) header.fail_bitmap_offset == -1 ||
        mtd_write_data(write, fail_bitmap, bitmap_length) != bitmap_length) {
        LOGE("Can't write bitmap to %s\n(%s)\n", CACHE_NAME, strerror(errno));
        mtd_write_close(write);
        return -1;
    }
    mtd_erase_blocks(write, 0);
    header.fail_bitmap_offset = mtd_find_write_start(write, fail_start_pos);

    /* Write the header last, after all the blocks it refers to, so that
     * when the magic number is installed everything is valid.
     */

    if (mtd_write_close(write)) {
        LOGE("Can't finish writing %s\n(%s)\n", CACHE_NAME, strerror(errno));
        return -1;
    }

    write = mtd_write_partition(part);
    if (write == NULL) {
        LOGE("Can't reopen %s\n(%s)\n", CACHE_NAME, strerror(errno));
        return -1;
    }

    if (mtd_write_data(write, (char*) &header, header_size) != header_size) {
        LOGE("Can't rewrite header to %s\n(%s)\n", CACHE_NAME, strerror(errno));
        mtd_write_close(write);
        return -1;
    }

    if (mtd_erase_blocks(write, 0) != image_start_pos) {
        LOGE("Misalignment rewriting %s\n(%s)\n", CACHE_NAME, strerror(errno));
        mtd_write_close(write);
        return -1;
    }

    if (mtd_write_close(write)) {
        LOGE("Can't finish header of %s\n(%s)\n", CACHE_NAME, strerror(errno));
        return -1;
    }

    return 0;
}
