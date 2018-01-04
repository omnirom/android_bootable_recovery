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

#ifndef ANDROID_VOLD_KEYSTORAGE3_H
#define ANDROID_VOLD_KEYSTORAGE3_H

#include <string>
#include "KeyStorage.h"

namespace android {
namespace vold {

extern const KeyAuthentication kEmptyAuthentication;

// Create a directory at the named path, and store "key" in it,
// in such a way that it can only be retrieved via Keymaster and
// can be securely deleted.
// It's safe to move/rename the directory after creation.
//bool storeKey(const std::string& dir, const KeyAuthentication& auth, const std::string& key);

// Retrieve the key from the named directory.
bool retrieveKey3(const std::string& dir, const KeyAuthentication& auth, std::string* key);

// Securely destroy the key stored in the named directory and delete the directory.
bool destroyKey3(const std::string& dir);

bool runSecdiscardSingle(const std::string& file);
}  // namespace vold
}  // namespace android

#endif
