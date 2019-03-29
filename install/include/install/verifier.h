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

#pragma once

#include <stdint.h>

#include <functional>
#include <memory>
#include <vector>

#include <openssl/ec_key.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>

constexpr size_t MiB = 1024 * 1024;

using HasherUpdateCallback = std::function<void(const uint8_t* addr, uint64_t size)>;

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

  Certificate(int hash_len_, KeyType key_type_, std::unique_ptr<RSA, RSADeleter>&& rsa_,
              std::unique_ptr<EC_KEY, ECKEYDeleter>&& ec_)
      : hash_len(hash_len_), key_type(key_type_), rsa(std::move(rsa_)), ec(std::move(ec_)) {}

  // SHA_DIGEST_LENGTH (SHA-1) or SHA256_DIGEST_LENGTH (SHA-256)
  int hash_len;
  KeyType key_type;
  std::unique_ptr<RSA, RSADeleter> rsa;
  std::unique_ptr<EC_KEY, ECKEYDeleter> ec;
};

class VerifierInterface {
 public:
  virtual ~VerifierInterface() = default;

  // Returns the package size in bytes.
  virtual uint64_t GetPackageSize() const = 0;

  // Reads |byte_count| data starting from |offset|, and puts the result in |buffer|.
  virtual bool ReadFullyAtOffset(uint8_t* buffer, uint64_t byte_count, uint64_t offset) = 0;

  // Updates the hash contexts for |length| bytes data starting from |start|.
  virtual bool UpdateHashAtOffset(const std::vector<HasherUpdateCallback>& hashers, uint64_t start,
                                  uint64_t length) = 0;

  // Updates the progress in fraction during package verification.
  virtual void SetProgress(float progress) = 0;
};

//  Looks for an RSA signature embedded in the .ZIP file comment given the path to the zip.
//  Verifies that it matches one of the given public keys. Returns VERIFY_SUCCESS or
//  VERIFY_FAILURE (if any error is encountered or no key matches the signature).
int verify_file(VerifierInterface* package, const std::vector<Certificate>& keys);

// Checks that the RSA key has a modulus of 2048 or 4096 bits long, and public exponent is 3 or
// 65537.
bool CheckRSAKey(const std::unique_ptr<RSA, RSADeleter>& rsa);

// Checks that the field size of the curve for the EC key is 256 bits.
bool CheckECKey(const std::unique_ptr<EC_KEY, ECKEYDeleter>& ec_key);

// Parses a PEM-encoded x509 certificate from the given buffer and saves it into |cert|. Returns
// false if there is a parsing failure or the signature's encryption algorithm is not supported.
bool LoadCertificateFromBuffer(const std::vector<uint8_t>& pem_content, Certificate* cert);

// Iterates over the zip entries with the suffix "x509.pem" and returns a list of recognized
// certificates. Returns an empty list if we fail to parse any of the entries.
std::vector<Certificate> LoadKeysFromZipfile(const std::string& zip_name);

#define VERIFY_SUCCESS 0
#define VERIFY_FAILURE 1
