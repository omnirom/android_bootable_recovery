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

/*
#include <fs_mgr.h>
*/
#include "bootloader.h"
#include "common.h"
extern "C" {
#include "mtdutils/mtdutils.h"
}
#include "roots.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char device_type = 'e'; // e for emmc or m for mtd, default is emmc
static char device_name[256];

/*
static int get_bootloader_message_mtd(struct bootloader_message *out, const Volume* v);
static int set_bootloader_message_mtd(const struct bootloader_message *in, const Volume* v);
static int get_bootloader_message_block(struct bootloader_message *out, const Volume* v);
static int set_bootloader_message_block(const struct bootloader_message *in, const Volume* v);
*/
int get_bootloader_message(struct bootloader_message *out) {
    //volume_for_path("/misc");
    if (device_name[0] == 0) {
      LOGE("Cannot load volume /misc!\n");
      return -1;
    }
    if (device_type == 'm') {
        return get_bootloader_message_mtd_name(out);
    } else if (device_type == 'e') {
        return get_bootloader_message_block_name(out);
    }
    LOGE("unknown misc partition fs_type \"%c\"\n", device_type);
    return -1;
}

int set_bootloader_message(const struct bootloader_message *in) {
    //volume_for_path("/misc");
    if (device_name[0] == 0) {
      LOGE("Cannot load volume /misc!\n");
      return -1;
    }
    if (device_type == 'm') {
        return set_bootloader_message_mtd_name(in, device_name);
    } else if (device_type == 'e') {
        return set_bootloader_message_block_name(in, device_name);
    }
    LOGE("unknown misc partition type \"%c\"\n", device_type);
    return -1;
}

// ------------------------------
// for misc partitions on MTD
// ------------------------------

static const int MISC_PAGES = 3;         // number of pages to save
static const int MISC_COMMAND_PAGE = 1;  // bootloader command is this page
/*
static int get_bootloader_message_mtd(struct bootloader_message *out,
                                      const Volume* v) {
    size_t write_size;
    mtd_scan_partitions();
    const MtdPartition *part = mtd_find_partition_by_name(v->blk_device);
    if (part == NULL || mtd_partition_info(part, NULL, NULL, &write_size)) {
        LOGE("Can't find %s\n", v->blk_device);
        return -1;
    }

    MtdReadContext *read = mtd_read_partition(part);
    if (read == NULL) {
        LOGE("Can't open %s\n(%s)\n", v->blk_device, strerror(errno));
        return -1;
    }

    const ssize_t size = write_size * MISC_PAGES;
    char data[size];
    ssize_t r = mtd_read_data(read, data, size);
    if (r != size) LOGE("Can't read %s\n(%s)\n", v->blk_device, strerror(errno));
    mtd_read_close(read);
    if (r != size) return -1;

    memcpy(out, &data[write_size * MISC_COMMAND_PAGE], sizeof(*out));
    return 0;
}
static int set_bootloader_message_mtd(const struct bootloader_message *in,
                                      const Volume* v) {
    size_t write_size;
    mtd_scan_partitions();
    const MtdPartition *part = mtd_find_partition_by_name(v->blk_device);
    if (part == NULL || mtd_partition_info(part, NULL, NULL, &write_size)) {
        LOGE("Can't find %s\n", v->blk_device);
        return -1;
    }

    MtdReadContext *read = mtd_read_partition(part);
    if (read == NULL) {
        LOGE("Can't open %s\n(%s)\n", v->blk_device, strerror(errno));
        return -1;
    }

    ssize_t size = write_size * MISC_PAGES;
    char data[size];
    ssize_t r = mtd_read_data(read, data, size);
    if (r != size) LOGE("Can't read %s\n(%s)\n", v->blk_device, strerror(errno));
    mtd_read_close(read);
    if (r != size) return -1;

    memcpy(&data[write_size * MISC_COMMAND_PAGE], in, sizeof(*in));

    MtdWriteContext *write = mtd_write_partition(part);
    if (write == NULL) {
        LOGE("Can't open %s\n(%s)\n", v->blk_device, strerror(errno));
        return -1;
    }
    if (mtd_write_data(write, data, size) != size) {
        LOGE("Can't write %s\n(%s)\n", v->blk_device, strerror(errno));
        mtd_write_close(write);
        return -1;
    }
    if (mtd_write_close(write)) {
        LOGE("Can't finish %s\n(%s)\n", v->blk_device, strerror(errno));
        return -1;
    }

    LOGI("Set boot command \"%s\"\n", in->command[0] != 255 ? in->command : "");
    return 0;
}
*/

void set_device_type(char new_type) {
	device_type = new_type;
}

void set_device_name(const char* new_name) {
	if (strlen(new_name) >= sizeof(device_name)) {
		LOGE("New device name of '%s' is too large for bootloader.cpp\n", new_name);
	} else {
		strcpy(device_name, new_name);
	}
}

int get_bootloader_message_mtd_name(struct bootloader_message *out) {
    size_t write_size;
    mtd_scan_partitions();
    const MtdPartition *part = mtd_find_partition_by_name(device_name);
    if (part == NULL || mtd_partition_info(part, NULL, NULL, &write_size)) {
        LOGE("Can't find %s\n", device_name);
        return -1;
    }

    MtdReadContext *read = mtd_read_partition(part);
    if (read == NULL) {
        LOGE("Can't open %s\n(%s)\n", device_name, strerror(errno));
        return -1;
    }

    const ssize_t size = write_size * MISC_PAGES;
    char data[size];
    ssize_t r = mtd_read_data(read, data, size);
    if (r != size) LOGE("Can't read %s\n(%s)\n", device_name, strerror(errno));
    mtd_read_close(read);
    if (r != size) return -1;

    memcpy(out, &data[write_size * MISC_COMMAND_PAGE], sizeof(*out));
    return 0;
}

int set_bootloader_message_mtd_name(const struct bootloader_message *in,
                                      const char* mtd_name) {
    size_t write_size;
    mtd_scan_partitions();
    const MtdPartition *part = mtd_find_partition_by_name(mtd_name);
    if (part == NULL || mtd_partition_info(part, NULL, NULL, &write_size)) {
        printf("Can't find %s\n", mtd_name);
        return -1;
    }

    MtdReadContext *read = mtd_read_partition(part);
    if (read == NULL) {
        printf("Can't open %s\n(%s)\n", mtd_name, strerror(errno));
        return -1;
    }

    ssize_t size = write_size * MISC_PAGES;
    char data[size];
    ssize_t r = mtd_read_data(read, data, size);
    if (r != size) printf("Can't read %s\n(%s)\n", mtd_name, strerror(errno));
    mtd_read_close(read);
    if (r != size) return -1;

    memcpy(&data[write_size * MISC_COMMAND_PAGE], in, sizeof(*in));

    MtdWriteContext *write = mtd_write_partition(part);
    if (write == NULL) {
        printf("Can't open %s\n(%s)\n", mtd_name, strerror(errno));
        return -1;
    }
    if (mtd_write_data(write, data, size) != size) {
        printf("Can't write %s\n(%s)\n", mtd_name, strerror(errno));
        mtd_write_close(write);
        return -1;
    }
    if (mtd_write_close(write)) {
        printf("Can't finish %s\n(%s)\n", mtd_name, strerror(errno));
        return -1;
    }

    printf("Set boot command \"%s\"\n", in->command[0] != 255 ? in->command : "");
    return 0;
}

// ------------------------------------
// for misc partitions on block devices
// ------------------------------------

static void wait_for_device(const char* fn) {
    int tries = 0;
    int ret;
    struct stat buf;
    do {
        ++tries;
        ret = stat(fn, &buf);
        if (ret) {
            printf("stat %s try %d: %s\n", fn, tries, strerror(errno));
            sleep(1);
        }
    } while (ret && tries < 10);
    if (ret) {
        printf("failed to stat %s\n", fn);
    }
}
/*
static int get_bootloader_message_block(struct bootloader_message *out,
                                        const Volume* v) {
    wait_for_device(v->blk_device);
    FILE* f = fopen(v->blk_device, "rb");
    if (f == NULL) {
        LOGE("Can't open %s\n(%s)\n", v->blk_device, strerror(errno));
        return -1;
    }
#ifdef BOARD_RECOVERY_BLDRMSG_OFFSET
    fseek(f, BOARD_RECOVERY_BLDRMSG_OFFSET, SEEK_SET);
#endif
    struct bootloader_message temp;
    int count = fread(&temp, sizeof(temp), 1, f);
    if (count != 1) {
        LOGE("Failed reading %s\n(%s)\n", v->blk_device, strerror(errno));
        return -1;
    }
    if (fclose(f) != 0) {
        LOGE("Failed closing %s\n(%s)\n", v->blk_device, strerror(errno));
        return -1;
    }
    memcpy(out, &temp, sizeof(temp));
    return 0;
}

static int set_bootloader_message_block(const struct bootloader_message *in,
                                        const Volume* v) {
    wait_for_device(v->blk_device);
    FILE* f = fopen(v->blk_device, "rb+");
    if (f == NULL) {
        LOGE("Can't open %s\n(%s)\n", v->blk_device, strerror(errno));
        return -1;
    }
#ifdef BOARD_RECOVERY_BLDRMSG_OFFSET
    fseek(f, BOARD_RECOVERY_BLDRMSG_OFFSET, SEEK_SET);
#endif
    int count = fwrite(in, sizeof(*in), 1, f);
    if (count != 1) {
        LOGE("Failed writing %s\n(%s)\n", v->blk_device, strerror(errno));
        return -1;
    }
    if (fclose(f) != 0) {
        LOGE("Failed closing %s\n(%s)\n", v->blk_device, strerror(errno));
        return -1;
    }
    return 0;
}
*/

int get_bootloader_message_block_name(struct bootloader_message *out) {
    wait_for_device(device_name);
    FILE* f = fopen(device_name, "rb");
    if (f == NULL) {
        LOGE("Can't open %s\n(%s)\n", device_name, strerror(errno));
        return -1;
    }
#ifdef BOARD_RECOVERY_BLDRMSG_OFFSET
    fseek(f, BOARD_RECOVERY_BLDRMSG_OFFSET, SEEK_SET);
#endif
    struct bootloader_message temp;
    int count = fread(&temp, sizeof(temp), 1, f);
    if (count != 1) {
        LOGE("Failed reading %s\n(%s)\n", device_name, strerror(errno));
        return -1;
    }
    if (fclose(f) != 0) {
        LOGE("Failed closing %s\n(%s)\n", device_name, strerror(errno));
        return -1;
    }
    memcpy(out, &temp, sizeof(temp));
    return 0;
}

int set_bootloader_message_block_name(const struct bootloader_message *in,
                                        const char* block_name) {
    wait_for_device(block_name);
    FILE* f = fopen(block_name, "rb+");
    if (f == NULL) {
        printf("Can't open %s\n(%s)\n", block_name, strerror(errno));
        return -1;
    }
#ifdef BOARD_RECOVERY_BLDRMSG_OFFSET
    fseek(f, BOARD_RECOVERY_BLDRMSG_OFFSET, SEEK_SET);
#endif
    int count = fwrite(in, sizeof(*in), 1, f);
    if (count != 1) {
        printf("Failed writing %s\n(%s)\n", block_name, strerror(errno));
        return -1;
    }
    if (fclose(f) != 0) {
        printf("Failed closing %s\n(%s)\n", block_name, strerror(errno));
        return -1;
    }
    return 0;
}

static const char *COMMAND_FILE = "/cache/recovery/command";
static const int MAX_ARG_LENGTH = 4096;
static const int MAX_ARGS = 100;

// command line args come from, in decreasing precedence:
//   - the actual command line
//   - the bootloader control block (one per line, after "recovery")
//   - the contents of COMMAND_FILE (one per line)
void
get_args(int *argc, char ***argv) {
    struct bootloader_message boot;
    memset(&boot, 0, sizeof(boot));
    get_bootloader_message(&boot);  // this may fail, leaving a zeroed structure

    if (boot.command[0] != 0 && boot.command[0] != 255) {
        LOGI("Boot command: %.*s\n", sizeof(boot.command), boot.command);
    }

    if (boot.status[0] != 0 && boot.status[0] != 255) {
        LOGI("Boot status: %.*s\n", sizeof(boot.status), boot.status);
    }

    // --- if arguments weren't supplied, look in the bootloader control block
    if (*argc <= 1) {
        boot.recovery[sizeof(boot.recovery) - 1] = '\0';  // Ensure termination
        const char *arg = strtok(boot.recovery, "\n");
        if (arg != NULL && !strcmp(arg, "recovery")) {
            *argv = (char **) malloc(sizeof(char *) * MAX_ARGS);
            (*argv)[0] = strdup(arg);
            for (*argc = 1; *argc < MAX_ARGS; ++*argc) {
                if ((arg = strtok(NULL, "\n")) == NULL) break;
                (*argv)[*argc] = strdup(arg);
            }
            LOGI("Got arguments from boot message\n");
        } else if (boot.recovery[0] != 0 && boot.recovery[0] != 255) {
            LOGE("Bad boot message\n\"%.20s\"\n", boot.recovery);
        }
    }

    // --- if that doesn't work, try the command file
    if (*argc <= 1) {
        FILE *fp = fopen(COMMAND_FILE, "r");
        if (fp != NULL) {
            char *argv0 = (*argv)[0];
            *argv = (char **) malloc(sizeof(char *) * MAX_ARGS);
            (*argv)[0] = argv0;  // use the same program name

            char buf[MAX_ARG_LENGTH];
            for (*argc = 1; *argc < MAX_ARGS; ++*argc) {
                if (!fgets(buf, sizeof(buf), fp)) break;
                (*argv)[*argc] = strdup(strtok(buf, "\r\n"));  // Strip newline.
            }

            fflush(fp);
		    if (ferror(fp)) LOGE("Error in %s\n(%s)\n", COMMAND_FILE, strerror(errno));
		    fclose(fp);
            LOGI("Got arguments from %s\n", COMMAND_FILE);
        }
    }

    // --> write the arguments we have back into the bootloader control block
    // always boot into recovery after this (until finish_recovery() is called)
    strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
    strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
    int i;
    for (i = 1; i < *argc; ++i) {
        strlcat(boot.recovery, (*argv)[i], sizeof(boot.recovery));
        strlcat(boot.recovery, "\n", sizeof(boot.recovery));
    }
    set_bootloader_message(&boot);
}
