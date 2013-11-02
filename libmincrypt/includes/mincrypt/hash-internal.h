// Copyright 2007 Google Inc. All Rights Reserved.
// Author: mschilder@google.com (Marius Schilder)

#ifndef SECURITY_UTIL_LITE_HASH_INTERNAL_H__
#define SECURITY_UTIL_LITE_HASH_INTERNAL_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

struct HASH_CTX;  // forward decl

typedef struct HASH_VTAB {
  void (* const init)(struct HASH_CTX*);
  void (* const update)(struct HASH_CTX*, const void*, int);
  const uint8_t* (* const final)(struct HASH_CTX*);
  const uint8_t* (* const hash)(const void*, int, uint8_t*);
  int size;
} HASH_VTAB;

typedef struct HASH_CTX {
  const HASH_VTAB * f;
  uint64_t count;
  uint8_t buf[64];
  uint32_t state[8];  // upto SHA2
} HASH_CTX;

#define HASH_init(ctx) (ctx)->f->init(ctx)
#define HASH_update(ctx, data, len) (ctx)->f->update(ctx, data, len)
#define HASH_final(ctx) (ctx)->f->final(ctx)
#define HASH_hash(data, len, digest) (ctx)->f->hash(data, len, digest)
#define HASH_size(ctx) (ctx)->f->size

#ifdef __cplusplus
}
#endif  // __cplusplus

#endif  // SECURITY_UTIL_LITE_HASH_INTERNAL_H__
