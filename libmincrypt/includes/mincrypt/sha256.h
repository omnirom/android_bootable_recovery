// Copyright 2011 Google Inc. All Rights Reserved.
// Author: mschilder@google.com (Marius Schilder)

#ifndef SECURITY_UTIL_LITE_SHA256_H__
#define SECURITY_UTIL_LITE_SHA256_H__

#include <stdint.h>
#include "hash-internal.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

typedef HASH_CTX SHA256_CTX;

void SHA256_init(SHA256_CTX* ctx);
void SHA256_update(SHA256_CTX* ctx, const void* data, int len);
const uint8_t* SHA256_final(SHA256_CTX* ctx);

// Convenience method. Returns digest address.
const uint8_t* SHA256_hash(const void* data, int len, uint8_t* digest);

#define SHA256_DIGEST_SIZE 32

#ifdef __cplusplus
}
#endif // __cplusplus

#endif  // SECURITY_UTIL_LITE_SHA256_H__
