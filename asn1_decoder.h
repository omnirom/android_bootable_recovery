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


#ifndef ASN1_DECODER_H_
#define ASN1_DECODER_H_

#include <stdint.h>

typedef struct asn1_context asn1_context_t;

asn1_context_t* asn1_context_new(uint8_t* buffer, size_t length);
void asn1_context_free(asn1_context_t* ctx);
asn1_context_t* asn1_constructed_get(asn1_context_t* ctx);
bool asn1_constructed_skip_all(asn1_context_t* ctx);
int asn1_constructed_type(asn1_context_t* ctx);
asn1_context_t* asn1_sequence_get(asn1_context_t* ctx);
asn1_context_t* asn1_set_get(asn1_context_t* ctx);
bool asn1_sequence_next(asn1_context_t* seq);
bool asn1_oid_get(asn1_context_t* ctx, uint8_t** oid, size_t* length);
bool asn1_octet_string_get(asn1_context_t* ctx, uint8_t** octet_string, size_t* length);

#endif /* ASN1_DECODER_H_ */
