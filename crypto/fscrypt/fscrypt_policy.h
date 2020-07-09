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

#ifndef _FS_CRYPT_H_
#define _FS_CRYPT_H_

#include <sys/cdefs.h>
#include <stdbool.h>
#include <cutils/multiuser.h>
#include <linux/fs.h>

__BEGIN_DECLS

#define FS_KEY_DESCRIPTOR_SIZE_HEX (2 * FS_KEY_DESCRIPTOR_SIZE + 1)

/* modes not supported by upstream kernel, so not in <linux/fs.h> */
#define FS_ENCRYPTION_MODE_AES_256_HEH      126
#define FS_ENCRYPTION_MODE_PRIVATE          127

/* new definition, not yet in Bionic's <linux/fs.h> */
#ifndef FS_ENCRYPTION_MODE_ADIANTUM
#define FS_ENCRYPTION_MODE_ADIANTUM         9
#endif

/* new definition, not yet in Bionic's <linux/fs.h> */
#ifndef FS_POLICY_FLAG_DIRECT_KEY
#define FS_POLICY_FLAG_DIRECT_KEY           0x4
#endif

#define HEX_LOOKUP "0123456789abcdef"

struct fscrypt_encryption_policy {
  uint8_t version;
  uint8_t contents_encryption_mode;
  uint8_t filenames_encryption_mode;
  uint8_t flags;
  uint8_t master_key_descriptor[FS_KEY_DESCRIPTOR_SIZE];
} __attribute__((packed));


bool fscrypt_set_mode();
bool lookup_ref_key(const uint8_t *policy, uint8_t* policy_type);
bool lookup_ref_tar(const uint8_t *policy_type, uint8_t *policy);
void policy_to_hex(const uint8_t* policy, char* hex);
bool fscrypt_policy_get_struct(const char *directory, struct fscrypt_encryption_policy *fep);
bool fscrypt_policy_set_struct(const char *directory, const struct fscrypt_encryption_policy *fep);
void fscrypt_policy_fill_default_struct(struct fscrypt_encryption_policy *fep);
__END_DECLS

#endif // _FS_CRYPT_H_
