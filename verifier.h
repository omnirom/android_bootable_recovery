/*
 * Copyright (C) 2008 The Android Open Source Project
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

#ifndef _RECOVERY_VERIFIER_H
#define _RECOVERY_VERIFIER_H

#include <stdint.h>

#include <functional>
#include <memory>
#include <vector>

#include <openssl/ec_key.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>

struct RSADeleter {
  void operator()(RSA* rsa) const {
    RSA_free(rsa);
  }
};

struct ECKEYDeleter {
  void operator()(EC_KEY* ec_key) const {
    EC_KEY_free(ec_key);
  }
};

struct Certificate {
    typedef enum {
        KEY_TYPE_RSA,
        KEY_TYPE_EC,
    } KeyType;

    Certificate(int hash_len_,
                KeyType key_type_,
                std::unique_ptr<RSA, RSADeleter>&& rsa_,
                std::unique_ptr<EC_KEY, ECKEYDeleter>&& ec_)
        : hash_len(hash_len_),
          key_type(key_type_),
          rsa(std::move(rsa_)),
          ec(std::move(ec_)) {}

    // SHA_DIGEST_LENGTH (SHA-1) or SHA256_DIGEST_LENGTH (SHA-256)
    int hash_len;
    KeyType key_type;
    std::unique_ptr<RSA, RSADeleter> rsa;
    std::unique_ptr<EC_KEY, ECKEYDeleter> ec;
};

/*
 * 'addr' and 'length' define an update package file that has been loaded (or mmap'ed, or
 * whatever) into memory. Verifies that the file is signed and the signature matches one of the
 * given keys. It optionally accepts a callback function for posting the progress to. Returns one
 * of the constants of VERIFY_SUCCESS and VERIFY_FAILURE.
 */
int verify_file(const unsigned char* addr, size_t length, const std::vector<Certificate>& keys,
                const std::function<void(float)>& set_progress = nullptr);

// Checks that the RSA key has a modulus of 2048 bits long, and public exponent is 3 or 65537.
bool CheckRSAKey(const std::unique_ptr<RSA, RSADeleter>& rsa);

// Checks that the field size of the curve for the EC key is 256 bits.
bool CheckECKey(const std::unique_ptr<EC_KEY, ECKEYDeleter>& ec_key);

// Parses a PEM-encoded x509 certificate from the given buffer and saves it into |cert|. Returns
// false if there is a parsing failure or the signature's encryption algorithm is not supported.
bool LoadCertificateFromBuffer(const std::vector<uint8_t>& pem_content, Certificate* cert);

// Iterates over the zip entries with the suffix "x509.pem" and returns a list of recognized
// certificates. Returns an empty list if we fail to parse any of the entries.
std::vector<Certificate> LoadKeysFromZipfile(const std::string& zip_name);

#define VERIFY_SUCCESS        0
#define VERIFY_FAILURE        1

#endif  /* _RECOVERY_VERIFIER_H */
