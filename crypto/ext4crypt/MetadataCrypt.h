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

#ifndef _METADATA_CRYPT_H
#define _METADATA_CRYPT_H

#include <string>
__BEGIN_DECLS
bool e4crypt_mount_metadata_encrypted(const std::string& mount_point, bool needs_encrypt, const std::string& key_dir, const std::string& blk_device, std::string* crypto_blkdev);
__END_DECLS

#endif
