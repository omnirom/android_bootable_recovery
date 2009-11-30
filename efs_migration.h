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

#ifndef __EFS_MIGRATION_H__
#define __EFS_MIGRATION_H__

#define MODE_ENCRYPTEDFS_DISABLED 0
#define MODE_ENCRYPTEDFS_ENABLED  1

#define EFS_OK            0
#define EFS_ERROR       (-1)

struct encrypted_fs_info {
    int encrypted_fs_mode;
    char *encrypted_fs_key;
};

typedef struct encrypted_fs_info encrypted_fs_info;

int read_encrypted_fs_info(encrypted_fs_info *efs_data);

int restore_encrypted_fs_info(encrypted_fs_info *efs_data);

#endif /* __EFS_MIGRATION_H__ */

