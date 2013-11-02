// Copyright 2005 Google Inc. All Rights Reserved.
// Author: mschilder@google.com (Marius Schilder)

#ifndef SECURITY_UTIL_LITE_SHA1_H__
#define SECURITY_UTIL_LITE_SHA1_H__

#include <stdint.h>
#include "hash-internal.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

typedef HASH_CTX SHA_CTX;

void SHA_init(SHA_CTX* ctx);
void SHA_update(SHA_CTX* ctx, const void* data, int len);
const uint8_t* SHA_final(SHA_CTX* ctx);

// Convenience method. Returns digest address.
// NOTE: *digest needs to hold SHA_DIGEST_SIZE bytes.
const uint8_t* SHA_hash(const void* data, int len, uint8_t* digest);

#define SHA_DIGEST_SIZE 20

#ifdef __cplusplus
}
#endif // __cplusplus

#endif  // SECURITY_UTIL_LITE_SHA1_H__
