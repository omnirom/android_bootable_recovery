/*
 * Copyright (C) 2009 The Android Open Source Project
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

#include <stdio.h>

#ifndef __ENCRYPTEDFS_PROVISIONING_H__
#define __ENCRYPTEDFS_PROVISIONING_H__

#define MODE_ENCRYPTED_FS_DISABLED    0
#define MODE_ENCRYPTED_FS_ENABLED     1

#define ENCRYPTED_FS_OK               0
#define ENCRYPTED_FS_ERROR          (-1)

#define ENCRYPTED_FS_KEY_SIZE        16
#define ENCRYPTED_FS_SALT_SIZE       16
#define ENCRYPTED_FS_MAX_HASH_SIZE  128
#define ENTROPY_MAX_SIZE        4096

struct encrypted_fs_info {
    int mode;
    char key[ENCRYPTED_FS_KEY_SIZE];
    char salt[ENCRYPTED_FS_SALT_SIZE];
    int salt_length;
    char hash[ENCRYPTED_FS_MAX_HASH_SIZE];
    int hash_length;
    char entropy[ENTROPY_MAX_SIZE];
    int entropy_length;
};

typedef struct encrypted_fs_info encrypted_fs_info;

int read_encrypted_fs_info(encrypted_fs_info *secure_fs_data);

int restore_encrypted_fs_info(encrypted_fs_info *secure_data);

#endif /* __ENCRYPTEDFS_PROVISIONING_H__ */

