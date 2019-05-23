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

#ifndef ANDROID_TWRP_KEYMASTER_H
#define ANDROID_TWRP_KEYMASTER_H

#include "KeyBuffer.h"

#include <memory>
#include <string>
#include <utility>

#include <android-base/macros.h>
#include <keymasterV4_0/Keymaster.h>
#include <keymasterV4_0/authorization_set.h>

namespace android {
namespace vold {

namespace km = ::android::hardware::keymaster::V4_0;
using KmDevice = km::support::Keymaster;

// C++ wrappers to the Keymaster hidl interface.
// This is tailored to the needs of KeyStorage, but could be extended to be
// a more general interface.

// Wrapper for a Keymaster operation handle representing an
// ongoing Keymaster operation.  Aborts the operation
// in the destructor if it is unfinished. Methods log failures
// to LOG(ERROR).
class KeymasterOperation {
  public:
    ~KeymasterOperation();
    // Is this instance valid? This is false if creation fails, and becomes
    // false on finish or if an update fails.
    explicit operator bool() { return mError == km::ErrorCode::OK; }
    km::ErrorCode errorCode() { return mError; }
    // Call "update" repeatedly until all of the input is consumed, and
    // concatenate the output. Return true on success.
    template <class TI, class TO>
    bool updateCompletely(TI& input, TO* output) {
        if (output) output->clear();
        return updateCompletely(input.data(), input.size(), [&](const char* b, size_t n) {
            if (output) std::copy(b, b + n, std::back_inserter(*output));
        });
    }

    // Finish and write the output to this string, unless pointer is null.
    bool finish(std::string* output);
    // Move constructor
    KeymasterOperation(KeymasterOperation&& rhs) { *this = std::move(rhs); }
    // Construct an object in an error state for error returns
    KeymasterOperation() : mDevice{nullptr}, mOpHandle{0}, mError{km::ErrorCode::UNKNOWN_ERROR} {}
    // Move Assignment
    KeymasterOperation& operator=(KeymasterOperation&& rhs) {
        mDevice = rhs.mDevice;
        rhs.mDevice = nullptr;

        mOpHandle = rhs.mOpHandle;
        rhs.mOpHandle = 0;

        mError = rhs.mError;
        rhs.mError = km::ErrorCode::UNKNOWN_ERROR;

        return *this;
    }

  private:
    KeymasterOperation(KmDevice* d, uint64_t h)
        : mDevice{d}, mOpHandle{h}, mError{km::ErrorCode::OK} {}
    KeymasterOperation(km::ErrorCode error) : mDevice{nullptr}, mOpHandle{0}, mError{error} {}

    bool updateCompletely(const char* input, size_t inputLen,
                          const std::function<void(const char*, size_t)> consumer);

    KmDevice* mDevice;
    uint64_t mOpHandle;
    km::ErrorCode mError;
    DISALLOW_COPY_AND_ASSIGN(KeymasterOperation);
    friend class Keymaster;
};

// Wrapper for a Keymaster device for methods that start a KeymasterOperation or are not
// part of one.
class Keymaster {
  public:
    Keymaster();
    // false if we failed to open the keymaster device.
    explicit operator bool() { return mDevice.get() != nullptr; }
    // Generate a key in the keymaster from the given params.
    bool generateKey(const km::AuthorizationSet& inParams, std::string* key);
    // Export a key from keymaster.
    bool exportKey(km::KeyFormat format, KeyBuffer& kmKey, const std::string& clientId,
                   const std::string& appData, std::string* key);
    // If the keymaster supports it, permanently delete a key.
    bool deleteKey(const std::string& key);
    // Replace stored key blob in response to KM_ERROR_KEY_REQUIRES_UPGRADE.
    bool upgradeKey(const std::string& oldKey, const km::AuthorizationSet& inParams,
                    std::string* newKey);
    // Begin a new cryptographic operation, collecting output parameters if pointer is non-null
    KeymasterOperation begin(km::KeyPurpose purpose, const std::string& key,
                             const km::AuthorizationSet& inParams,
                             const km::HardwareAuthToken& authToken,
                             km::AuthorizationSet* outParams);
    bool isSecure();

  private:
    std::unique_ptr<KmDevice> mDevice;
    DISALLOW_COPY_AND_ASSIGN(Keymaster);
    static bool hmacKeyGenerated;
};

}  // namespace vold
}  // namespace android

// FIXME no longer needed now cryptfs is in C++.

/*
 * The following functions provide C bindings to keymaster services
 * needed by cryptfs scrypt. The compatibility check checks whether
 * the keymaster implementation is considered secure, i.e., TEE backed.
 * The create_key function generates an RSA key for signing.
 * The sign_object function signes an object with the given keymaster
 * key.
 */

/* Return values for keymaster_sign_object_for_cryptfs_scrypt */

enum class KeymasterSignResult {
    ok = 0,
    error = -1,
    upgrade = -2,
};

//int keymaster_compatibility_cryptfs_scrypt();
/*int keymaster_create_key_for_cryptfs_scrypt(uint32_t rsa_key_size, uint64_t rsa_exponent,
                                            uint32_t ratelimit, uint8_t* key_buffer,
                                            uint32_t key_buffer_size, uint32_t* key_out_size);*/

int keymaster_upgrade_key_for_cryptfs_scrypt(uint32_t rsa_key_size, uint64_t rsa_exponent,
                                             uint32_t ratelimit, const uint8_t* key_blob,
                                             size_t key_blob_size, uint8_t* key_buffer,
                                             uint32_t key_buffer_size, uint32_t* key_out_size);

KeymasterSignResult keymaster_sign_object_for_cryptfs_scrypt(
    const uint8_t* key_blob, size_t key_blob_size, uint32_t ratelimit, const uint8_t* object,
    const size_t object_size, uint8_t** signature_buffer, size_t* signature_buffer_size);

#endif
