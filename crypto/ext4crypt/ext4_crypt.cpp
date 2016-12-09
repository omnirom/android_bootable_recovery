/*
 * Copyright (C) 2015 The Android Open Source Project
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

/* TWRP NOTE: Kanged from system/extras/ext4_utils/ext4_crypt.cpp
 * because policy_to_hex, e4crypt_policy_set, and e4crypt_policy_get
 * are not exposed to be used. There was also a bug in e4crypt_policy_get
 * that may or may not be fixed in the user's local repo:
 * https://android.googlesource.com/platform/system/extras/+/30b93dd5715abcabd621235733733c0503f9c552
 */

#include "ext4_crypt.h"

#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <fcntl.h>
#include <asm/ioctl.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <cutils/properties.h>

#define XATTR_NAME_ENCRYPTION_POLICY "encryption.policy"
#define EXT4_KEYREF_DELIMITER ((char)'.')

// ext4enc:TODO Include structure from somewhere sensible
// MUST be in sync with ext4_crypto.c in kernel
#define EXT4_KEY_DESCRIPTOR_SIZE 8
#define EXT4_KEY_DESCRIPTOR_SIZE_HEX 17

struct ext4_encryption_policy {
    char version;
    char contents_encryption_mode;
    char filenames_encryption_mode;
    char flags;
    char master_key_descriptor[EXT4_KEY_DESCRIPTOR_SIZE];
} __attribute__((__packed__));

#define EXT4_ENCRYPTION_MODE_AES_256_XTS    1
#define EXT4_ENCRYPTION_MODE_AES_256_CTS    4
#define EXT4_ENCRYPTION_MODE_PRIVATE        127

static int encryption_mode = EXT4_ENCRYPTION_MODE_PRIVATE;

// ext4enc:TODO Get value from somewhere sensible
#define EXT4_IOC_SET_ENCRYPTION_POLICY _IOR('f', 19, struct ext4_encryption_policy)
#define EXT4_IOC_GET_ENCRYPTION_POLICY _IOW('f', 21, struct ext4_encryption_policy)

#define HEX_LOOKUP "0123456789abcdef"

extern "C" void policy_to_hex(const char* policy, char* hex) {
    for (size_t i = 0, j = 0; i < EXT4_KEY_DESCRIPTOR_SIZE; i++) {
        hex[j++] = HEX_LOOKUP[(policy[i] & 0xF0) >> 4];
        hex[j++] = HEX_LOOKUP[policy[i] & 0x0F];
    }
    hex[EXT4_KEY_DESCRIPTOR_SIZE_HEX - 1] = '\0';
}

extern "C" bool e4crypt_policy_set(const char *directory, const char *policy,
                               size_t policy_length, int contents_encryption_mode) {
    if (contents_encryption_mode == 0)
        contents_encryption_mode = encryption_mode;
    if (policy_length != EXT4_KEY_DESCRIPTOR_SIZE) {
		printf("policy wrong length\n");
        LOG(ERROR) << "Policy wrong length: " << policy_length;
        return false;
    }
    int fd = open(directory, O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd == -1) {
		printf("failed to open %s\n", directory);
        PLOG(ERROR) << "Failed to open directory " << directory;
        return false;
    }

    ext4_encryption_policy eep;
    eep.version = 0;
    eep.contents_encryption_mode = contents_encryption_mode;
    eep.filenames_encryption_mode = EXT4_ENCRYPTION_MODE_AES_256_CTS;
    eep.flags = 0;
    memcpy(eep.master_key_descriptor, policy, EXT4_KEY_DESCRIPTOR_SIZE);
    if (ioctl(fd, EXT4_IOC_SET_ENCRYPTION_POLICY, &eep)) {
		printf("failed to set policy for '%s' '%s'\n", directory, policy);
        PLOG(ERROR) << "Failed to set encryption policy for " << directory;
        close(fd);
        return false;
    }
    close(fd);

    char policy_hex[EXT4_KEY_DESCRIPTOR_SIZE_HEX];
    policy_to_hex(policy, policy_hex);
    LOG(INFO) << "Policy for " << directory << " set to " << policy_hex;
    return true;
}

extern "C" bool e4crypt_policy_get(const char *directory, char *policy,
                               size_t policy_length, int contents_encryption_mode) {
    if (contents_encryption_mode == 0)
        contents_encryption_mode = encryption_mode;
    if (policy_length != EXT4_KEY_DESCRIPTOR_SIZE) {
        LOG(ERROR) << "Policy wrong length: " << policy_length;
        return false;
    }

    int fd = open(directory, O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd == -1) {
        PLOG(ERROR) << "Failed to open directory " << directory;
        return false;
    }

    ext4_encryption_policy eep;
    memset(&eep, 0, sizeof(ext4_encryption_policy));
    if (ioctl(fd, EXT4_IOC_GET_ENCRYPTION_POLICY, &eep) != 0) {
        PLOG(ERROR) << "Failed to get encryption policy for " << directory;
        close(fd);
        return false;
    }
    close(fd);

    if ((eep.version != 0)
            || (eep.contents_encryption_mode != contents_encryption_mode)
            || (eep.filenames_encryption_mode != EXT4_ENCRYPTION_MODE_AES_256_CTS)
            || (eep.flags != 0)) {
        LOG(ERROR) << "Failed to find matching encryption policy for " << directory;
        return false;
    }
    memcpy(policy, eep.master_key_descriptor, EXT4_KEY_DESCRIPTOR_SIZE);

    return true;
}

extern "C" bool e4crypt_set_mode() {
    const char* mode_file = "/data/unencrypted/mode";
    struct stat st;
    if (stat(mode_file, &st) != 0 || st.st_size <= 0) {
        printf("Invalid encryption mode file %s\n", mode_file);
        return false;
    }
    size_t mode_size = st.st_size;
    char contents_encryption_mode[mode_size + 1];
    memset((void*)contents_encryption_mode, 0, mode_size + 1);
    int fd = open(mode_file, O_RDONLY);
    if (fd < 0) {
        printf("error opening '%s': %s\n", mode_file, strerror(errno));
        return false;
    }
    if (read(fd, contents_encryption_mode, mode_size) != mode_size) {
        printf("read error on '%s': %s\n", mode_file, strerror(errno));
        close(fd);
        return false;
    }
    close(fd);
    if (!strcmp(contents_encryption_mode, "software")) {
        encryption_mode = EXT4_ENCRYPTION_MODE_AES_256_XTS;
    } else if (!strcmp(contents_encryption_mode, "ice")) {
        encryption_mode = EXT4_ENCRYPTION_MODE_PRIVATE;
    } else {
        printf("Invalid encryption mode '%s'\n", contents_encryption_mode);
        return false;
    }
    printf("set encryption mode to %i\n", encryption_mode);
    return true;
}
