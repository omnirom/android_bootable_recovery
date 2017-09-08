/*
 * Copyright (C) 2009 The Android Open Source Project
 *
 * Licensed under the Apache License, version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agree to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <errno.h>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/stringprintf.h>
#include <android-base/test_utils.h>

#include "common/test_constants.h"
#include "otautil/SysUtil.h"
#include "verifier.h"

using namespace std::string_literals;

class VerifierTest : public testing::TestWithParam<std::vector<std::string>> {
 protected:
  void SetUp() override {
    std::vector<std::string> args = GetParam();
    std::string package = from_testdata_base(args[0]);
    if (sysMapFile(package.c_str(), &memmap) != 0) {
      FAIL() << "Failed to mmap " << package << ": " << strerror(errno) << "\n";
    }

    for (auto it = ++args.cbegin(); it != args.cend(); ++it) {
      std::string public_key_file = from_testdata_base("testkey_" + *it + ".txt");
      ASSERT_TRUE(load_keys(public_key_file.c_str(), certs));
    }
  }

  MemMapping memmap;
  std::vector<Certificate> certs;
};

class VerifierSuccessTest : public VerifierTest {
};

class VerifierFailureTest : public VerifierTest {
};

TEST(VerifierTest, load_keys_multiple_keys) {
  std::string testkey_v4;
  ASSERT_TRUE(android::base::ReadFileToString(from_testdata_base("testkey_v4.txt"), &testkey_v4));

  std::string testkey_v3;
  ASSERT_TRUE(android::base::ReadFileToString(from_testdata_base("testkey_v3.txt"), &testkey_v3));

  std::string keys = testkey_v4 + "," + testkey_v3 + "," + testkey_v4;
  TemporaryFile key_file1;
  ASSERT_TRUE(android::base::WriteStringToFile(keys, key_file1.path));
  std::vector<Certificate> certs;
  ASSERT_TRUE(load_keys(key_file1.path, certs));
  ASSERT_EQ(3U, certs.size());
}

TEST(VerifierTest, load_keys_invalid_keys) {
  std::vector<Certificate> certs;
  ASSERT_FALSE(load_keys("/doesntexist", certs));

  // Empty file.
  TemporaryFile key_file1;
  ASSERT_FALSE(load_keys(key_file1.path, certs));

  // Invalid contents.
  ASSERT_TRUE(android::base::WriteStringToFile("invalid", key_file1.path));
  ASSERT_FALSE(load_keys(key_file1.path, certs));

  std::string testkey_v4;
  ASSERT_TRUE(android::base::ReadFileToString(from_testdata_base("testkey_v4.txt"), &testkey_v4));

  // Invalid key version: "v4 ..." => "v6 ...".
  std::string invalid_key2(testkey_v4);
  invalid_key2[1] = '6';
  TemporaryFile key_file2;
  ASSERT_TRUE(android::base::WriteStringToFile(invalid_key2, key_file2.path));
  ASSERT_FALSE(load_keys(key_file2.path, certs));

  // Invalid key content: inserted extra bytes ",2209831334".
  std::string invalid_key3(testkey_v4);
  invalid_key3.insert(invalid_key2.size() - 2, ",2209831334");
  TemporaryFile key_file3;
  ASSERT_TRUE(android::base::WriteStringToFile(invalid_key3, key_file3.path));
  ASSERT_FALSE(load_keys(key_file3.path, certs));

  // Invalid key: the last key must not end with an extra ','.
  std::string invalid_key4 = testkey_v4 + ",";
  TemporaryFile key_file4;
  ASSERT_TRUE(android::base::WriteStringToFile(invalid_key4, key_file4.path));
  ASSERT_FALSE(load_keys(key_file4.path, certs));

  // Invalid key separator.
  std::string invalid_key5 = testkey_v4 + ";" + testkey_v4;
  TemporaryFile key_file5;
  ASSERT_TRUE(android::base::WriteStringToFile(invalid_key5, key_file5.path));
  ASSERT_FALSE(load_keys(key_file5.path, certs));
}

TEST(VerifierTest, BadPackage_SignatureStartOutOfBounds) {
  std::string testkey_v3;
  ASSERT_TRUE(android::base::ReadFileToString(from_testdata_base("testkey_v3.txt"), &testkey_v3));

  TemporaryFile key_file;
  ASSERT_TRUE(android::base::WriteStringToFile(testkey_v3, key_file.path));
  std::vector<Certificate> certs;
  ASSERT_TRUE(load_keys(key_file.path, certs));

  // Signature start is 65535 (0xffff) while comment size is 0 (Bug: 31914369).
  std::string package = "\x50\x4b\x05\x06"s + std::string(12, '\0') + "\xff\xff\xff\xff\x00\x00"s;
  ASSERT_EQ(VERIFY_FAILURE, verify_file(reinterpret_cast<const unsigned char*>(package.data()),
                                        package.size(), certs));
}

TEST_P(VerifierSuccessTest, VerifySucceed) {
  ASSERT_EQ(verify_file(memmap.addr, memmap.length, certs, nullptr), VERIFY_SUCCESS);
}

TEST_P(VerifierFailureTest, VerifyFailure) {
  ASSERT_EQ(verify_file(memmap.addr, memmap.length, certs, nullptr), VERIFY_FAILURE);
}

INSTANTIATE_TEST_CASE_P(SingleKeySuccess, VerifierSuccessTest,
    ::testing::Values(
      std::vector<std::string>({"otasigned_v1.zip", "v1"}),
      std::vector<std::string>({"otasigned_v2.zip", "v2"}),
      std::vector<std::string>({"otasigned_v3.zip", "v3"}),
      std::vector<std::string>({"otasigned_v4.zip", "v4"}),
      std::vector<std::string>({"otasigned_v5.zip", "v5"})));

INSTANTIATE_TEST_CASE_P(MultiKeySuccess, VerifierSuccessTest,
    ::testing::Values(
      std::vector<std::string>({"otasigned_v1.zip", "v1", "v2"}),
      std::vector<std::string>({"otasigned_v2.zip", "v5", "v2"}),
      std::vector<std::string>({"otasigned_v3.zip", "v5", "v1", "v3"}),
      std::vector<std::string>({"otasigned_v4.zip", "v5", "v1", "v4"}),
      std::vector<std::string>({"otasigned_v5.zip", "v4", "v1", "v5"})));

INSTANTIATE_TEST_CASE_P(WrongKey, VerifierFailureTest,
    ::testing::Values(
      std::vector<std::string>({"otasigned_v1.zip", "v2"}),
      std::vector<std::string>({"otasigned_v2.zip", "v1"}),
      std::vector<std::string>({"otasigned_v3.zip", "v5"}),
      std::vector<std::string>({"otasigned_v4.zip", "v5"}),
      std::vector<std::string>({"otasigned_v5.zip", "v3"})));

INSTANTIATE_TEST_CASE_P(WrongHash, VerifierFailureTest,
    ::testing::Values(
      std::vector<std::string>({"otasigned_v1.zip", "v3"}),
      std::vector<std::string>({"otasigned_v2.zip", "v4"}),
      std::vector<std::string>({"otasigned_v3.zip", "v1"}),
      std::vector<std::string>({"otasigned_v4.zip", "v2"})));

INSTANTIATE_TEST_CASE_P(BadPackage, VerifierFailureTest,
    ::testing::Values(
      std::vector<std::string>({"random.zip", "v1"}),
      std::vector<std::string>({"fake-eocd.zip", "v1"}),
      std::vector<std::string>({"alter-metadata.zip", "v1"}),
      std::vector<std::string>({"alter-footer.zip", "v1"})));
