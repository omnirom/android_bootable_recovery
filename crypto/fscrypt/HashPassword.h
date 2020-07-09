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

#ifndef __HASH_PASSWORD_H
#define __HASH_PASSWORD_H

#include <string>

#define FBE_PERSONALIZATION "Android FBE credential hash"
#define PERSONALISATION_WEAVER_KEY "weaver-key"
#define PERSONALISATION_WEAVER_PASSWORD "weaver-pwd"
#define PERSONALISATION_APPLICATION_ID "application-id"
#define PERSONALIZATION_FBE_KEY "fbe-key"
#define PERSONALIZATION_USER_GK_AUTH "user-gk-authentication"
#define PERSONALISATION_SECDISCARDABLE "secdiscardable-transform"
#define PERSONALISATION_CONTEXT "android-synthetic-password-personalization-context"

void* PersonalizedHashBinary(const char* prefix, const char* key, const size_t key_size);

std::string PersonalizedHash(const char* prefix, const char* key, const size_t key_size);
std::string PersonalizedHash(const char* prefix, const std::string& Password);
std::string PersonalizedHashSP800(const char* label, const char* context, const char* key, const size_t key_size);
std::string HashPassword(const std::string& Password);

template <class T>
void endianswap(T *objp) {
	unsigned char *memp = reinterpret_cast<unsigned char*>(objp);
	std::reverse(memp, memp + sizeof(T));
}

#endif
