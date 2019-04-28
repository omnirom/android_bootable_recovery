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

#include <stdbool.h>
#include <sys/cdefs.h>

#include <cutils/multiuser.h>

#include <map>
#include <string>

__BEGIN_DECLS

// General functions
bool e4crypt_is_native();
bool e4crypt_initialize_global_de();

bool e4crypt_init_user0();
//bool e4crypt_vold_create_user_key(userid_t user_id, int serial, bool ephemeral);
//bool e4crypt_destroy_user_key(userid_t user_id);
//bool e4crypt_add_user_key_auth(userid_t user_id, int serial, const char* token,
//                               const char* secret);
//bool e4crypt_fixate_newest_user_key_auth(userid_t user_id);

bool e4crypt_unlock_user_key(userid_t user_id, int serial, const char* token, const char* secret);
//bool e4crypt_lock_user_key(userid_t user_id);

//bool e4crypt_prepare_user_storage(const char* volume_uuid, userid_t user_id, int serial, int flags);
//bool e4crypt_destroy_user_storage(const char* volume_uuid, userid_t user_id, int flags);

bool lookup_key_ref(const std::map<userid_t, std::string>& key_map, userid_t user_id,
                           std::string* raw_ref);

__END_DECLS
