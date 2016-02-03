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

#include <memory>
#include <vector>

#include "mincrypt/p256.h"
#include "mincrypt/rsa.h"

typedef struct {
    p256_int x;
    p256_int y;
} ECPublicKey;

struct Certificate {
    typedef enum {
        RSA,
        EC,
    } KeyType;

    Certificate(int hash_len_, KeyType key_type_,
            std::unique_ptr<RSAPublicKey>&& rsa_,
            std::unique_ptr<ECPublicKey>&& ec_) :
        hash_len(hash_len_),
        key_type(key_type_),
        rsa(std::move(rsa_)),
        ec(std::move(ec_)) { }

    int hash_len;  // SHA_DIGEST_SIZE (SHA-1) or SHA256_DIGEST_SIZE (SHA-256)
    KeyType key_type;
    std::unique_ptr<RSAPublicKey> rsa;
    std::unique_ptr<ECPublicKey> ec;
};

/* addr and length define a an update package file that has been
 * loaded (or mmap'ed, or whatever) into memory.  Verify that the file
 * is signed and the signature matches one of the given keys.  Return
 * one of the constants below.
 */
int verify_file(unsigned char* addr, size_t length,
                const std::vector<Certificate>& keys);

bool load_keys(const char* filename, std::vector<Certificate>& certs);

#define VERIFY_SUCCESS        0
#define VERIFY_FAILURE        1

#endif  /* _RECOVERY_VERIFIER_H */
