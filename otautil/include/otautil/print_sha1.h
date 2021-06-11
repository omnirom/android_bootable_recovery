/*
 * Copyright (C) 2015 The Android Open Source Project
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

#ifndef RECOVERY_PRINT_SHA1_H
#define RECOVERY_PRINT_SHA1_H

#include <stdint.h>
#include <string>

#include <openssl/sha.h>

static std::string print_sha1(const uint8_t* sha1, size_t len) {
  const char* hex = "0123456789abcdef";
  std::string result = "";
  for (size_t i = 0; i < len; ++i) {
    result.push_back(hex[(sha1[i] >> 4) & 0xf]);
    result.push_back(hex[sha1[i] & 0xf]);
  }
  return result;
}

[[maybe_unused]] static std::string print_sha1(const uint8_t sha1[SHA_DIGEST_LENGTH]) {
  return print_sha1(sha1, SHA_DIGEST_LENGTH);
}

[[maybe_unused]] static std::string short_sha1(const uint8_t sha1[SHA_DIGEST_LENGTH]) {
  return print_sha1(sha1, 4);
}

[[maybe_unused]] static std::string print_hex(const uint8_t* bytes, size_t len) {
  return print_sha1(bytes, len);
}

#endif  // RECOVERY_PRINT_SHA1_H
