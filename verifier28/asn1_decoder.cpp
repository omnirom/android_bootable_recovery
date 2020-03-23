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

#include "asn1_decoder.h"

#include <stdint.h>

int asn1_context::peek_byte() const {
  if (length_ == 0) {
    return -1;
  }
  return *p_;
}

int asn1_context::get_byte() {
  if (length_ == 0) {
    return -1;
  }

  int byte = *p_;
  p_++;
  length_--;
  return byte;
}

bool asn1_context::skip_bytes(size_t num_skip) {
  if (length_ < num_skip) {
    return false;
  }
  p_ += num_skip;
  length_ -= num_skip;
  return true;
}

bool asn1_context::decode_length(size_t* out_len) {
  int num_octets = get_byte();
  if (num_octets == -1) {
    return false;
  }
  if ((num_octets & 0x80) == 0x00) {
    *out_len = num_octets;
    return true;
  }
  num_octets &= kMaskTag;
  if (static_cast<size_t>(num_octets) >= sizeof(size_t)) {
    return false;
  }
  size_t length = 0;
  for (int i = 0; i < num_octets; ++i) {
    int byte = get_byte();
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
asn1_context* asn1_context::asn1_constructed_get() {
  int type = get_byte();
  if (type == -1 || (type & kMaskConstructed) != kTagConstructed) {
    return nullptr;
  }
  size_t length;
  if (!decode_length(&length) || length > length_) {
    return nullptr;
  }
  asn1_context* app_ctx = new asn1_context(p_, length);
  app_ctx->app_type_ = type & kMaskAppType;
  return app_ctx;
}

bool asn1_context::asn1_constructed_skip_all() {
  int byte = peek_byte();
  while (byte != -1 && (byte & kMaskConstructed) == kTagConstructed) {
    skip_bytes(1);
    size_t length;
    if (!decode_length(&length) || !skip_bytes(length)) {
      return false;
    }
    byte = peek_byte();
  }
  return byte != -1;
}

int asn1_context::asn1_constructed_type() const {
  return app_type_;
}

asn1_context* asn1_context::asn1_sequence_get() {
  if ((get_byte() & kMaskTag) != kTagSequence) {
    return nullptr;
  }
  size_t length;
  if (!decode_length(&length) || length > length_) {
    return nullptr;
  }
  return new asn1_context(p_, length);
}

asn1_context* asn1_context::asn1_set_get() {
  if ((get_byte() & kMaskTag) != kTagSet) {
    return nullptr;
  }
  size_t length;
  if (!decode_length(&length) || length > length_) {
    return nullptr;
  }
  return new asn1_context(p_, length);
}

bool asn1_context::asn1_sequence_next() {
  size_t length;
  if (get_byte() == -1 || !decode_length(&length) || !skip_bytes(length)) {
    return false;
  }
  return true;
}

bool asn1_context::asn1_oid_get(const uint8_t** oid, size_t* length) {
  if (get_byte() != kTagOid) {
    return false;
  }
  if (!decode_length(length) || *length == 0 || *length > length_) {
    return false;
  }
  *oid = p_;
  return true;
}

bool asn1_context::asn1_octet_string_get(const uint8_t** octet_string, size_t* length) {
  if (get_byte() != kTagOctetString) {
    return false;
  }
  if (!decode_length(length) || *length == 0 || *length > length_) {
    return false;
  }
  *octet_string = p_;
  return true;
}
