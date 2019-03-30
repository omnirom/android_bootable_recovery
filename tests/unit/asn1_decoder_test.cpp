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

#include <memory>

#include <gtest/gtest.h>

#include "private/asn1_decoder.h"

TEST(Asn1DecoderTest, Empty_Failure) {
  uint8_t empty[] = {};
  asn1_context ctx(empty, sizeof(empty));

  ASSERT_EQ(nullptr, ctx.asn1_constructed_get());
  ASSERT_FALSE(ctx.asn1_constructed_skip_all());
  ASSERT_EQ(0, ctx.asn1_constructed_type());
  ASSERT_EQ(nullptr, ctx.asn1_sequence_get());
  ASSERT_EQ(nullptr, ctx.asn1_set_get());
  ASSERT_FALSE(ctx.asn1_sequence_next());

  const uint8_t* junk;
  size_t length;
  ASSERT_FALSE(ctx.asn1_oid_get(&junk, &length));
  ASSERT_FALSE(ctx.asn1_octet_string_get(&junk, &length));
}

TEST(Asn1DecoderTest, ConstructedGet_TruncatedLength_Failure) {
  uint8_t truncated[] = { 0xA0, 0x82 };
  asn1_context ctx(truncated, sizeof(truncated));
  ASSERT_EQ(nullptr, ctx.asn1_constructed_get());
}

TEST(Asn1DecoderTest, ConstructedGet_LengthTooBig_Failure) {
  uint8_t truncated[] = { 0xA0, 0x8a, 0xA5, 0x5A, 0xA5, 0x5A, 0xA5, 0x5A, 0xA5, 0x5A, 0xA5, 0x5A };
  asn1_context ctx(truncated, sizeof(truncated));
  ASSERT_EQ(nullptr, ctx.asn1_constructed_get());
}

TEST(Asn1DecoderTest, ConstructedGet_TooSmallForChild_Failure) {
  uint8_t data[] = { 0xA5, 0x02, 0x06, 0x01, 0x01 };
  asn1_context ctx(data, sizeof(data));
  std::unique_ptr<asn1_context> ptr(ctx.asn1_constructed_get());
  ASSERT_NE(nullptr, ptr);
  ASSERT_EQ(5, ptr->asn1_constructed_type());
  const uint8_t* oid;
  size_t length;
  ASSERT_FALSE(ptr->asn1_oid_get(&oid, &length));
}

TEST(Asn1DecoderTest, ConstructedGet_Success) {
  uint8_t data[] = { 0xA5, 0x03, 0x06, 0x01, 0x01 };
  asn1_context ctx(data, sizeof(data));
  std::unique_ptr<asn1_context> ptr(ctx.asn1_constructed_get());
  ASSERT_NE(nullptr, ptr);
  ASSERT_EQ(5, ptr->asn1_constructed_type());
  const uint8_t* oid;
  size_t length;
  ASSERT_TRUE(ptr->asn1_oid_get(&oid, &length));
  ASSERT_EQ(1U, length);
  ASSERT_EQ(0x01U, *oid);
}

TEST(Asn1DecoderTest, ConstructedSkipAll_TruncatedLength_Failure) {
  uint8_t truncated[] = { 0xA2, 0x82 };
  asn1_context ctx(truncated, sizeof(truncated));
  ASSERT_FALSE(ctx.asn1_constructed_skip_all());
}

TEST(Asn1DecoderTest, ConstructedSkipAll_Success) {
  uint8_t data[] = { 0xA0, 0x03, 0x02, 0x01, 0x01, 0xA1, 0x03, 0x02, 0x01, 0x01, 0x06, 0x01, 0xA5 };
  asn1_context ctx(data, sizeof(data));
  ASSERT_TRUE(ctx.asn1_constructed_skip_all());
  const uint8_t* oid;
  size_t length;
  ASSERT_TRUE(ctx.asn1_oid_get(&oid, &length));
  ASSERT_EQ(1U, length);
  ASSERT_EQ(0xA5U, *oid);
}

TEST(Asn1DecoderTest, SequenceGet_TruncatedLength_Failure) {
  uint8_t truncated[] = { 0x30, 0x82 };
  asn1_context ctx(truncated, sizeof(truncated));
  ASSERT_EQ(nullptr, ctx.asn1_sequence_get());
}

TEST(Asn1DecoderTest, SequenceGet_TooSmallForChild_Failure) {
  uint8_t data[] = { 0x30, 0x02, 0x06, 0x01, 0x01 };
  asn1_context ctx(data, sizeof(data));
  std::unique_ptr<asn1_context> ptr(ctx.asn1_sequence_get());
  ASSERT_NE(nullptr, ptr);
  const uint8_t* oid;
  size_t length;
  ASSERT_FALSE(ptr->asn1_oid_get(&oid, &length));
}

TEST(Asn1DecoderTest, SequenceGet_Success) {
  uint8_t data[] = { 0x30, 0x03, 0x06, 0x01, 0x01 };
  asn1_context ctx(data, sizeof(data));
  std::unique_ptr<asn1_context> ptr(ctx.asn1_sequence_get());
  ASSERT_NE(nullptr, ptr);
  const uint8_t* oid;
  size_t length;
  ASSERT_TRUE(ptr->asn1_oid_get(&oid, &length));
  ASSERT_EQ(1U, length);
  ASSERT_EQ(0x01U, *oid);
}

TEST(Asn1DecoderTest, SetGet_TruncatedLength_Failure) {
  uint8_t truncated[] = { 0x31, 0x82 };
  asn1_context ctx(truncated, sizeof(truncated));
  ASSERT_EQ(nullptr, ctx.asn1_set_get());
}

TEST(Asn1DecoderTest, SetGet_TooSmallForChild_Failure) {
  uint8_t data[] = { 0x31, 0x02, 0x06, 0x01, 0x01 };
  asn1_context ctx(data, sizeof(data));
  std::unique_ptr<asn1_context> ptr(ctx.asn1_set_get());
  ASSERT_NE(nullptr, ptr);
  const uint8_t* oid;
  size_t length;
  ASSERT_FALSE(ptr->asn1_oid_get(&oid, &length));
}

TEST(Asn1DecoderTest, SetGet_Success) {
  uint8_t data[] = { 0x31, 0x03, 0x06, 0x01, 0xBA };
  asn1_context ctx(data, sizeof(data));
  std::unique_ptr<asn1_context> ptr(ctx.asn1_set_get());
  ASSERT_NE(nullptr, ptr);
  const uint8_t* oid;
  size_t length;
  ASSERT_TRUE(ptr->asn1_oid_get(&oid, &length));
  ASSERT_EQ(1U, length);
  ASSERT_EQ(0xBAU, *oid);
}

TEST(Asn1DecoderTest, OidGet_LengthZero_Failure) {
  uint8_t data[] = { 0x06, 0x00, 0x01 };
  asn1_context ctx(data, sizeof(data));
  const uint8_t* oid;
  size_t length;
  ASSERT_FALSE(ctx.asn1_oid_get(&oid, &length));
}

TEST(Asn1DecoderTest, OidGet_TooSmall_Failure) {
  uint8_t data[] = { 0x06, 0x01 };
  asn1_context ctx(data, sizeof(data));
  const uint8_t* oid;
  size_t length;
  ASSERT_FALSE(ctx.asn1_oid_get(&oid, &length));
}

TEST(Asn1DecoderTest, OidGet_Success) {
  uint8_t data[] = { 0x06, 0x01, 0x99 };
  asn1_context ctx(data, sizeof(data));
  const uint8_t* oid;
  size_t length;
  ASSERT_TRUE(ctx.asn1_oid_get(&oid, &length));
  ASSERT_EQ(1U, length);
  ASSERT_EQ(0x99U, *oid);
}

TEST(Asn1DecoderTest, OctetStringGet_LengthZero_Failure) {
  uint8_t data[] = { 0x04, 0x00, 0x55 };
  asn1_context ctx(data, sizeof(data));
  const uint8_t* string;
  size_t length;
  ASSERT_FALSE(ctx.asn1_octet_string_get(&string, &length));
}

TEST(Asn1DecoderTest, OctetStringGet_TooSmall_Failure) {
  uint8_t data[] = { 0x04, 0x01 };
  asn1_context ctx(data, sizeof(data));
  const uint8_t* string;
  size_t length;
  ASSERT_FALSE(ctx.asn1_octet_string_get(&string, &length));
}

TEST(Asn1DecoderTest, OctetStringGet_Success) {
  uint8_t data[] = { 0x04, 0x01, 0xAA };
  asn1_context ctx(data, sizeof(data));
  const uint8_t* string;
  size_t length;
  ASSERT_TRUE(ctx.asn1_octet_string_get(&string, &length));
  ASSERT_EQ(1U, length);
  ASSERT_EQ(0xAAU, *string);
}
