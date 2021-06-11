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

#ifndef ANDROID_VOLD_KEYUTIL_H
#define ANDROID_VOLD_KEYUTIL_H

#include "KeyBuffer.h"
#include "KeyStorage4.h"
#include "Keymaster4.h"

#include <string>
#include <memory>

namespace android {
namespace vold {

bool randomKey(KeyBuffer* key);
bool installKey(const KeyBuffer& key, std::string* raw_ref);
bool evictKey(const std::string& raw_ref);
bool retrieveAndInstallKey(bool create_if_absent, const KeyAuthentication& key_authentication,
                           const std::string& key_path, const std::string& tmp_path,
                           std::string* key_ref, bool wrapped_key_supported);
bool retrieveKey(bool create_if_absent, const std::string& key_path,
                 const std::string& tmp_path, KeyBuffer* key);

}  // namespace vold
}  // namespace android

#endif
