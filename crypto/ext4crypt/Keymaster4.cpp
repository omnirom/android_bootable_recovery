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

#include "Keymaster4.h"

//#include <android-base/logging.h>
#include <keymasterV4_0/authorization_set.h>
#include <keymasterV4_0/keymaster_utils.h>

#include <iostream>
#define LOG(x) std::cout
#define PLOG(x) std::cout

namespace android {
namespace vold {

using ::android::hardware::hidl_string;
using ::android::hardware::hidl_vec;
using ::android::hardware::keymaster::V4_0::SecurityLevel;

KeymasterOperation::~KeymasterOperation() {
    if (mDevice) mDevice->abort(mOpHandle);
}

bool KeymasterOperation::updateCompletely(const char* input, size_t inputLen,
                                          const std::function<void(const char*, size_t)> consumer) {
    uint32_t inputConsumed = 0;

    km::ErrorCode km_error;
    auto hidlCB = [&](km::ErrorCode ret, uint32_t inputConsumedDelta,
                      const hidl_vec<km::KeyParameter>& /*ignored*/,
                      const hidl_vec<uint8_t>& _output) {
        km_error = ret;
        if (km_error != km::ErrorCode::OK) return;
        inputConsumed += inputConsumedDelta;
        consumer(reinterpret_cast<const char*>(&_output[0]), _output.size());
    };

    while (inputConsumed != inputLen) {
        size_t toRead = static_cast<size_t>(inputLen - inputConsumed);
        auto inputBlob = km::support::blob2hidlVec(
            reinterpret_cast<const uint8_t*>(&input[inputConsumed]), toRead);
        auto error = mDevice->update(mOpHandle, hidl_vec<km::KeyParameter>(), inputBlob,
                                     km::HardwareAuthToken(), km::VerificationToken(), hidlCB);
        if (!error.isOk()) {
            LOG(ERROR) << "update failed: " << error.description() << std::endl;
            mDevice = nullptr;
            return false;
        }
        if (km_error != km::ErrorCode::OK) {
            LOG(ERROR) << "update failed, code " << int32_t(km_error) << std::endl;
            mDevice = nullptr;
            return false;
        }
        if (inputConsumed > inputLen) {
            LOG(ERROR) << "update reported too much input consumed" << std::endl;
            mDevice = nullptr;
            return false;
        }
    }
    return true;
}

bool KeymasterOperation::finish(std::string* output) {
    km::ErrorCode km_error;
    auto hidlCb = [&](km::ErrorCode ret, const hidl_vec<km::KeyParameter>& /*ignored*/,
                      const hidl_vec<uint8_t>& _output) {
        km_error = ret;
        if (km_error != km::ErrorCode::OK) return;
        if (output) output->assign(reinterpret_cast<const char*>(&_output[0]), _output.size());
    };
    auto error = mDevice->finish(mOpHandle, hidl_vec<km::KeyParameter>(), hidl_vec<uint8_t>(),
                                 hidl_vec<uint8_t>(), km::HardwareAuthToken(),
                                 km::VerificationToken(), hidlCb);
    mDevice = nullptr;
    if (!error.isOk()) {
        LOG(ERROR) << "finish failed: " << error.description() << std::endl;
        return false;
    }
    if (km_error != km::ErrorCode::OK) {
        LOG(ERROR) << "finish failed, code " << int32_t(km_error) << std::endl;
        return false;
    }
    return true;
}

/* static */ bool Keymaster::hmacKeyGenerated = false;

Keymaster::Keymaster() {
    auto devices = KmDevice::enumerateAvailableDevices();
    if (!hmacKeyGenerated) {
        KmDevice::performHmacKeyAgreement(devices);
        hmacKeyGenerated = true;
    }
    for (auto& dev : devices) {
        // Do not use StrongBox for device encryption / credential encryption.  If a security chip
        // is present it will have Weaver, which already strengthens CE.  We get no additional
        // benefit from using StrongBox here, so skip it.
        if (dev->halVersion().securityLevel != SecurityLevel::STRONGBOX) {
            mDevice = std::move(dev);
            break;
        }
    }
    if (!mDevice) return;
    auto& version = mDevice->halVersion();
    LOG(INFO) << "Using " << version.keymasterName << " from " << version.authorName
              << " for encryption.  Security level: " << toString(version.securityLevel)
              << ", HAL: " << mDevice->descriptor() << "/" << mDevice->instanceName() << std::endl;
}

bool Keymaster::generateKey(const km::AuthorizationSet& inParams, std::string* key) {
    km::ErrorCode km_error;
    auto hidlCb = [&](km::ErrorCode ret, const hidl_vec<uint8_t>& keyBlob,
                      const km::KeyCharacteristics& /*ignored*/) {
        km_error = ret;
        if (km_error != km::ErrorCode::OK) return;
        if (key) key->assign(reinterpret_cast<const char*>(&keyBlob[0]), keyBlob.size());
    };

    auto error = mDevice->generateKey(inParams.hidl_data(), hidlCb);
    if (!error.isOk()) {
        LOG(ERROR) << "generate_key failed: " << error.description() << std::endl;
        return false;
    }
    if (km_error != km::ErrorCode::OK) {
        LOG(ERROR) << "generate_key failed, code " << int32_t(km_error) << std::endl;
        return false;
    }
    return true;
}

bool Keymaster::exportKey(km::KeyFormat format, KeyBuffer& kmKey, const std::string& clientId,
                          const std::string& appData, std::string* key) {
    auto kmKeyBlob = km::support::blob2hidlVec(std::string(kmKey.data(), kmKey.size()));
    auto emptyAssign = NULL;
    auto kmClientId = (clientId == "!") ? emptyAssign: km::support::blob2hidlVec(clientId);
    auto kmAppData = (appData == "!") ? emptyAssign: km::support::blob2hidlVec(appData);
    km::ErrorCode km_error;
    auto hidlCb = [&](km::ErrorCode ret, const hidl_vec<uint8_t>& exportedKeyBlob) {
        km_error = ret;
        if (km_error != km::ErrorCode::OK) return;
        if(key)
            key->assign(reinterpret_cast<const char*>(&exportedKeyBlob[0]),
                            exportedKeyBlob.size());
    };
    auto error = mDevice->exportKey(format, kmKeyBlob, kmClientId, kmAppData, hidlCb);
    if (!error.isOk()) {
        LOG(ERROR) << "export_key failed: " << error.description();
        return false;
    }
    if (km_error != km::ErrorCode::OK) {
        LOG(ERROR) << "export_key failed, code " << int32_t(km_error);
        return false;
    }
    return true;
}

bool Keymaster::deleteKey(const std::string& key) {
    LOG(ERROR) << "not actually deleting key\n";
    return true;
    auto keyBlob = km::support::blob2hidlVec(key);
    auto error = mDevice->deleteKey(keyBlob);
    if (!error.isOk()) {
        LOG(ERROR) << "delete_key failed: " << error.description();
        return false;
    }
    if (error != km::ErrorCode::OK) {
        LOG(ERROR) << "delete_key failed, code " << int32_t(km::ErrorCode(error));
        return false;
    }
    return true;
}

bool Keymaster::upgradeKey(const std::string& oldKey, const km::AuthorizationSet& inParams,
                           std::string* newKey) {
    auto oldKeyBlob = km::support::blob2hidlVec(oldKey);
    km::ErrorCode km_error;
    auto hidlCb = [&](km::ErrorCode ret, const hidl_vec<uint8_t>& upgradedKeyBlob) {
        km_error = ret;
        if (km_error != km::ErrorCode::OK) return;
        if (newKey)
            newKey->assign(reinterpret_cast<const char*>(&upgradedKeyBlob[0]),
                           upgradedKeyBlob.size());
    };
    auto error = mDevice->upgradeKey(oldKeyBlob, inParams.hidl_data(), hidlCb);
    if (!error.isOk()) {
        LOG(ERROR) << "upgrade_key failed: " << error.description() << std::endl;
        return false;
    }
    if (km_error != km::ErrorCode::OK) {
        LOG(ERROR) << "upgrade_key failed, code " << int32_t(km_error) << std::endl;
        return false;
    }
    return true;
}

KeymasterOperation Keymaster::begin(km::KeyPurpose purpose, const std::string& key,
                                    const km::AuthorizationSet& inParams,
                                    const km::HardwareAuthToken& authToken,
                                    km::AuthorizationSet* outParams) {
    auto keyBlob = km::support::blob2hidlVec(key);
    uint64_t mOpHandle;
    km::ErrorCode km_error;

    auto hidlCb = [&](km::ErrorCode ret, const hidl_vec<km::KeyParameter>& _outParams,
                      uint64_t operationHandle) {
        km_error = ret;
        if (km_error != km::ErrorCode::OK) return;
        if (outParams) *outParams = _outParams;
        mOpHandle = operationHandle;
    };

    auto error = mDevice->begin(purpose, keyBlob, inParams.hidl_data(), authToken, hidlCb);
    if (!error.isOk()) {
        LOG(ERROR) << "begin failed: " << error.description() << std::endl;
        return KeymasterOperation(km::ErrorCode::UNKNOWN_ERROR);
    }
    if (km_error != km::ErrorCode::OK) {
        LOG(ERROR) << "begin failed, code " << int32_t(km_error) << std::endl;
        return KeymasterOperation(km_error);
    }
    return KeymasterOperation(mDevice.get(), mOpHandle);
}

bool Keymaster::isSecure() {
    return mDevice->halVersion().securityLevel != km::SecurityLevel::SOFTWARE;
}

}  // namespace vold
}  // namespace android

using namespace ::android::vold;

/*
int keymaster_compatibility_cryptfs_scrypt() {
    Keymaster dev;
    if (!dev) {
        LOG(ERROR) << "Failed to initiate keymaster session" << std::endl;
        return -1;
    }
    return dev.isSecure();
}
*/

static bool write_string_to_buf(const std::string& towrite, uint8_t* buffer, uint32_t buffer_size,
                                uint32_t* out_size) {
    if (!buffer || !out_size) {
        LOG(ERROR) << "Missing target pointers" << std::endl;
        return false;
    }
    *out_size = towrite.size();
    if (buffer_size < towrite.size()) {
        LOG(ERROR) << "Buffer too small " << buffer_size << " < " << towrite.size() << std::endl;
        return false;
    }
    memset(buffer, '\0', buffer_size);
    std::copy(towrite.begin(), towrite.end(), buffer);
    return true;
}

static km::AuthorizationSet keyParams(uint32_t rsa_key_size, uint64_t rsa_exponent,
                                      uint32_t ratelimit) {
    return km::AuthorizationSetBuilder()
        .RsaSigningKey(rsa_key_size, rsa_exponent)
        .NoDigestOrPadding()
        .Authorization(km::TAG_BLOB_USAGE_REQUIREMENTS, km::KeyBlobUsageRequirements::STANDALONE)
        .Authorization(km::TAG_NO_AUTH_REQUIRED)
        .Authorization(km::TAG_MIN_SECONDS_BETWEEN_OPS, ratelimit);
}

/*
int keymaster_create_key_for_cryptfs_scrypt(uint32_t rsa_key_size, uint64_t rsa_exponent,
                                            uint32_t ratelimit, uint8_t* key_buffer,
                                            uint32_t key_buffer_size, uint32_t* key_out_size) {
    if (key_out_size) {
        *key_out_size = 0;
    }
    Keymaster dev;
    if (!dev) {
        LOG(ERROR) << "Failed to initiate keymaster session" << std::endl;
        return -1;
    }
    std::string key;
    if (!dev.generateKey(keyParams(rsa_key_size, rsa_exponent, ratelimit), &key)) return -1;
    if (!write_string_to_buf(key, key_buffer, key_buffer_size, key_out_size)) return -1;
    return 0;
}
*/

int keymaster_upgrade_key_for_cryptfs_scrypt(uint32_t rsa_key_size, uint64_t rsa_exponent,
                                             uint32_t ratelimit, const uint8_t* key_blob,
                                             size_t key_blob_size, uint8_t* key_buffer,
                                             uint32_t key_buffer_size, uint32_t* key_out_size) {
    if (key_out_size) {
        *key_out_size = 0;
    }
    Keymaster dev;
    if (!dev) {
        LOG(ERROR) << "Failed to initiate keymaster session" << std::endl;
        return -1;
    }
    std::string old_key(reinterpret_cast<const char*>(key_blob), key_blob_size);
    std::string new_key;
    if (!dev.upgradeKey(old_key, keyParams(rsa_key_size, rsa_exponent, ratelimit), &new_key))
        return -1;
    if (!write_string_to_buf(new_key, key_buffer, key_buffer_size, key_out_size)) return -1;
    return 0;
}

KeymasterSignResult keymaster_sign_object_for_cryptfs_scrypt(
    const uint8_t* key_blob, size_t key_blob_size, uint32_t ratelimit, const uint8_t* object,
    const size_t object_size, uint8_t** signature_buffer, size_t* signature_buffer_size) {
    Keymaster dev;
    if (!dev) {
        LOG(ERROR) << "Failed to initiate keymaster session" << std::endl;
        return KeymasterSignResult::error;
    }
    if (!key_blob || !object || !signature_buffer || !signature_buffer_size) {
        LOG(ERROR) << __FILE__ << ":" << __LINE__ << ":Invalid argument" << std::endl;
        return KeymasterSignResult::error;
    }

    km::AuthorizationSet outParams;
    std::string key(reinterpret_cast<const char*>(key_blob), key_blob_size);
    std::string input(reinterpret_cast<const char*>(object), object_size);
    std::string output;
    KeymasterOperation op;

    auto paramBuilder = km::AuthorizationSetBuilder().NoDigestOrPadding();
    while (true) {
        op = dev.begin(km::KeyPurpose::SIGN, key, paramBuilder, km::HardwareAuthToken(), &outParams);
        if (op.errorCode() == km::ErrorCode::KEY_RATE_LIMIT_EXCEEDED) {
            sleep(ratelimit);
            continue;
        } else
            break;
    }

    if (op.errorCode() == km::ErrorCode::KEY_REQUIRES_UPGRADE) {
        LOG(ERROR) << "Keymaster key requires upgrade" << std::endl;
        return KeymasterSignResult::upgrade;
    }

    if (op.errorCode() != km::ErrorCode::OK) {
        LOG(ERROR) << "Error starting keymaster signature transaction: " << int32_t(op.errorCode()) << std::endl;
        return KeymasterSignResult::error;
    }

    if (!op.updateCompletely(input, &output)) {
        LOG(ERROR) << "Error sending data to keymaster signature transaction: "
                   << uint32_t(op.errorCode()) << std::endl;
        return KeymasterSignResult::error;
    }

    if (!op.finish(&output)) {
        LOG(ERROR) << "Error finalizing keymaster signature transaction: "
                   << int32_t(op.errorCode()) << std::endl;
        return KeymasterSignResult::error;
    }

    *signature_buffer = reinterpret_cast<uint8_t*>(malloc(output.size()));
    if (*signature_buffer == nullptr) {
        LOG(ERROR) << "Error allocation buffer for keymaster signature" << std::endl;
        return KeymasterSignResult::error;
    }
    *signature_buffer_size = output.size();
    std::copy(output.data(), output.data() + output.size(), *signature_buffer);
    return KeymasterSignResult::ok;
}
