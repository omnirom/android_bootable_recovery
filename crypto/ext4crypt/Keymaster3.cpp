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

#include "Keymaster3.h"

//#include <android-base/logging.h>
#include <keystore/keymaster_tags.h>
#include <keystore/authorization_set.h>
#include <keystore/keystore_hidl_support.h>

#include <iostream>
#define ERROR 1
#define LOG(x) std::cout

using namespace ::keystore;
using android::hardware::hidl_string;

namespace android {
namespace vold {

KeymasterOperation::~KeymasterOperation() {
    if (mDevice.get()) mDevice->abort(mOpHandle);
}

bool KeymasterOperation::updateCompletely(const std::string& input, std::string* output) {
    if (output)
        output->clear();
    auto it = input.begin();
    uint32_t inputConsumed;

    ErrorCode km_error;
    auto hidlCB = [&] (ErrorCode ret, uint32_t _inputConsumed,
            const hidl_vec<KeyParameter>& /*ignored*/, const hidl_vec<uint8_t>& _output) {
        km_error = ret;
        if (km_error != ErrorCode::OK) return;
        inputConsumed = _inputConsumed;
        if (output)
            output->append(reinterpret_cast<const char*>(&_output[0]), _output.size());
    };

    while (it != input.end()) {
        size_t toRead = static_cast<size_t>(input.end() - it);
        auto inputBlob = blob2hidlVec(reinterpret_cast<const uint8_t*>(&*it), toRead);
        auto error = mDevice->update(mOpHandle, hidl_vec<KeyParameter>(), inputBlob, hidlCB);
        if (!error.isOk()) {
            LOG(ERROR) << "update failed: " << error.description();
            mDevice = nullptr;
            return false;
        }
        if (km_error != ErrorCode::OK) {
            LOG(ERROR) << "update failed, code " << int32_t(km_error);
            mDevice = nullptr;
            return false;
        }
        if (inputConsumed > toRead) {
            LOG(ERROR) << "update reported too much input consumed";
            mDevice = nullptr;
            return false;
        }
        it += inputConsumed;
    }
    return true;
}

bool KeymasterOperation::finish(std::string* output) {
    ErrorCode km_error;
    auto hidlCb = [&] (ErrorCode ret, const hidl_vec<KeyParameter>& /*ignored*/,
            const hidl_vec<uint8_t>& _output) {
        km_error = ret;
        if (km_error != ErrorCode::OK) return;
        if (output)
            output->assign(reinterpret_cast<const char*>(&_output[0]), _output.size());
    };
    auto error = mDevice->finish(mOpHandle, hidl_vec<KeyParameter>(), hidl_vec<uint8_t>(),
            hidl_vec<uint8_t>(), hidlCb);
    mDevice = nullptr;
    if (!error.isOk()) {
        LOG(ERROR) << "finish failed: " << error.description();
        return false;
    }
    if (km_error != ErrorCode::OK) {
        LOG(ERROR) << "finish failed, code " << int32_t(km_error);
        return false;
    }
    return true;
}

Keymaster::Keymaster() {
    mDevice = ::android::hardware::keymaster::V3_0::IKeymasterDevice::getService();
}

/*bool Keymaster::generateKey(const AuthorizationSet& inParams, std::string* key) {
    ErrorCode km_error;
    auto hidlCb = [&] (ErrorCode ret, const hidl_vec<uint8_t>& keyBlob,
            const KeyCharacteristics& /*ignored* /) {
        km_error = ret;
        if (km_error != ErrorCode::OK) return;
        if (key)
            key->assign(reinterpret_cast<const char*>(&keyBlob[0]), keyBlob.size());
    };

    auto error = mDevice->generateKey(inParams.hidl_data(), hidlCb);
    if (!error.isOk()) {
        LOG(ERROR) << "generate_key failed: " << error.description();
        return false;
    }
    if (km_error != ErrorCode::OK) {
        LOG(ERROR) << "generate_key failed, code " << int32_t(km_error);
        return false;
    }
    return true;
}*/

bool Keymaster::deleteKey(const std::string& key) {
	LOG(ERROR) << "NOT deleting key in TWRP";
	return false;
    /*auto keyBlob = blob2hidlVec(key);
    auto error = mDevice->deleteKey(keyBlob);
    if (!error.isOk()) {
        LOG(ERROR) << "delete_key failed: " << error.description();
        return false;
    }
    if (ErrorCode(error) != ErrorCode::OK) {
        LOG(ERROR) << "delete_key failed, code " << uint32_t(ErrorCode(error));
        return false;
    }
    return true;*/
}

bool Keymaster::upgradeKey(const std::string& oldKey, const AuthorizationSet& inParams,
                           std::string* newKey) {
    auto oldKeyBlob = blob2hidlVec(oldKey);
    ErrorCode km_error;
    auto hidlCb = [&] (ErrorCode ret, const hidl_vec<uint8_t>& upgradedKeyBlob) {
        km_error = ret;
        if (km_error != ErrorCode::OK) return;
        if (newKey)
            newKey->assign(reinterpret_cast<const char*>(&upgradedKeyBlob[0]),
                    upgradedKeyBlob.size());
    };
    auto error = mDevice->upgradeKey(oldKeyBlob, inParams.hidl_data(), hidlCb);
    if (!error.isOk()) {
        LOG(ERROR) << "upgrade_key failed: " << error.description();
        return false;
    }
    if (km_error != ErrorCode::OK) {
        LOG(ERROR) << "upgrade_key failed, code " << int32_t(km_error);
        return false;
    }
    return true;
}

KeymasterOperation Keymaster::begin(KeyPurpose purpose, const std::string& key,
                                    const AuthorizationSet& inParams,
                                    AuthorizationSet* outParams) {
    auto keyBlob = blob2hidlVec(key);
    uint64_t mOpHandle;
    ErrorCode km_error;

    auto hidlCb = [&] (ErrorCode ret, const hidl_vec<KeyParameter>& _outParams,
            uint64_t operationHandle) {
        km_error = ret;
        if (km_error != ErrorCode::OK) return;
        if (outParams)
            *outParams = _outParams;
        mOpHandle = operationHandle;
    };

    auto error = mDevice->begin(purpose, keyBlob, inParams.hidl_data(), hidlCb);
    if (!error.isOk()) {
        LOG(ERROR) << "begin failed: " << error.description() << "\n";
        return KeymasterOperation(ErrorCode::UNKNOWN_ERROR);
    }
    if (km_error != ErrorCode::OK) {
        LOG(ERROR) << "begin failed, code " << int32_t(km_error) << "\n";
        return KeymasterOperation(km_error);
    }
    return KeymasterOperation(mDevice, mOpHandle);
}
bool Keymaster::isSecure() {
    bool _isSecure = false;
    auto rc = mDevice->getHardwareFeatures(
            [&] (bool isSecure, bool, bool, bool, bool, const hidl_string&, const hidl_string&) {
                _isSecure = isSecure; });
    return rc.isOk() && _isSecure;
}

}  // namespace vold
}  // namespace android

using namespace ::android::vold;

/*
int keymaster_compatibility_cryptfs_scrypt() {
    Keymaster dev;
    if (!dev) {
        LOG(ERROR) << "Failed to initiate keymaster session";
        return -1;
    }
    return dev.isSecure();
}
*/

/*int keymaster_create_key_for_cryptfs_scrypt(uint32_t rsa_key_size,
                                            uint64_t rsa_exponent,
                                            uint32_t ratelimit,
                                            uint8_t* key_buffer,
                                            uint32_t key_buffer_size,
                                            uint32_t* key_out_size)
{
    Keymaster dev;
    std::string key;
    if (!dev) {
        LOG(ERROR) << "Failed to initiate keymaster session";
        return -1;
    }
    if (!key_buffer || !key_out_size) {
        LOG(ERROR) << __FILE__ << ":" << __LINE__ << ":Invalid argument";
        return -1;
    }
    if (key_out_size) {
        *key_out_size = 0;
    }

    auto paramBuilder = AuthorizationSetBuilder()
                            .Authorization(TAG_ALGORITHM, Algorithm::RSA)
                            .Authorization(TAG_KEY_SIZE, rsa_key_size)
                            .Authorization(TAG_RSA_PUBLIC_EXPONENT, rsa_exponent)
                            .Authorization(TAG_PURPOSE, KeyPurpose::SIGN)
                            .Authorization(TAG_PADDING, PaddingMode::NONE)
                            .Authorization(TAG_DIGEST, Digest::NONE)
                            .Authorization(TAG_BLOB_USAGE_REQUIREMENTS,
                                    KeyBlobUsageRequirements::STANDALONE)
                            .Authorization(TAG_NO_AUTH_REQUIRED)
                            .Authorization(TAG_MIN_SECONDS_BETWEEN_OPS, ratelimit);

    if (!dev.generateKey(paramBuilder, &key)) {
        return -1;
    }

    if (key_out_size) {
        *key_out_size = key.size();
    }

    if (key_buffer_size < key.size()) {
        return -1;
    }

    std::copy(key.data(), key.data() + key.size(), key_buffer);
    return 0;
}*/

int keymaster_sign_object_for_cryptfs_scrypt(const uint8_t* key_blob,
                                             size_t key_blob_size,
                                             uint32_t ratelimit,
                                             const uint8_t* object,
                                             const size_t object_size,
                                             uint8_t** signature_buffer,
                                             size_t* signature_buffer_size,
                                             uint8_t* key_buffer,
                                             uint32_t key_buffer_size,
                                             uint32_t* key_out_size)
{
    Keymaster dev;
    if (!dev) {
        LOG(ERROR) << "Failed to initiate keymaster session";
        return -1;
    }
    if (!key_blob || !object || !signature_buffer || !signature_buffer_size) {
        LOG(ERROR) << __FILE__ << ":" << __LINE__ << ":Invalid argument";
        return -1;
    }

    AuthorizationSet outParams;
    std::string key(reinterpret_cast<const char*>(key_blob), key_blob_size);
    std::string input(reinterpret_cast<const char*>(object), object_size);
    std::string output;
    KeymasterOperation op;

    auto paramBuilder = AuthorizationSetBuilder()
                            .Authorization(TAG_PADDING, PaddingMode::NONE)
                            .Authorization(TAG_DIGEST, Digest::NONE);

    while (true) {
        op = dev.begin(KeyPurpose::SIGN, key, paramBuilder, &outParams);
        if (op.errorCode() == ErrorCode::KEY_RATE_LIMIT_EXCEEDED) {
            sleep(ratelimit);
            continue;
        } else if (op.errorCode() == ErrorCode::KEY_REQUIRES_UPGRADE) {
            std::string newKey;
            bool ret = dev.upgradeKey(key, paramBuilder, &newKey);
            if(ret == false) {
                LOG(ERROR) << "Error upgradeKey: ";
                return -1;
            }

            if (key_out_size) {
                *key_out_size = newKey.size();
            }

            if (key_buffer_size < newKey.size()) {
                LOG(ERROR) << "key buffer size is too small";
                return -1;
            }

            std::copy(newKey.data(), newKey.data() + newKey.size(), key_buffer);
            key = newKey;
        } else break;
    }

    if (op.errorCode() != ErrorCode::OK) {
        LOG(ERROR) << "Error starting keymaster signature transaction: " << int32_t(op.errorCode());
        return -1;
    }

    if (!op.updateCompletely(input, &output)) {
        LOG(ERROR) << "Error sending data to keymaster signature transaction: "
                   << uint32_t(op.errorCode());
        return -1;
    }

    if (!op.finish(&output)) {
        LOG(ERROR) << "Error finalizing keymaster signature transaction: " << int32_t(op.errorCode());
        return -1;
    }

    *signature_buffer = reinterpret_cast<uint8_t*>(malloc(output.size()));
    if (*signature_buffer == nullptr) {
        LOG(ERROR) << "Error allocation buffer for keymaster signature";
        return -1;
    }
    *signature_buffer_size = output.size();
    std::copy(output.data(), output.data() + output.size(), *signature_buffer);
    return 0;
}
