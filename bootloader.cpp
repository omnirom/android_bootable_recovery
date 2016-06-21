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

#include <android-base/file.h>
#include <android-base/unique_fd.h>

#include "bootloader.h"
#include "common.h"
#include "roots.h"

static bool read_misc_partition(const Volume* v, size_t offset, size_t size, std::string* out);
static bool write_misc_partition(const Volume* v, size_t offset, const std::string& in);

int get_bootloader_message(bootloader_message* out) {
    Volume* v = volume_for_path("/misc");
    if (v == nullptr) {
        LOGE("Cannot load volume /misc!\n");
        return -1;
    }
    if (strcmp(v->fs_type, "emmc") == 0) {
        std::string s;
        if (!read_misc_partition(v, BOOTLOADER_MESSAGE_OFFSET_IN_MISC, sizeof(bootloader_message),
                                 &s)) {
            return -1;
        }
        memcpy(out, s.data(), s.size());
        return 0;
    }
    LOGE("Unknown misc partition fs_type \"%s\"\n", v->fs_type);
    return -1;
}

bool read_wipe_package(size_t size, std::string* out) {
    Volume* v = volume_for_path("/misc");
    if (v == nullptr) {
        LOGE("Cannot load volume /misc!\n");
        return false;
    }
    if (strcmp(v->fs_type, "mtd") == 0) {
        LOGE("Read wipe package on mtd is not supported.\n");
        return false;
    } else if (strcmp(v->fs_type, "emmc") == 0) {
        return read_misc_partition(v, WIPE_PACKAGE_OFFSET_IN_MISC, size, out);
    }
    LOGE("Unknown misc partition fs_type \"%s\"\n", v->fs_type);
    return false;
}

int set_bootloader_message(const bootloader_message* in) {
    Volume* v = volume_for_path("/misc");
    if (v == nullptr) {
        LOGE("Cannot load volume /misc!\n");
        return -1;
    }
    if (strcmp(v->fs_type, "emmc") == 0) {
        std::string s(reinterpret_cast<const char*>(in), sizeof(*in));
        bool success = write_misc_partition(v, BOOTLOADER_MESSAGE_OFFSET_IN_MISC, s);
        return success ? 0 : -1;
    }
    LOGE("Unknown misc partition fs_type \"%s\"\n", v->fs_type);
    return -1;
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

static bool read_misc_partition(const Volume* v, size_t offset, size_t size, std::string* out) {
    wait_for_device(v->blk_device);
    android::base::unique_fd fd(open(v->blk_device, O_RDONLY));
    if (fd == -1) {
        LOGE("Failed to open \"%s\": %s\n", v->blk_device, strerror(errno));
        return false;
    }
    if (lseek(fd, static_cast<off_t>(offset), SEEK_SET) != static_cast<off_t>(offset)) {
        LOGE("Failed to lseek \"%s\": %s\n", v->blk_device, strerror(errno));
        return false;
    }
    out->resize(size);
    if (!android::base::ReadFully(fd, &(*out)[0], size)) {
        LOGE("Failed to read \"%s\": %s\n", v->blk_device, strerror(errno));
        return false;
    }
    return true;
}

static bool write_misc_partition(const Volume* v, size_t offset, const std::string& in) {
    wait_for_device(v->blk_device);
    android::base::unique_fd fd(open(v->blk_device, O_WRONLY | O_SYNC));
    if (fd == -1) {
        LOGE("Failed to open \"%s\": %s\n", v->blk_device, strerror(errno));
        return false;
    }
    if (lseek(fd, static_cast<off_t>(offset), SEEK_SET) != static_cast<off_t>(offset)) {
        LOGE("Failed to lseek \"%s\": %s\n", v->blk_device, strerror(errno));
        return false;
    }
    if (!android::base::WriteFully(fd, in.data(), in.size())) {
        LOGE("Failed to write \"%s\": %s\n", v->blk_device, strerror(errno));
        return false;
    }

    if (fsync(fd) == -1) {
        LOGE("Failed to fsync \"%s\": %s\n", v->blk_device, strerror(errno));
        return false;
    }
    return true;
}
