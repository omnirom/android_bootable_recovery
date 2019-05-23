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

#include "KeyStorage4.h"

#include "Keymaster4.h"
#include "ScryptParameters.h"
#include "Utils.h"

#include <vector>

#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/sha.h>

#include <android-base/file.h>
//#include <android-base/logging.h>
#include <android-base/unique_fd.h>

#include <cutils/properties.h>

#include <hardware/hw_auth_token.h>
#include <keymasterV4_0/authorization_set.h>
#include <keymasterV4_0/keymaster_utils.h>

#include <iostream>
#define ERROR 1
#define LOG(x) std::cout
#define PLOG(x) std::cout

extern "C" {

#include "crypto_scrypt.h"
}

namespace android {
namespace vold {

const KeyAuthentication kEmptyAuthentication{"", ""};

static constexpr size_t AES_KEY_BYTES = 32;
static constexpr size_t GCM_NONCE_BYTES = 12;
static constexpr size_t GCM_MAC_BYTES = 16;
static constexpr size_t SALT_BYTES = 1 << 4;
static constexpr size_t SECDISCARDABLE_BYTES = 1 << 14;
static constexpr size_t STRETCHED_BYTES = 1 << 6;

static constexpr uint32_t AUTH_TIMEOUT = 30;  // Seconds
constexpr int EXT4_AES_256_XTS_KEY_SIZE = 64;

static const char* kCurrentVersion = "1";
static const char* kRmPath = "/system/bin/rm";
static const char* kSecdiscardPath = "/system/bin/secdiscard";
static const char* kStretch_none = "none";
static const char* kStretch_nopassword = "nopassword";
static const std::string kStretchPrefix_scrypt = "scrypt ";
static const char* kHashPrefix_secdiscardable = "Android secdiscardable SHA512";
static const char* kHashPrefix_keygen = "Android key wrapping key generation SHA512";
static const char* kFn_encrypted_key = "encrypted_key";
static const char* kFn_keymaster_key_blob = "keymaster_key_blob";
static const char* kFn_keymaster_key_blob_upgraded = "keymaster_key_blob_upgraded";
static const char* kFn_salt = "salt";
static const char* kFn_secdiscardable = "secdiscardable";
static const char* kFn_stretching = "stretching";
static const char* kFn_version = "version";

static bool checkSize(const std::string& kind, size_t actual, size_t expected) {
    if (actual != expected) {
        LOG(ERROR) << "Wrong number of bytes in " << kind << ", expected " << expected << " got "
                   << actual << std::endl;
        return false;
    }
    return true;
}

static void hashWithPrefix(char const* prefix, const std::string& tohash, std::string* res) {
    SHA512_CTX c;

    SHA512_Init(&c);
    // Personalise the hashing by introducing a fixed prefix.
    // Hashing applications should use personalization except when there is a
    // specific reason not to; see section 4.11 of https://www.schneier.com/skein1.3.pdf
    std::string hashingPrefix = prefix;
    hashingPrefix.resize(SHA512_CBLOCK);
    SHA512_Update(&c, hashingPrefix.data(), hashingPrefix.size());
    SHA512_Update(&c, tohash.data(), tohash.size());
    res->assign(SHA512_DIGEST_LENGTH, '\0');
    SHA512_Final(reinterpret_cast<uint8_t*>(&(*res)[0]), &c);
}

static bool generateKeymasterKey(Keymaster& keymaster, const KeyAuthentication& auth,
                                 const std::string& appId, std::string* key) {
    auto paramBuilder = km::AuthorizationSetBuilder()
                            .AesEncryptionKey(AES_KEY_BYTES * 8)
                            .GcmModeMinMacLen(GCM_MAC_BYTES * 8)
                            .Authorization(km::TAG_APPLICATION_ID, km::support::blob2hidlVec(appId));
    if (auth.token.empty()) {
        LOG(DEBUG) << "Creating key that doesn't need auth token" << std::endl;
        paramBuilder.Authorization(km::TAG_NO_AUTH_REQUIRED);
    } else {
        LOG(DEBUG) << "Auth token required for key" << std::endl;
        if (auth.token.size() != sizeof(hw_auth_token_t)) {
            LOG(ERROR) << "Auth token should be " << sizeof(hw_auth_token_t) << " bytes, was "
                       << auth.token.size() << " bytes" << std::endl;
            return false;
        }
        const hw_auth_token_t* at = reinterpret_cast<const hw_auth_token_t*>(auth.token.data());
        paramBuilder.Authorization(km::TAG_USER_SECURE_ID, at->user_id);
        paramBuilder.Authorization(km::TAG_USER_AUTH_TYPE, km::HardwareAuthenticatorType::PASSWORD);
        paramBuilder.Authorization(km::TAG_AUTH_TIMEOUT, AUTH_TIMEOUT);
    }
    return keymaster.generateKey(paramBuilder, key);
}

bool generateWrappedKey(userid_t user_id, KeyType key_type,
                                     KeyBuffer* key) {
    Keymaster keymaster;
    if (!keymaster) return false;
    *key = KeyBuffer(EXT4_AES_256_XTS_KEY_SIZE);
    std::string key_temp;
    auto paramBuilder = km::AuthorizationSetBuilder()
                               .AesEncryptionKey(AES_KEY_BYTES * 8)
                               .GcmModeMinMacLen(GCM_MAC_BYTES * 8)
                               .Authorization(km::TAG_USER_ID, user_id);
    km::KeyParameter param1;
    param1.tag = (km::Tag) (android::hardware::keymaster::V4_0::KM_TAG_FBE_ICE);
    param1.f.boolValue = true;
    paramBuilder.push_back(param1);
    km::KeyParameter param2;
    if ((key_type == KeyType::DE_USER) || (key_type == KeyType::DE_SYS)) {
        param2.tag = (km::Tag) (android::hardware::keymaster::V4_0::KM_TAG_KEY_TYPE);
        param2.f.integer = 0;
    } else if (key_type == KeyType::CE_USER) {
        param2.tag = (km::Tag) (android::hardware::keymaster::V4_0::KM_TAG_KEY_TYPE);
        param2.f.integer = 1;
    }
    paramBuilder.push_back(param2);
    if (!keymaster.generateKey(paramBuilder, &key_temp)) return false;
    *key = KeyBuffer(key_temp.size());
    memcpy(reinterpret_cast<void*>(key->data()), key_temp.c_str(), key->size());
    return true;
}

bool getEphemeralWrappedKey(km::KeyFormat format, KeyBuffer& kmKey, KeyBuffer* key) {
    std::string key_temp;
    Keymaster keymaster;
    if (!keymaster) return false;
    if (!keymaster.exportKey(format, kmKey, "!", "!", &key_temp)) return false;
    *key = KeyBuffer(key_temp.size());
    memcpy(reinterpret_cast<void*>(key->data()), key_temp.c_str(), key->size());
    return true;
}

static std::pair<km::AuthorizationSet, km::HardwareAuthToken> beginParams(
    const KeyAuthentication& auth, const std::string& appId) {
    auto paramBuilder = km::AuthorizationSetBuilder()
                            .GcmModeMacLen(GCM_MAC_BYTES * 8)
                            .Authorization(km::TAG_APPLICATION_ID, km::support::blob2hidlVec(appId));
    km::HardwareAuthToken authToken;
    if (!auth.token.empty()) {
        LOG(DEBUG) << "Supplying auth token to Keymaster" << std::endl;
        authToken = km::support::hidlVec2AuthToken(km::support::blob2hidlVec(auth.token));
    }
    return {paramBuilder, authToken};
}

static bool readFileToString(const std::string& filename, std::string* result) {
    if (!android::base::ReadFileToString(filename, result)) {
        PLOG(ERROR) << "Failed to read from " << filename << std::endl;
        return false;
    }
    return true;
}

static bool writeStringToFile(const std::string& payload, const std::string& filename) {
	PLOG(ERROR) << __FUNCTION__ << " called for " << filename << " and being skipped\n";
	return true;
    android::base::unique_fd fd(TEMP_FAILURE_RETRY(
        open(filename.c_str(), O_WRONLY | O_CREAT | O_NOFOLLOW | O_TRUNC | O_CLOEXEC, 0666)));
    if (fd == -1) {
        PLOG(ERROR) << "Failed to open " << filename;
        return false;
    }
    if (!android::base::WriteStringToFd(payload, fd)) {
        PLOG(ERROR) << "Failed to write to " << filename;
        unlink(filename.c_str());
        return false;
    }
    // fsync as close won't guarantee flush data
    // see close(2), fsync(2) and b/68901441
    if (fsync(fd) == -1) {
        if (errno == EROFS || errno == EINVAL) {
            PLOG(WARNING) << "Skip fsync " << filename
                          << " on a file system does not support synchronization";
        } else {
            PLOG(ERROR) << "Failed to fsync " << filename;
            unlink(filename.c_str());
            return false;
        }
    }
    return true;
}

static bool readRandomBytesOrLog(size_t count, std::string* out) {
    auto status = ReadRandomBytes(count, *out);
    if (status != OK) {
        LOG(ERROR) << "Random read failed with status: " << status << std::endl;
        return false;
    }
    return true;
}

bool createSecdiscardable(const std::string& filename, std::string* hash) {
    std::string secdiscardable;
    if (!readRandomBytesOrLog(SECDISCARDABLE_BYTES, &secdiscardable)) return false;
    if (!writeStringToFile(secdiscardable, filename)) return false;
    hashWithPrefix(kHashPrefix_secdiscardable, secdiscardable, hash);
    return true;
}

bool readSecdiscardable(const std::string& filename, std::string* hash) {
    std::string secdiscardable;
    if (!readFileToString(filename, &secdiscardable)) return false;
    hashWithPrefix(kHashPrefix_secdiscardable, secdiscardable, hash);
    return true;
}

static KeymasterOperation begin(Keymaster& keymaster, const std::string& dir,
                                km::KeyPurpose purpose, const km::AuthorizationSet& keyParams,
                                const km::AuthorizationSet& opParams,
                                const km::HardwareAuthToken& authToken,
                                km::AuthorizationSet* outParams) {
    auto kmKeyPath = dir + "/" + kFn_keymaster_key_blob;
    std::string kmKey;
    if (!readFileToString(kmKeyPath, &kmKey)) return KeymasterOperation();
    km::AuthorizationSet inParams(keyParams);
    inParams.append(opParams.begin(), opParams.end());
    for (;;) {
        auto opHandle = keymaster.begin(purpose, kmKey, inParams, authToken, outParams);
        if (opHandle) {
            return opHandle;
        }
        if (opHandle.errorCode() != km::ErrorCode::KEY_REQUIRES_UPGRADE) return opHandle;
        LOG(DEBUG) << "Upgrading key in memory only: " << dir << std::endl;
        std::string newKey;
        if (!keymaster.upgradeKey(kmKey, keyParams, &newKey)) return KeymasterOperation();
        /*auto newKeyPath = dir + "/" + kFn_keymaster_key_blob_upgraded;
        if (!writeStringToFile(newKey, newKeyPath)) return KeymasterOperation();
        if (rename(newKeyPath.c_str(), kmKeyPath.c_str()) != 0) {
            PLOG(ERROR) << "Unable to move upgraded key to location: " << kmKeyPath;
            return KeymasterOperation();
        }
        if (!keymaster.deleteKey(kmKey)) {
            LOG(ERROR) << "Key deletion failed during upgrade, continuing anyway: " << dir;
        }*/
        kmKey = newKey;
        LOG(INFO) << "Key upgraded in memory but not updated in folder: " << dir << std::endl;
    }
}

static bool encryptWithKeymasterKey(Keymaster& keymaster, const std::string& dir,
                                    const km::AuthorizationSet& keyParams,
                                    const km::HardwareAuthToken& authToken,
                                    const KeyBuffer& message, std::string* ciphertext) {
    km::AuthorizationSet opParams;
    km::AuthorizationSet outParams;
    auto opHandle =
        begin(keymaster, dir, km::KeyPurpose::ENCRYPT, keyParams, opParams, authToken, &outParams);
    if (!opHandle) return false;
    auto nonceBlob = outParams.GetTagValue(km::TAG_NONCE);
    if (!nonceBlob.isOk()) {
        LOG(ERROR) << "GCM encryption but no nonce generated" << std::endl;
        return false;
    }
    // nonceBlob here is just a pointer into existing data, must not be freed
    std::string nonce(reinterpret_cast<const char*>(&nonceBlob.value()[0]),
                      nonceBlob.value().size());
    if (!checkSize("nonce", nonce.size(), GCM_NONCE_BYTES)) return false;
    std::string body;
    if (!opHandle.updateCompletely(message, &body)) return false;

    std::string mac;
    if (!opHandle.finish(&mac)) return false;
    if (!checkSize("mac", mac.size(), GCM_MAC_BYTES)) return false;
    *ciphertext = nonce + body + mac;
    return true;
}

static bool decryptWithKeymasterKey(Keymaster& keymaster, const std::string& dir,
                                    const km::AuthorizationSet& keyParams,
                                    const km::HardwareAuthToken& authToken,
                                    const std::string& ciphertext, KeyBuffer* message) {
    auto nonce = ciphertext.substr(0, GCM_NONCE_BYTES);
    auto bodyAndMac = ciphertext.substr(GCM_NONCE_BYTES);
    auto opParams = km::AuthorizationSetBuilder().Authorization(km::TAG_NONCE,
                                                                km::support::blob2hidlVec(nonce));
    auto opHandle =
        begin(keymaster, dir, km::KeyPurpose::DECRYPT, keyParams, opParams, authToken, nullptr);
    if (!opHandle) return false;
    if (!opHandle.updateCompletely(bodyAndMac, message)) return false;
    if (!opHandle.finish(nullptr)) return false;
    return true;
}

static std::string getStretching(const KeyAuthentication& auth) {
    if (!auth.usesKeymaster()) {
        return kStretch_none;
    } else if (auth.secret.empty()) {
        return kStretch_nopassword;
    } else {
        char paramstr[PROPERTY_VALUE_MAX];

        property_get(SCRYPT_PROP, paramstr, SCRYPT_DEFAULTS);
        return std::string() + kStretchPrefix_scrypt + paramstr;
    }
}

static bool stretchingNeedsSalt(const std::string& stretching) {
    return stretching != kStretch_nopassword && stretching != kStretch_none;
}

static bool stretchSecret(const std::string& stretching, const std::string& secret,
                          const std::string& salt, std::string* stretched) {
    if (stretching == kStretch_nopassword) {
        if (!secret.empty()) {
            LOG(WARNING) << "Password present but stretching is nopassword" << std::endl;
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
            LOG(ERROR) << "Unable to parse scrypt params in stretching: " << stretching << std::endl;
            return false;
        }
        stretched->assign(STRETCHED_BYTES, '\0');
        if (crypto_scrypt(reinterpret_cast<const uint8_t*>(secret.data()), secret.size(),
                          reinterpret_cast<const uint8_t*>(salt.data()), salt.size(), 1 << Nf,
                          1 << rf, 1 << pf, reinterpret_cast<uint8_t*>(&(*stretched)[0]),
                          stretched->size()) != 0) {
            LOG(ERROR) << "scrypt failed with params: " << stretching << std::endl;
            return false;
        }
    } else {
        LOG(ERROR) << "Unknown stretching type: " << stretching << std::endl;
        return false;
    }
    return true;
}

static bool generateAppId(const KeyAuthentication& auth, const std::string& stretching,
                          const std::string& salt, const std::string& secdiscardable_hash,
                          std::string* appId) {
    std::string stretched;
    if (!stretchSecret(stretching, auth.secret, salt, &stretched)) return false;
    *appId = secdiscardable_hash + stretched;
    return true;
}

static void logOpensslError() {
    LOG(ERROR) << "Openssl error: " << ERR_get_error() << std::endl;
}

static bool encryptWithoutKeymaster(const std::string& preKey, const KeyBuffer& plaintext,
                                    std::string* ciphertext) {
    std::string key;
    hashWithPrefix(kHashPrefix_keygen, preKey, &key);
    key.resize(AES_KEY_BYTES);
    if (!readRandomBytesOrLog(GCM_NONCE_BYTES, ciphertext)) return false;
    auto ctx = std::unique_ptr<EVP_CIPHER_CTX, decltype(&::EVP_CIPHER_CTX_free)>(
        EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!ctx) {
        logOpensslError();
        return false;
    }
    if (1 != EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), NULL,
                                reinterpret_cast<const uint8_t*>(key.data()),
                                reinterpret_cast<const uint8_t*>(ciphertext->data()))) {
        logOpensslError();
        return false;
    }
    ciphertext->resize(GCM_NONCE_BYTES + plaintext.size() + GCM_MAC_BYTES);
    int outlen;
    if (1 != EVP_EncryptUpdate(
                 ctx.get(), reinterpret_cast<uint8_t*>(&(*ciphertext)[0] + GCM_NONCE_BYTES),
                 &outlen, reinterpret_cast<const uint8_t*>(plaintext.data()), plaintext.size())) {
        logOpensslError();
        return false;
    }
    if (outlen != static_cast<int>(plaintext.size())) {
        LOG(ERROR) << "GCM ciphertext length should be " << plaintext.size() << " was " << outlen << std::endl;
        return false;
    }
    if (1 != EVP_EncryptFinal_ex(
                 ctx.get(),
                 reinterpret_cast<uint8_t*>(&(*ciphertext)[0] + GCM_NONCE_BYTES + plaintext.size()),
                 &outlen)) {
        logOpensslError();
        return false;
    }
    if (outlen != 0) {
        LOG(ERROR) << "GCM EncryptFinal should be 0, was " << outlen << std::endl;
        return false;
    }
    if (1 != EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, GCM_MAC_BYTES,
                                 reinterpret_cast<uint8_t*>(&(*ciphertext)[0] + GCM_NONCE_BYTES +
                                                            plaintext.size()))) {
        logOpensslError();
        return false;
    }
    return true;
}

static bool decryptWithoutKeymaster(const std::string& preKey, const std::string& ciphertext,
                                    KeyBuffer* plaintext) {
    if (ciphertext.size() < GCM_NONCE_BYTES + GCM_MAC_BYTES) {
        LOG(ERROR) << "GCM ciphertext too small: " << ciphertext.size() << std::endl;
        return false;
    }
    std::string key;
    hashWithPrefix(kHashPrefix_keygen, preKey, &key);
    key.resize(AES_KEY_BYTES);
    auto ctx = std::unique_ptr<EVP_CIPHER_CTX, decltype(&::EVP_CIPHER_CTX_free)>(
        EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!ctx) {
        logOpensslError();
        return false;
    }
    if (1 != EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), NULL,
                                reinterpret_cast<const uint8_t*>(key.data()),
                                reinterpret_cast<const uint8_t*>(ciphertext.data()))) {
        logOpensslError();
        return false;
    }
    *plaintext = KeyBuffer(ciphertext.size() - GCM_NONCE_BYTES - GCM_MAC_BYTES);
    int outlen;
    if (1 != EVP_DecryptUpdate(ctx.get(), reinterpret_cast<uint8_t*>(&(*plaintext)[0]), &outlen,
                               reinterpret_cast<const uint8_t*>(ciphertext.data() + GCM_NONCE_BYTES),
                               plaintext->size())) {
        logOpensslError();
        return false;
    }
    if (outlen != static_cast<int>(plaintext->size())) {
        LOG(ERROR) << "GCM plaintext length should be " << plaintext->size() << " was " << outlen << std::endl;
        return false;
    }
    if (1 != EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, GCM_MAC_BYTES,
                                 const_cast<void*>(reinterpret_cast<const void*>(
                                     ciphertext.data() + GCM_NONCE_BYTES + plaintext->size())))) {
        logOpensslError();
        return false;
    }
    if (1 != EVP_DecryptFinal_ex(ctx.get(),
                                 reinterpret_cast<uint8_t*>(&(*plaintext)[0] + plaintext->size()),
                                 &outlen)) {
        logOpensslError();
        return false;
    }
    if (outlen != 0) {
        LOG(ERROR) << "GCM EncryptFinal should be 0, was " << outlen << std::endl;
        return false;
    }
    return true;
}

bool pathExists(const std::string& path) {
    return access(path.c_str(), F_OK) == 0;
}

bool storeKey(const std::string& dir, const KeyAuthentication& auth, const KeyBuffer& key) {
    if (TEMP_FAILURE_RETRY(mkdir(dir.c_str(), 0700)) == -1) {
        PLOG(ERROR) << "key mkdir " << dir << std::endl;
        return false;
    }
    if (!writeStringToFile(kCurrentVersion, dir + "/" + kFn_version)) return false;
    std::string secdiscardable_hash;
    if (!createSecdiscardable(dir + "/" + kFn_secdiscardable, &secdiscardable_hash)) return false;
    std::string stretching = getStretching(auth);
    if (!writeStringToFile(stretching, dir + "/" + kFn_stretching)) return false;
    std::string salt;
    if (stretchingNeedsSalt(stretching)) {
        if (ReadRandomBytes(SALT_BYTES, salt) != OK) {
            LOG(ERROR) << "Random read failed" << std::endl;
            return false;
        }
        if (!writeStringToFile(salt, dir + "/" + kFn_salt)) return false;
    }
    std::string appId;
    if (!generateAppId(auth, stretching, salt, secdiscardable_hash, &appId)) return false;
    std::string encryptedKey;
    if (auth.usesKeymaster()) {
        Keymaster keymaster;
        if (!keymaster) return false;
        std::string kmKey;
        if (!generateKeymasterKey(keymaster, auth, appId, &kmKey)) return false;
        if (!writeStringToFile(kmKey, dir + "/" + kFn_keymaster_key_blob)) return false;
        km::AuthorizationSet keyParams;
        km::HardwareAuthToken authToken;
        std::tie(keyParams, authToken) = beginParams(auth, appId);
        if (!encryptWithKeymasterKey(keymaster, dir, keyParams, authToken, key, &encryptedKey))
            return false;
    } else {
        if (!encryptWithoutKeymaster(appId, key, &encryptedKey)) return false;
    }
    if (!writeStringToFile(encryptedKey, dir + "/" + kFn_encrypted_key)) return false;
    return true;
}

bool storeKeyAtomically(const std::string& key_path, const std::string& tmp_path,
                        const KeyAuthentication& auth, const KeyBuffer& key) {
    if (pathExists(key_path)) {
        LOG(ERROR) << "Already exists, cannot create key at: " << key_path << std::endl;
        return false;
    }
    if (pathExists(tmp_path)) {
        LOG(DEBUG) << "Already exists, destroying: " << tmp_path << std::endl;
        destroyKey(tmp_path);  // May be partially created so ignore errors
    }
    if (!storeKey(tmp_path, auth, key)) return false;
    if (rename(tmp_path.c_str(), key_path.c_str()) != 0) {
        PLOG(ERROR) << "Unable to move new key to location: " << key_path << std::endl;
        return false;
    }
    LOG(DEBUG) << "Created key: " << key_path << std::endl;
    return true;
}

bool retrieveKey(const std::string& dir, const KeyAuthentication& auth, KeyBuffer* key) {
    std::string version;
    if (!readFileToString(dir + "/" + kFn_version, &version)) return false;
    if (version != kCurrentVersion) {
        LOG(ERROR) << "Version mismatch, expected " << kCurrentVersion << " got " << version << std::endl;
        return false;
    }
    std::string secdiscardable_hash;
    if (!readSecdiscardable(dir + "/" + kFn_secdiscardable, &secdiscardable_hash)) return false;
    std::string stretching;
    if (!readFileToString(dir + "/" + kFn_stretching, &stretching)) return false;
    std::string salt;
    if (stretchingNeedsSalt(stretching)) {
        if (!readFileToString(dir + "/" + kFn_salt, &salt)) return false;
    }
    std::string appId;
    if (!generateAppId(auth, stretching, salt, secdiscardable_hash, &appId)) return false;
    std::string encryptedMessage;
    if (!readFileToString(dir + "/" + kFn_encrypted_key, &encryptedMessage)) return false;
    if (auth.usesKeymaster()) {
        Keymaster keymaster;
        if (!keymaster) return false;
        km::AuthorizationSet keyParams;
        km::HardwareAuthToken authToken;
        std::tie(keyParams, authToken) = beginParams(auth, appId);
        if (!decryptWithKeymasterKey(keymaster, dir, keyParams, authToken, encryptedMessage, key))
            return false;
    } else {
        if (!decryptWithoutKeymaster(appId, encryptedMessage, key)) return false;
    }
    return true;
}

static bool deleteKey(const std::string& dir) {
	LOG(DEBUG) << "not deleting key in " << __FILE__ << std::endl;
	return true;
    std::string kmKey;
    if (!readFileToString(dir + "/" + kFn_keymaster_key_blob, &kmKey)) return false;
    Keymaster keymaster;
    if (!keymaster) return false;
    if (!keymaster.deleteKey(kmKey)) return false;
    return true;
}

bool runSecdiscardSingle(const std::string& file) {
    if (ForkExecvp(std::vector<std::string>{kSecdiscardPath, "--", file}) != 0) {
        LOG(ERROR) << "secdiscard failed" << std::endl;
        return false;
    }
    return true;
}

static bool recursiveDeleteKey(const std::string& dir) {
	LOG(DEBUG) << "not recursively deleting key in " << __FILE__ << std::endl;
	return true;
    if (ForkExecvp(std::vector<std::string>{kRmPath, "-rf", dir}) != 0) {
        LOG(ERROR) << "recursive delete failed" << std::endl;
        return false;
    }
    return true;
}

bool destroyKey(const std::string& dir) {
	LOG(DEBUG) << "not destroying key in " << __FILE__ << std::endl;
	return true;
    bool success = true;
    // Try each thing, even if previous things failed.
    bool uses_km = pathExists(dir + "/" + kFn_keymaster_key_blob);
    if (uses_km) {
        success &= deleteKey(dir);
    }
    auto secdiscard_cmd = std::vector<std::string>{
        kSecdiscardPath, "--", dir + "/" + kFn_encrypted_key, dir + "/" + kFn_secdiscardable,
    };
    if (uses_km) {
        secdiscard_cmd.emplace_back(dir + "/" + kFn_keymaster_key_blob);
    }
    if (ForkExecvp(secdiscard_cmd) != 0) {
        LOG(ERROR) << "secdiscard failed" << std::endl;
        success = false;
    }
    success &= recursiveDeleteKey(dir);
    return success;
}

}  // namespace vold
}  // namespace android
