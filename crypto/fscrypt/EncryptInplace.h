/*
 * Copyright (C) 2016 The Android Open Source Project
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

#ifndef _ENCRYPT_INPLACE_H
#define _ENCRYPT_INPLACE_H

#include <sys/types.h>

#define CRYPT_INPLACE_BUFSIZE 4096
#define CRYPT_SECTOR_SIZE 512
#define RETRY_MOUNT_ATTEMPTS 10
#define RETRY_MOUNT_DELAY_SECONDS 1

int cryptfs_enable_inplace(const char* crypto_blkdev, const char* real_blkdev, off64_t size,
                           off64_t* size_already_done, off64_t tot_size,
                           off64_t previously_encrypted_upto, bool set_progress_properties);

#endif
