/*
 * Copyright (C) 2013 The Android Open Source Project
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

#include <stdint.h>
#include <string.h>

#include "asn1_decoder.h"


typedef struct asn1_context {
    size_t length;
    uint8_t* p;
    int app_type;
} asn1_context_t;


static const int kMaskConstructed = 0xE0;
static const int kMaskTag = 0x7F;
static const int kMaskAppType = 0x1F;

static const int kTagOctetString = 0x04;
static const int kTagOid = 0x06;
static const int kTagSequence = 0x30;
static const int kTagSet = 0x31;
static const int kTagConstructed = 0xA0;

asn1_context_t* asn1_context_new(uint8_t* buffer, size_t length) {
    asn1_context_t* ctx = (asn1_context_t*) calloc(1, sizeof(asn1_context_t));
    if (ctx == NULL) {
        return NULL;
    }
    ctx->p = buffer;
    ctx->length = length;
    return ctx;
}

void asn1_context_free(asn1_context_t* ctx) {
    free(ctx);
}

static inline int peek_byte(asn1_context_t* ctx) {
    if (ctx->length <= 0) {
        return -1;
    }
    return *ctx->p;
}

static inline int get_byte(asn1_context_t* ctx) {
    if (ctx->length <= 0) {
        return -1;
    }
    int byte = *ctx->p;
    ctx->p++;
    ctx->length--;
    return byte;
}

static inline bool skip_bytes(asn1_context_t* ctx, size_t num_skip) {
    if (ctx->length < num_skip) {
        return false;
    }
    ctx->p += num_skip;
    ctx->length -= num_skip;
    return true;
}

static bool decode_length(asn1_context_t* ctx, size_t* out_len) {
    int num_octets = get_byte(ctx);
    if (num_octets == -1) {
        return false;
    }
    if ((num_octets & 0x80) == 0x00) {
        *out_len = num_octets;
        return 1;
    }
    num_octets &= kMaskTag;
    if ((size_t)num_octets >= sizeof(size_t)) {
        return false;
    }
    size_t length = 0;
    for (int i = 0; i < num_octets; ++i) {
        int byte = get_byte(ctx);
        if (byte == -1) {
            return false;
        }
        length <<= 8;
        length += byte;
    }
    *out_len = length;
    return true;
}

/**
 * Returns the constructed type and advances the pointer. E.g. A0 -> 0
 */
asn1_context_t* asn1_constructed_get(asn1_context_t* ctx) {
    int type = get_byte(ctx);
    if (type == -1 || (type & kMaskConstructed) != kTagConstructed) {
        return NULL;
    }
    size_t length;
    if (!decode_length(ctx, &length) || length > ctx->length) {
        return NULL;
    }
    asn1_context_t* app_ctx = asn1_context_new(ctx->p, length);
    app_ctx->app_type = type & kMaskAppType;
    return app_ctx;
}

bool asn1_constructed_skip_all(asn1_context_t* ctx) {
    int byte = peek_byte(ctx);
    while (byte != -1 && (byte & kMaskConstructed) == kTagConstructed) {
        skip_bytes(ctx, 1);
        size_t length;
        if (!decode_length(ctx, &length) || !skip_bytes(ctx, length)) {
            return false;
        }
        byte = peek_byte(ctx);
    }
    return byte != -1;
}

int asn1_constructed_type(asn1_context_t* ctx) {
    return ctx->app_type;
}

asn1_context_t* asn1_sequence_get(asn1_context_t* ctx) {
    if ((get_byte(ctx) & kMaskTag) != kTagSequence) {
        return NULL;
    }
    size_t length;
    if (!decode_length(ctx, &length) || length > ctx->length) {
        return NULL;
    }
    return asn1_context_new(ctx->p, length);
}

asn1_context_t* asn1_set_get(asn1_context_t* ctx) {
    if ((get_byte(ctx) & kMaskTag) != kTagSet) {
        return NULL;
    }
    size_t length;
    if (!decode_length(ctx, &length) || length > ctx->length) {
        return NULL;
    }
    return asn1_context_new(ctx->p, length);
}

bool asn1_sequence_next(asn1_context_t* ctx) {
    size_t length;
    if (get_byte(ctx) == -1 || !decode_length(ctx, &length) || !skip_bytes(ctx, length)) {
        return false;
    }
    return true;
}

bool asn1_oid_get(asn1_context_t* ctx, uint8_t** oid, size_t* length) {
    if (get_byte(ctx) != kTagOid) {
        return false;
    }
    if (!decode_length(ctx, length) || *length == 0 || *length > ctx->length) {
        return false;
    }
    *oid = ctx->p;
    return true;
}

bool asn1_octet_string_get(asn1_context_t* ctx, uint8_t** octet_string, size_t* length) {
    if (get_byte(ctx) != kTagOctetString) {
        return false;
    }
    if (!decode_length(ctx, length) || *length == 0 || *length > ctx->length) {
        return false;
    }
    *octet_string = ctx->p;
    return true;
}
