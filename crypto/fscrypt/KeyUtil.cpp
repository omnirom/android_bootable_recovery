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

#include "KeyUtil.h"

#include <linux/fs.h>
#include <iomanip>
#include <sstream>
#include <string>

#include <openssl/sha.h>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <keyutils.h>

#include "FsCrypt.h"
#include "KeyStorage.h"
#include "Utils.h"

#define MAX_USER_ID 0xFFFFFFFF

using android::hardware::keymaster::V4_0::KeyFormat;
using android::vold::KeyType;
namespace android {
namespace vold {

constexpr int FS_AES_256_XTS_KEY_SIZE = 64;

bool randomKey(KeyBuffer* key) {
    *key = KeyBuffer(FS_AES_256_XTS_KEY_SIZE);
    if (ReadRandomBytes(key->size(), key->data()) != 0) {
        // TODO status_t plays badly with PLOG, fix it.
        LOG(ERROR) << "Random read failed";
        return false;
    }
    return true;
}

// Get raw keyref - used to make keyname and to pass to ioctl
static std::string generateKeyRef(const uint8_t* key, int length) {
    SHA512_CTX c;

    SHA512_Init(&c);
    SHA512_Update(&c, key, length);
    unsigned char key_ref1[SHA512_DIGEST_LENGTH];
    SHA512_Final(key_ref1, &c);

    SHA512_Init(&c);
    SHA512_Update(&c, key_ref1, SHA512_DIGEST_LENGTH);
    unsigned char key_ref2[SHA512_DIGEST_LENGTH];
    SHA512_Final(key_ref2, &c);

    static_assert(FS_KEY_DESCRIPTOR_SIZE <= SHA512_DIGEST_LENGTH, "Hash too short for descriptor");
    return std::string((char*)key_ref2, FS_KEY_DESCRIPTOR_SIZE);
}

static bool fillKey(const KeyBuffer& key, fscrypt_key* fs_key) {
    if (key.size() != FS_AES_256_XTS_KEY_SIZE) {
        LOG(ERROR) << "Wrong size key " << key.size();
        return false;
    }
    static_assert(FS_AES_256_XTS_KEY_SIZE <= sizeof(fs_key->raw), "Key too long!");
    fs_key->mode = FS_ENCRYPTION_MODE_AES_256_XTS;
    fs_key->size = key.size();
    memset(fs_key->raw, 0, sizeof(fs_key->raw));
    memcpy(fs_key->raw, key.data(), key.size());
    return true;
}

static char const* const NAME_PREFIXES[] = {"ext4", "f2fs", "fscrypt", nullptr};

static std::string keyname(const std::string& prefix, const std::string& raw_ref) {
    std::ostringstream o;
    o << prefix << ":";
    for (unsigned char i : raw_ref) {
        o << std::hex << std::setw(2) << std::setfill('0') << (int)i;
    }
    return o.str();
}

// Get the keyring we store all keys in
static bool fscryptKeyring(key_serial_t* device_keyring) {
    *device_keyring = keyctl_search(KEY_SPEC_SESSION_KEYRING, "keyring", "fscrypt", 0);
    if (*device_keyring == -1) {
        PLOG(ERROR) << "Unable to find device keyring";
        return false;
    }
    return true;
}

// Install password into global keyring
// Return raw key reference for use in policy
bool installKey(const KeyBuffer& key, std::string* raw_ref) {
    // Place fscrypt_key into automatically zeroing buffer.
    KeyBuffer fsKeyBuffer(sizeof(fscrypt_key));
    fscrypt_key& fs_key = *reinterpret_cast<fscrypt_key*>(fsKeyBuffer.data());

    if (!fillKey(key, &fs_key)) return false;
    if (is_wrapped_key_supported()) {
        /* When wrapped key is supported, only the first 32 bytes are
           the same per boot. The second 32 bytes can change as the ephemeral
           key is different. */
        *raw_ref = generateKeyRef(fs_key.raw, (fs_key.size)/2);
    } else {
        *raw_ref = generateKeyRef(fs_key.raw, fs_key.size);
    }
    key_serial_t device_keyring;
    if (!fscryptKeyring(&device_keyring)) return false;
    for (char const* const* name_prefix = NAME_PREFIXES; *name_prefix != nullptr; name_prefix++) {
        auto ref = keyname(*name_prefix, *raw_ref);
        key_serial_t key_id =
            add_key("logon", ref.c_str(), (void*)&fs_key, sizeof(fs_key), device_keyring);
        if (key_id == -1) {
            PLOG(ERROR) << "Failed to insert key into keyring " << device_keyring;
            return false;
        }
        LOG(DEBUG) << "Added key " << key_id << " (" << ref << ") to keyring " << device_keyring
                   << " in process " << getpid();
    }
    return true;
}

bool evictKey(const std::string& raw_ref) {
    key_serial_t device_keyring;
    if (!fscryptKeyring(&device_keyring)) return false;
    bool success = true;
    for (char const* const* name_prefix = NAME_PREFIXES; *name_prefix != nullptr; name_prefix++) {
        auto ref = keyname(*name_prefix, raw_ref);
        auto key_serial = keyctl_search(device_keyring, "logon", ref.c_str(), 0);

        // Unlink the key from the keyring.  Prefer unlinking to revoking or
        // invalidating, since unlinking is actually no less secure currently, and
        // it avoids bugs in certain kernel versions where the keyring key is
        // referenced from places it shouldn't be.
        if (keyctl_unlink(key_serial, device_keyring) != 0) {
            PLOG(ERROR) << "Failed to unlink key with serial " << key_serial << " ref " << ref;
            success = false;
        } else {
            LOG(DEBUG) << "Unlinked key with serial " << key_serial << " ref " << ref;
        }
    }
    return success;
}

bool retrieveAndInstallKey(bool create_if_absent, const KeyAuthentication& key_authentication,
                           const std::string& key_path, const std::string& tmp_path,
                           std::string* key_ref, bool wrapped_key_supported) {
    KeyBuffer key;
    if (pathExists(key_path)) {
        LOG(DEBUG) << "Key exists, using: " << key_path;
        if (!retrieveKey(key_path, key_authentication, &key)) return false;
    } else {
        if (!create_if_absent) {
            LOG(ERROR) << "No key found in " << key_path;
            return false;
        }
        LOG(INFO) << "Creating new key in " << key_path;
        if (wrapped_key_supported) {
            if(!generateWrappedKey(MAX_USER_ID, KeyType::DE_SYS, &key)) return false;
        } else {
            if (!randomKey(&key)) return false;
        }
        if (!storeKeyAtomically(key_path, tmp_path, key_authentication, key)) return false;
    }

    if (wrapped_key_supported) {
        KeyBuffer ephemeral_wrapped_key;
        if (!getEphemeralWrappedKey(KeyFormat::RAW, key, &ephemeral_wrapped_key)) {
            LOG(ERROR) << "Failed to export key in retrieveAndInstallKey";
            return false;
        }
        key = std::move(ephemeral_wrapped_key);
    }

    if (!installKey(key, key_ref)) {
        LOG(ERROR) << "Failed to install key in " << key_path;
        return false;
    }
    return true;
}

bool retrieveKey(bool create_if_absent, const std::string& key_path, const std::string& tmp_path,
                 KeyBuffer* key, bool keepOld) {
    LOG(ERROR) << "retreiveKey1";
    if (pathExists(key_path)) {
        LOG(ERROR) << "Key exists, using: " << key_path;
        if (!retrieveKey(key_path, kEmptyAuthentication, key, keepOld)) return false;
			if (is_metadata_wrapped_key_supported()) {
				KeyBuffer ephemeral_wrapped_key;
				if (!getEphemeralWrappedKey(KeyFormat::RAW, *key, &ephemeral_wrapped_key)) {
					LOG(ERROR) << "Failed to export key for retrieved key";
					return false;
				}
				*key = std::move(ephemeral_wrapped_key);
			}
    } else {
        if (!create_if_absent) {
            LOG(ERROR) << "No key found in " << key_path;
            return false;
        }
        LOG(ERROR) << "Creating new key in " << key_path;
        if (is_metadata_wrapped_key_supported()) {
            if(!generateWrappedKey(MAX_USER_ID, KeyType::ME, key)) return false;
        } else {
            if (!randomKey(key)) return false;
        }
		LOG(ERROR) << "retrieveKey1";
        if (!storeKeyAtomically(key_path, tmp_path,
                kEmptyAuthentication, *key)) return false;
	if (is_metadata_wrapped_key_supported()) {
            KeyBuffer ephemeral_wrapped_key;
            if (!getEphemeralWrappedKey(KeyFormat::RAW, *key, &ephemeral_wrapped_key)) {
                LOG(ERROR) << "Failed to export key for generated key";
                return false;
            }
            *key = std::move(ephemeral_wrapped_key);
        }
    }
    return true;
}

}  // namespace vold
}  // namespace android
