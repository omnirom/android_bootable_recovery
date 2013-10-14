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

#define LOG_TAG "asn1_decoder_test"

#include <cutils/log.h>
#include <gtest/gtest.h>
#include <stdint.h>
#include <unistd.h>

#include "asn1_decoder.h"

namespace android {

class Asn1DecoderTest : public testing::Test {
};

TEST_F(Asn1DecoderTest, Empty_Failure) {
    uint8_t empty[] = { };
    asn1_context_t* ctx = asn1_context_new(empty, sizeof(empty));

    EXPECT_EQ(NULL, asn1_constructed_get(ctx));
    EXPECT_FALSE(asn1_constructed_skip_all(ctx));
    EXPECT_EQ(0, asn1_constructed_type(ctx));
    EXPECT_EQ(NULL, asn1_sequence_get(ctx));
    EXPECT_EQ(NULL, asn1_set_get(ctx));
    EXPECT_FALSE(asn1_sequence_next(ctx));

    uint8_t* junk;
    size_t length;
    EXPECT_FALSE(asn1_oid_get(ctx, &junk, &length));
    EXPECT_FALSE(asn1_octet_string_get(ctx, &junk, &length));

    asn1_context_free(ctx);
}

TEST_F(Asn1DecoderTest, ConstructedGet_TruncatedLength_Failure) {
    uint8_t truncated[] = { 0xA0, 0x82, };
    asn1_context_t* ctx = asn1_context_new(truncated, sizeof(truncated));
    EXPECT_EQ(NULL, asn1_constructed_get(ctx));
    asn1_context_free(ctx);
}

TEST_F(Asn1DecoderTest, ConstructedGet_LengthTooBig_Failure) {
    uint8_t truncated[] = { 0xA0, 0x8a, 0xA5, 0x5A, 0xA5, 0x5A,
                            0xA5, 0x5A, 0xA5, 0x5A, 0xA5, 0x5A, };
    asn1_context_t* ctx = asn1_context_new(truncated, sizeof(truncated));
    EXPECT_EQ(NULL, asn1_constructed_get(ctx));
    asn1_context_free(ctx);
}

TEST_F(Asn1DecoderTest, ConstructedGet_TooSmallForChild_Failure) {
    uint8_t data[] = { 0xA5, 0x02, 0x06, 0x01, 0x01, };
    asn1_context_t* ctx = asn1_context_new(data, sizeof(data));
    asn1_context_t* ptr = asn1_constructed_get(ctx);
    ASSERT_NE((asn1_context_t*)NULL, ptr);
    EXPECT_EQ(5, asn1_constructed_type(ptr));
    uint8_t* oid;
    size_t length;
    EXPECT_FALSE(asn1_oid_get(ptr, &oid, &length));
    asn1_context_free(ptr);
    asn1_context_free(ctx);
}

TEST_F(Asn1DecoderTest, ConstructedGet_Success) {
    uint8_t data[] = { 0xA5, 0x03, 0x06, 0x01, 0x01, };
    asn1_context_t* ctx = asn1_context_new(data, sizeof(data));
    asn1_context_t* ptr = asn1_constructed_get(ctx);
    ASSERT_NE((asn1_context_t*)NULL, ptr);
    EXPECT_EQ(5, asn1_constructed_type(ptr));
    uint8_t* oid;
    size_t length;
    ASSERT_TRUE(asn1_oid_get(ptr, &oid, &length));
    EXPECT_EQ(1U, length);
    EXPECT_EQ(0x01U, *oid);
    asn1_context_free(ptr);
    asn1_context_free(ctx);
}

TEST_F(Asn1DecoderTest, ConstructedSkipAll_TruncatedLength_Failure) {
    uint8_t truncated[] = { 0xA2, 0x82, };
    asn1_context_t* ctx = asn1_context_new(truncated, sizeof(truncated));
    EXPECT_FALSE(asn1_constructed_skip_all(ctx));
    asn1_context_free(ctx);
}

TEST_F(Asn1DecoderTest, ConstructedSkipAll_Success) {
    uint8_t data[] = { 0xA0, 0x03, 0x02, 0x01, 0x01,
                            0xA1, 0x03, 0x02, 0x01, 0x01,
                            0x06, 0x01, 0xA5, };
    asn1_context_t* ctx = asn1_context_new(data, sizeof(data));
    ASSERT_TRUE(asn1_constructed_skip_all(ctx));
    uint8_t* oid;
    size_t length;
    ASSERT_TRUE(asn1_oid_get(ctx, &oid, &length));
    EXPECT_EQ(1U, length);
    EXPECT_EQ(0xA5U, *oid);
    asn1_context_free(ctx);
}

TEST_F(Asn1DecoderTest, SequenceGet_TruncatedLength_Failure) {
    uint8_t truncated[] = { 0x30, 0x82, };
    asn1_context_t* ctx = asn1_context_new(truncated, sizeof(truncated));
    EXPECT_EQ(NULL, asn1_sequence_get(ctx));
    asn1_context_free(ctx);
}

TEST_F(Asn1DecoderTest, SequenceGet_TooSmallForChild_Failure) {
    uint8_t data[] = { 0x30, 0x02, 0x06, 0x01, 0x01, };
    asn1_context_t* ctx = asn1_context_new(data, sizeof(data));
    asn1_context_t* ptr = asn1_sequence_get(ctx);
    ASSERT_NE((asn1_context_t*)NULL, ptr);
    uint8_t* oid;
    size_t length;
    EXPECT_FALSE(asn1_oid_get(ptr, &oid, &length));
    asn1_context_free(ptr);
    asn1_context_free(ctx);
}

TEST_F(Asn1DecoderTest, SequenceGet_Success) {
    uint8_t data[] = { 0x30, 0x03, 0x06, 0x01, 0x01, };
    asn1_context_t* ctx = asn1_context_new(data, sizeof(data));
    asn1_context_t* ptr = asn1_sequence_get(ctx);
    ASSERT_NE((asn1_context_t*)NULL, ptr);
    uint8_t* oid;
    size_t length;
    ASSERT_TRUE(asn1_oid_get(ptr, &oid, &length));
    EXPECT_EQ(1U, length);
    EXPECT_EQ(0x01U, *oid);
    asn1_context_free(ptr);
    asn1_context_free(ctx);
}

TEST_F(Asn1DecoderTest, SetGet_TruncatedLength_Failure) {
    uint8_t truncated[] = { 0x31, 0x82, };
    asn1_context_t* ctx = asn1_context_new(truncated, sizeof(truncated));
    EXPECT_EQ(NULL, asn1_set_get(ctx));
    asn1_context_free(ctx);
}

TEST_F(Asn1DecoderTest, SetGet_TooSmallForChild_Failure) {
    uint8_t data[] = { 0x31, 0x02, 0x06, 0x01, 0x01, };
    asn1_context_t* ctx = asn1_context_new(data, sizeof(data));
    asn1_context_t* ptr = asn1_set_get(ctx);
    ASSERT_NE((asn1_context_t*)NULL, ptr);
    uint8_t* oid;
    size_t length;
    EXPECT_FALSE(asn1_oid_get(ptr, &oid, &length));
    asn1_context_free(ptr);
    asn1_context_free(ctx);
}

TEST_F(Asn1DecoderTest, SetGet_Success) {
    uint8_t data[] = { 0x31, 0x03, 0x06, 0x01, 0xBA, };
    asn1_context_t* ctx = asn1_context_new(data, sizeof(data));
    asn1_context_t* ptr = asn1_set_get(ctx);
    ASSERT_NE((asn1_context_t*)NULL, ptr);
    uint8_t* oid;
    size_t length;
    ASSERT_TRUE(asn1_oid_get(ptr, &oid, &length));
    EXPECT_EQ(1U, length);
    EXPECT_EQ(0xBAU, *oid);
    asn1_context_free(ptr);
    asn1_context_free(ctx);
}

TEST_F(Asn1DecoderTest, OidGet_LengthZero_Failure) {
    uint8_t data[] = { 0x06, 0x00, 0x01, };
    asn1_context_t* ctx = asn1_context_new(data, sizeof(data));
    uint8_t* oid;
    size_t length;
    EXPECT_FALSE(asn1_oid_get(ctx, &oid, &length));
    asn1_context_free(ctx);
}

TEST_F(Asn1DecoderTest, OidGet_TooSmall_Failure) {
    uint8_t data[] = { 0x06, 0x01, };
    asn1_context_t* ctx = asn1_context_new(data, sizeof(data));
    uint8_t* oid;
    size_t length;
    EXPECT_FALSE(asn1_oid_get(ctx, &oid, &length));
    asn1_context_free(ctx);
}

TEST_F(Asn1DecoderTest, OidGet_Success) {
    uint8_t data[] = { 0x06, 0x01, 0x99, };
    asn1_context_t* ctx = asn1_context_new(data, sizeof(data));
    uint8_t* oid;
    size_t length;
    ASSERT_TRUE(asn1_oid_get(ctx, &oid, &length));
    EXPECT_EQ(1U, length);
    EXPECT_EQ(0x99U, *oid);
    asn1_context_free(ctx);
}

TEST_F(Asn1DecoderTest, OctetStringGet_LengthZero_Failure) {
    uint8_t data[] = { 0x04, 0x00, 0x55, };
    asn1_context_t* ctx = asn1_context_new(data, sizeof(data));
    uint8_t* string;
    size_t length;
    ASSERT_FALSE(asn1_octet_string_get(ctx, &string, &length));
    asn1_context_free(ctx);
}

TEST_F(Asn1DecoderTest, OctetStringGet_TooSmall_Failure) {
    uint8_t data[] = { 0x04, 0x01, };
    asn1_context_t* ctx = asn1_context_new(data, sizeof(data));
    uint8_t* string;
    size_t length;
    ASSERT_FALSE(asn1_octet_string_get(ctx, &string, &length));
    asn1_context_free(ctx);
}

TEST_F(Asn1DecoderTest, OctetStringGet_Success) {
    uint8_t data[] = { 0x04, 0x01, 0xAA, };
    asn1_context_t* ctx = asn1_context_new(data, sizeof(data));
    uint8_t* string;
    size_t length;
    ASSERT_TRUE(asn1_octet_string_get(ctx, &string, &length));
    EXPECT_EQ(1U, length);
    EXPECT_EQ(0xAAU, *string);
    asn1_context_free(ctx);
}

} // namespace android
