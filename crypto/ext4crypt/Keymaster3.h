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

#ifndef ANDROID_VOLD_KEYMASTER_H
#define ANDROID_VOLD_KEYMASTER_H

#ifdef __cplusplus

#include <memory>
#include <string>
#include <utility>

#include <android/hardware/keymaster/3.0/IKeymasterDevice.h>
#include <keystore/authorization_set.h>
#include "Utils.h"

namespace android {
namespace vold {
using ::android::hardware::keymaster::V3_0::IKeymasterDevice;
using ::keystore::ErrorCode;
using ::keystore::KeyPurpose;
using ::keystore::AuthorizationSet;

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
    explicit operator bool() { return mError == ErrorCode::OK; }
    ErrorCode errorCode() { return mError; }
    // Call "update" repeatedly until all of the input is consumed, and
    // concatenate the output. Return true on success.
    bool updateCompletely(const std::string& input, std::string* output);
    // Finish and write the output to this string, unless pointer is null.
    bool finish(std::string* output);
    // Move constructor
    KeymasterOperation(KeymasterOperation&& rhs) {
        mDevice = std::move(rhs.mDevice);
        mOpHandle = std::move(rhs.mOpHandle);
        mError = std::move(rhs.mError);
    }
    // Construct an object in an error state for error returns
    KeymasterOperation()
        : mDevice{nullptr}, mOpHandle{0},
          mError {ErrorCode::UNKNOWN_ERROR} {}
    // Move Assignment
    KeymasterOperation& operator= (KeymasterOperation&& rhs) {
        mDevice = std::move(rhs.mDevice);
        mOpHandle = std::move(rhs.mOpHandle);
        mError = std::move(rhs.mError);
        rhs.mError = ErrorCode::UNKNOWN_ERROR;
        rhs.mOpHandle = 0;
        return *this;
    }

  private:
    KeymasterOperation(const sp<IKeymasterDevice>& d, uint64_t h)
        : mDevice{d}, mOpHandle{h}, mError {ErrorCode::OK} {}
    KeymasterOperation(ErrorCode error)
        : mDevice{nullptr}, mOpHandle{0},
          mError {error} {}
    sp<IKeymasterDevice> mDevice;
    uint64_t mOpHandle;
    ErrorCode mError;
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
    //bool generateKey(const AuthorizationSet& inParams, std::string* key);
    // If the keymaster supports it, permanently delete a key.
    bool deleteKey(const std::string& key);
    // Replace stored key blob in response to KM_ERROR_KEY_REQUIRES_UPGRADE.
    bool upgradeKey(const std::string& oldKey, const AuthorizationSet& inParams,
                    std::string* newKey);
    // Begin a new cryptographic operation, collecting output parameters if pointer is non-null
    KeymasterOperation begin(KeyPurpose purpose, const std::string& key,
                             const AuthorizationSet& inParams, AuthorizationSet* outParams);
    bool isSecure();

  private:
    sp<hardware::keymaster::V3_0::IKeymasterDevice> mDevice;
    DISALLOW_COPY_AND_ASSIGN(Keymaster);
};

namespace dump {

template<typename T>
std::string toHexString(T t, bool prefix = true) {
    std::ostringstream os;
    if (prefix) { os << std::showbase; }
    os << std::hex << t;
    return os.str();
}

template<typename Array>
std::string arrayToHexString(const Array &a, size_t size) {
    using android::hardware::toString;
    std::string os;
    for (size_t i = 0; i < size; ++i) {
        os += toHexString(a[i]);
    }
    return os;
}

template<typename T>
std::string toString(const hardware::hidl_vec<T> &a) {
    std::string os;
    os += arrayToHexString(a, a.size());
    return os;
}

}  // namespace dump
}  // namespace vold
}  // namespace android

#endif // __cplusplus


/*
 * The following functions provide C bindings to keymaster services
 * needed by cryptfs scrypt. The compatibility check checks whether
 * the keymaster implementation is considered secure, i.e., TEE backed.
 * The create_key function generates an RSA key for signing.
 * The sign_object function signes an object with the given keymaster
 * key.
 */
__BEGIN_DECLS

//int keymaster_compatibility_cryptfs_scrypt();
/*int keymaster_create_key_for_cryptfs_scrypt(uint32_t rsa_key_size,
                                            uint64_t rsa_exponent,
                                            uint32_t ratelimit,
                                            uint8_t* key_buffer,
                                            uint32_t key_buffer_size,
                                            uint32_t* key_out_size);*/

int keymaster_sign_object_for_cryptfs_scrypt(const uint8_t* key_blob,
                                             size_t key_blob_size,
                                             uint32_t ratelimit,
                                             const uint8_t* object,
                                             const size_t object_size,
                                             uint8_t** signature_buffer,
                                             size_t* signature_buffer_size,
                                             uint8_t* key_buffer,
                                             uint32_t key_buffer_size,
                                             uint32_t* key_out_size);

__END_DECLS

#endif
