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

#include "KeyStorage.h"

#include "Keymaster.h"
#include "ScryptParameters.h"
#include "Utils.h"

#include <vector>

#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <iostream>

#include <openssl/sha.h>

#include <android-base/file.h>
//#include <android-base/logging.h>

#include <cutils/properties.h>

#include <hardware/hw_auth_token.h>

#include <keymaster/authorization_set.h>

extern "C" {

#include "crypto_scrypt.h"
}

#define ERROR 1
#define LOG(x) std::cout
#define PLOG(x) std::cout

namespace android {
namespace vold {

const KeyAuthentication kEmptyAuthentication{"", ""};

static constexpr size_t AES_KEY_BYTES = 32;
static constexpr size_t GCM_NONCE_BYTES = 12;
static constexpr size_t GCM_MAC_BYTES = 16;
static constexpr size_t SALT_BYTES = 1 << 4;
static constexpr size_t SECDISCARDABLE_BYTES = 1 << 14;
static constexpr size_t STRETCHED_BYTES = 1 << 6;

static constexpr uint32_t AUTH_TIMEOUT = 30; // Seconds

static const char* kCurrentVersion = "1";
static const char* kRmPath = "/system/bin/rm";
static const char* kSecdiscardPath = "/system/bin/secdiscard";
static const char* kStretch_none = "none";
static const char* kStretch_nopassword = "nopassword";
static const std::string kStretchPrefix_scrypt = "scrypt ";
static const char* kFn_encrypted_key = "encrypted_key";
static const char* kFn_keymaster_key_blob = "keymaster_key_blob";
static const char* kFn_salt = "salt";
static const char* kFn_secdiscardable = "secdiscardable";
static const char* kFn_stretching = "stretching";
static const char* kFn_version = "version";

static bool checkSize(const std::string& kind, size_t actual, size_t expected) {
    if (actual != expected) {
        LOG(ERROR) << "Wrong number of bytes in " << kind << ", expected " << expected << " got "
                   << actual;
        return false;
    }
    return true;
}

static std::string hashSecdiscardable(const std::string& secdiscardable) {
    SHA512_CTX c;

    SHA512_Init(&c);
    // Personalise the hashing by introducing a fixed prefix.
    // Hashing applications should use personalization except when there is a
    // specific reason not to; see section 4.11 of https://www.schneier.com/skein1.3.pdf
    std::string secdiscardableHashingPrefix = "Android secdiscardable SHA512";
    secdiscardableHashingPrefix.resize(SHA512_CBLOCK);
    SHA512_Update(&c, secdiscardableHashingPrefix.data(), secdiscardableHashingPrefix.size());
    SHA512_Update(&c, secdiscardable.data(), secdiscardable.size());
    std::string res(SHA512_DIGEST_LENGTH, '\0');
    SHA512_Final(reinterpret_cast<uint8_t*>(&res[0]), &c);
    return res;
}

/*static bool generateKeymasterKey(Keymaster& keymaster, const KeyAuthentication& auth,
                                 const std::string& appId, std::string* key) {
    auto paramBuilder = keymaster::AuthorizationSetBuilder()
                            .AesEncryptionKey(AES_KEY_BYTES * 8)
                            .Authorization(keymaster::TAG_BLOCK_MODE, KM_MODE_GCM)
                            .Authorization(keymaster::TAG_MIN_MAC_LENGTH, GCM_MAC_BYTES * 8)
                            .Authorization(keymaster::TAG_PADDING, KM_PAD_NONE);
    addStringParam(&paramBuilder, keymaster::TAG_APPLICATION_ID, appId);
    if (auth.token.empty()) {
        LOG(DEBUG) << "Creating key that doesn't need auth token";
        paramBuilder.Authorization(keymaster::TAG_NO_AUTH_REQUIRED);
    } else {
        LOG(DEBUG) << "Auth token required for key";
        if (auth.token.size() != sizeof(hw_auth_token_t)) {
            LOG(ERROR) << "Auth token should be " << sizeof(hw_auth_token_t) << " bytes, was "
                       << auth.token.size() << " bytes";
            return false;
        }
        const hw_auth_token_t* at = reinterpret_cast<const hw_auth_token_t*>(auth.token.data());
        paramBuilder.Authorization(keymaster::TAG_USER_SECURE_ID, at->user_id);
        paramBuilder.Authorization(keymaster::TAG_USER_AUTH_TYPE, HW_AUTH_PASSWORD);
        paramBuilder.Authorization(keymaster::TAG_AUTH_TIMEOUT, AUTH_TIMEOUT);
    }
    return keymaster.generateKey(paramBuilder.build(), key);
}*/

static keymaster::AuthorizationSetBuilder beginParams(const KeyAuthentication& auth,
                                                      const std::string& appId) {
    auto paramBuilder = keymaster::AuthorizationSetBuilder()
                            .Authorization(keymaster::TAG_BLOCK_MODE, KM_MODE_GCM)
                            .Authorization(keymaster::TAG_MAC_LENGTH, GCM_MAC_BYTES * 8)
                            .Authorization(keymaster::TAG_PADDING, KM_PAD_NONE);
    addStringParam(&paramBuilder, keymaster::TAG_APPLICATION_ID, appId);
    if (!auth.token.empty()) {
        LOG(DEBUG) << "Supplying auth token to Keymaster";
        addStringParam(&paramBuilder, keymaster::TAG_AUTH_TOKEN, auth.token);
    }
    return paramBuilder;
}

/*static bool encryptWithKeymasterKey(Keymaster& keymaster, const std::string& key,
                                    const KeyAuthentication& auth, const std::string& appId,
                                    const std::string& message, std::string* ciphertext) {
    auto params = beginParams(auth, appId).build();
    keymaster::AuthorizationSet outParams;
    auto opHandle = keymaster.begin(KM_PURPOSE_ENCRYPT, key, params, &outParams);
    if (!opHandle) return false;
    keymaster_blob_t nonceBlob;
    if (!outParams.GetTagValue(keymaster::TAG_NONCE, &nonceBlob)) {
        LOG(ERROR) << "GCM encryption but no nonce generated";
        return false;
    }
    // nonceBlob here is just a pointer into existing data, must not be freed
    std::string nonce(reinterpret_cast<const char*>(nonceBlob.data), nonceBlob.data_length);
    if (!checkSize("nonce", nonce.size(), GCM_NONCE_BYTES)) return false;
    std::string body;
    if (!opHandle.updateCompletely(message, &body)) return false;

    std::string mac;
    if (!opHandle.finishWithOutput(&mac)) return false;
    if (!checkSize("mac", mac.size(), GCM_MAC_BYTES)) return false;
    *ciphertext = nonce + body + mac;
    return true;
}*/

static bool decryptWithKeymasterKey(Keymaster& keymaster, const std::string& key,
                                    const KeyAuthentication& auth, const std::string& appId,
                                    const std::string& ciphertext, std::string* message) {
    auto nonce = ciphertext.substr(0, GCM_NONCE_BYTES);
    auto bodyAndMac = ciphertext.substr(GCM_NONCE_BYTES);
    auto params = addStringParam(beginParams(auth, appId), keymaster::TAG_NONCE, nonce).build();
    auto opHandle = keymaster.begin(KM_PURPOSE_DECRYPT, key, params);
    if (!opHandle) return false;
    if (!opHandle.updateCompletely(bodyAndMac, message)) return false;
    if (!opHandle.finish()) return false;
    return true;
}

static bool readFileToString(const std::string& filename, std::string* result) {
    if (!android::base::ReadFileToString(filename, result)) {
        PLOG(ERROR) << "Failed to read from " << filename;
        return false;
    }
    return true;
}

/*static bool writeStringToFile(const std::string& payload, const std::string& filename) {
    if (!android::base::WriteStringToFile(payload, filename)) {
        PLOG(ERROR) << "Failed to write to " << filename;
        return false;
    }
    return true;
}*/

static std::string getStretching() {
    char paramstr[PROPERTY_VALUE_MAX];

    property_get(SCRYPT_PROP, paramstr, SCRYPT_DEFAULTS);
    return std::string() + kStretchPrefix_scrypt + paramstr;
}

static bool stretchingNeedsSalt(const std::string& stretching) {
    return stretching != kStretch_nopassword && stretching != kStretch_none;
}

static bool stretchSecret(const std::string& stretching, const std::string& secret,
                          const std::string& salt, std::string* stretched) {
    if (stretching == kStretch_nopassword) {
        if (!secret.empty()) {
            LOG(WARNING) << "Password present but stretching is nopassword";
            // Continue anyway
        }
        stretched->clear();
    } else if (stretching == kStretch_none) {
        *stretched = secret;
    } else if (std::equal(kStretchPrefix_scrypt.begin(), kStretchPrefix_scrypt.end(),
                          stretching.begin())) {
        int Nf, rf, pf;
        if (!parse_scrypt_parameters(stretching.substr(kStretchPrefix_scrypt.size()).c_str(), &Nf,
                                     &rf, &pf)) {
            LOG(ERROR) << "Unable to parse scrypt params in stretching: " << stretching;
            return false;
        }
        stretched->assign(STRETCHED_BYTES, '\0');
        if (crypto_scrypt(reinterpret_cast<const uint8_t*>(secret.data()), secret.size(),
                          reinterpret_cast<const uint8_t*>(salt.data()), salt.size(),
                          1 << Nf, 1 << rf, 1 << pf,
                          reinterpret_cast<uint8_t*>(&(*stretched)[0]), stretched->size()) != 0) {
            LOG(ERROR) << "scrypt failed with params: " << stretching;
            return false;
        }
    } else {
        LOG(ERROR) << "Unknown stretching type: " << stretching;
        return false;
    }
    return true;
}

static bool generateAppId(const KeyAuthentication& auth, const std::string& stretching,
                          const std::string& salt, const std::string& secdiscardable,
                          std::string* appId) {
    std::string stretched;
    if (!stretchSecret(stretching, auth.secret, salt, &stretched)) return false;
    *appId = hashSecdiscardable(secdiscardable) + stretched;
    return true;
}

/*bool storeKey(const std::string& dir, const KeyAuthentication& auth, const std::string& key) {
    if (TEMP_FAILURE_RETRY(mkdir(dir.c_str(), 0700)) == -1) {
        PLOG(ERROR) << "key mkdir " << dir;
        return false;
    }
    if (!writeStringToFile(kCurrentVersion, dir + "/" + kFn_version)) return false;
    std::string secdiscardable;
    if (ReadRandomBytes(SECDISCARDABLE_BYTES, secdiscardable) != OK) {
        // TODO status_t plays badly with PLOG, fix it.
        LOG(ERROR) << "Random read failed";
        return false;
    }
    if (!writeStringToFile(secdiscardable, dir + "/" + kFn_secdiscardable)) return false;
    std::string stretching = auth.secret.empty() ? kStretch_nopassword : getStretching();
    if (!writeStringToFile(stretching, dir + "/" + kFn_stretching)) return false;
    std::string salt;
    if (stretchingNeedsSalt(stretching)) {
        if (ReadRandomBytes(SALT_BYTES, salt) != OK) {
            LOG(ERROR) << "Random read failed";
            return false;
        }
        if (!writeStringToFile(salt, dir + "/" + kFn_salt)) return false;
    }
    std::string appId;
    if (!generateAppId(auth, stretching, salt, secdiscardable, &appId)) return false;
    Keymaster keymaster;
    if (!keymaster) return false;
    std::string kmKey;
    if (!generateKeymasterKey(keymaster, auth, appId, &kmKey)) return false;
    if (!writeStringToFile(kmKey, dir + "/" + kFn_keymaster_key_blob)) return false;
    std::string encryptedKey;
    if (!encryptWithKeymasterKey(keymaster, kmKey, auth, appId, key, &encryptedKey)) return false;
    if (!writeStringToFile(encryptedKey, dir + "/" + kFn_encrypted_key)) return false;
    return true;
}*/

bool retrieveKey(const std::string& dir, const KeyAuthentication& auth, std::string* key) {
    std::string version;
    if (!readFileToString(dir + "/" + kFn_version, &version)) return false;
    if (version != kCurrentVersion) {
        LOG(ERROR) << "Version mismatch, expected " << kCurrentVersion << " got " << version;
        return false;
    }
    std::string secdiscardable;
    if (!readFileToString(dir + "/" + kFn_secdiscardable, &secdiscardable)) return false;
    std::string stretching;
    if (!readFileToString(dir + "/" + kFn_stretching, &stretching)) return false;
    std::string salt;
    if (stretchingNeedsSalt(stretching)) {
        if (!readFileToString(dir + "/" + kFn_salt, &salt)) return false;
    }
    std::string appId;
    if (!generateAppId(auth, stretching, salt, secdiscardable, &appId)) return false;
    std::string kmKey;
    if (!readFileToString(dir + "/" + kFn_keymaster_key_blob, &kmKey)) return false;
    std::string encryptedMessage;
    if (!readFileToString(dir + "/" + kFn_encrypted_key, &encryptedMessage)) return false;
    Keymaster keymaster;
    if (!keymaster) return false;
    return decryptWithKeymasterKey(keymaster, kmKey, auth, appId, encryptedMessage, key);
}

static bool deleteKey(const std::string& dir) {
    std::string kmKey;
    if (!readFileToString(dir + "/" + kFn_keymaster_key_blob, &kmKey)) return false;
    Keymaster keymaster;
    if (!keymaster) return false;
    if (!keymaster.deleteKey(kmKey)) return false;
    return true;
}

static bool secdiscardSecdiscardable(const std::string& dir) {
    if (ForkExecvp(
            std::vector<std::string>{kSecdiscardPath, "--", dir + "/" + kFn_secdiscardable}) != 0) {
        LOG(ERROR) << "secdiscard failed";
        return false;
    }
    return true;
}

static bool recursiveDeleteKey(const std::string& dir) {
    if (ForkExecvp(std::vector<std::string>{kRmPath, "-rf", dir}) != 0) {
        LOG(ERROR) << "recursive delete failed";
        return false;
    }
    return true;
}

bool destroyKey(const std::string& dir) {
    bool success = true;
    // Try each thing, even if previous things failed.
    success &= deleteKey(dir);
    success &= secdiscardSecdiscardable(dir);
    success &= recursiveDeleteKey(dir);
    return success;
}

}  // namespace vold
}  // namespace android
