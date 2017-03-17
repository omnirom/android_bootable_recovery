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
