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

#ifndef ANDROID_TWRP_KEYSTORAGE_H
#define ANDROID_TWRP_KEYSTORAGE_H

#include "Keymaster4.h"
#include "KeyBuffer.h"
#include <ext4_utils/ext4_crypt.h>

#include <string>

namespace android {
namespace vold {

namespace km = ::android::hardware::keymaster::V4_0;

// Represents the information needed to decrypt a disk encryption key.
// If "token" is nonempty, it is passed in as a required Gatekeeper auth token.
// If "token" and "secret" are nonempty, "secret" is appended to the application-specific
// binary needed to unlock.
// If only "secret" is nonempty, it is used to decrypt in a non-Keymaster process.
class KeyAuthentication {
  public:
    KeyAuthentication(std::string t, std::string s) : token{t}, secret{s} {};

    bool usesKeymaster() const { return !token.empty() || secret.empty(); };

    const std::string token;
    const std::string secret;
};

enum class KeyType {
    DE_SYS,
    DE_USER,
    CE_USER
};

extern const KeyAuthentication kEmptyAuthentication;

// Checks if path "path" exists.
bool pathExists(const std::string& path);

bool createSecdiscardable(const std::string& path, std::string* hash);
bool readSecdiscardable(const std::string& path, std::string* hash);

// Create a directory at the named path, and store "key" in it,
// in such a way that it can only be retrieved via Keymaster and
// can be securely deleted.
// It's safe to move/rename the directory after creation.
bool storeKey(const std::string& dir, const KeyAuthentication& auth, const KeyBuffer& key);

// Create a directory at the named path, and store "key" in it as storeKey
// This version creates the key in "tmp_path" then atomically renames "tmp_path"
// to "key_path" thereby ensuring that the key is either stored entirely or
// not at all.
bool storeKeyAtomically(const std::string& key_path, const std::string& tmp_path,
                        const KeyAuthentication& auth, const KeyBuffer& key);

// Retrieve the key from the named directory.
bool retrieveKey(const std::string& dir, const KeyAuthentication& auth, KeyBuffer* key);

// Securely destroy the key stored in the named directory and delete the directory.
bool destroyKey(const std::string& dir);

bool runSecdiscardSingle(const std::string& file);

bool generateWrappedKey(userid_t user_id, KeyType key_type, KeyBuffer* key);
bool getEphemeralWrappedKey(km::KeyFormat format, KeyBuffer& kmKey, KeyBuffer* key);
}  // namespace vold
}  // namespace android

#endif
