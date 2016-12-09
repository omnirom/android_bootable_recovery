/*
 * Copyright (C) 2016 Team Win Recovery Project
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

#ifndef __EXT4CRYPT_TAR_H
#define __EXT4CRYPT_TAR_H

#include <sys/cdefs.h>
#include <stdbool.h>
#include <cutils/multiuser.h>

__BEGIN_DECLS

bool lookup_ref_key(const char* policy, char* policy_type);
bool lookup_ref_tar(const char* policy_type, char* policy);

void policy_to_hex(const char* policy, char* hex);
bool e4crypt_policy_set(const char *directory, const char *policy,
                               size_t policy_length, int contents_encryption_mode);
bool e4crypt_policy_get(const char *directory, char *policy,
                               size_t policy_length, int contents_encryption_mode);

bool e4crypt_set_mode();
__END_DECLS

#endif
