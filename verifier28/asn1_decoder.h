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

class asn1_context {
 public:
  asn1_context(const uint8_t* buffer, size_t length) : p_(buffer), length_(length), app_type_(0) {}
  int asn1_constructed_type() const;
  asn1_context* asn1_constructed_get();
  bool asn1_constructed_skip_all();
  asn1_context* asn1_sequence_get();
  asn1_context* asn1_set_get();
  bool asn1_sequence_next();
  bool asn1_oid_get(const uint8_t** oid, size_t* length);
  bool asn1_octet_string_get(const uint8_t** octet_string, size_t* length);

 private:
  static constexpr int kMaskConstructed = 0xE0;
  static constexpr int kMaskTag = 0x7F;
  static constexpr int kMaskAppType = 0x1F;

  static constexpr int kTagOctetString = 0x04;
  static constexpr int kTagOid = 0x06;
  static constexpr int kTagSequence = 0x30;
  static constexpr int kTagSet = 0x31;
  static constexpr int kTagConstructed = 0xA0;

  int peek_byte() const;
  int get_byte();
  bool skip_bytes(size_t num_skip);
  bool decode_length(size_t* out_len);

  const uint8_t* p_;
  size_t length_;
  int app_type_;
};

#endif /* ASN1_DECODER_H_ */
