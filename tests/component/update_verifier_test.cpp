/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include <string>

#include <android-base/file.h>
#include <android-base/test_utils.h>
#include <gtest/gtest.h>
#include <update_verifier/update_verifier.h>

class UpdateVerifierTest : public ::testing::Test {
 protected:
  void SetUp() override {
#if defined(PRODUCT_SUPPORTS_VERITY) || defined(BOARD_AVB_ENABLE)
    verity_supported = true;
#else
    verity_supported = false;
#endif
  }

  bool verity_supported;
};

TEST_F(UpdateVerifierTest, verify_image_no_care_map) {
  // Non-existing care_map is allowed.
  ASSERT_TRUE(verify_image("/doesntexist"));
}

TEST_F(UpdateVerifierTest, verify_image_smoke) {
  // This test relies on dm-verity support.
  if (!verity_supported) {
    GTEST_LOG_(INFO) << "Test skipped on devices without dm-verity support.";
    return;
  }

  // The care map file can have only two or four lines.
  TemporaryFile temp_file;
  std::string content = "system\n2,0,1";
  ASSERT_TRUE(android::base::WriteStringToFile(content, temp_file.path));
  ASSERT_TRUE(verify_image(temp_file.path));

  // Leading and trailing newlines should be accepted.
  ASSERT_TRUE(android::base::WriteStringToFile("\n" + content + "\n\n", temp_file.path));
  ASSERT_TRUE(verify_image(temp_file.path));
}

TEST_F(UpdateVerifierTest, verify_image_wrong_lines) {
  // The care map file can have only two or four lines.
  TemporaryFile temp_file;
  ASSERT_FALSE(verify_image(temp_file.path));

  ASSERT_TRUE(android::base::WriteStringToFile("line1", temp_file.path));
  ASSERT_FALSE(verify_image(temp_file.path));

  ASSERT_TRUE(android::base::WriteStringToFile("line1\nline2\nline3", temp_file.path));
  ASSERT_FALSE(verify_image(temp_file.path));
}

TEST_F(UpdateVerifierTest, verify_image_malformed_care_map) {
  // This test relies on dm-verity support.
  if (!verity_supported) {
    GTEST_LOG_(INFO) << "Test skipped on devices without dm-verity support.";
    return;
  }

  TemporaryFile temp_file;
  std::string content = "system\n2,1,0";
  ASSERT_TRUE(android::base::WriteStringToFile(content, temp_file.path));
  ASSERT_FALSE(verify_image(temp_file.path));
}

TEST_F(UpdateVerifierTest, verify_image_legacy_care_map) {
  // This test relies on dm-verity support.
  if (!verity_supported) {
    GTEST_LOG_(INFO) << "Test skipped on devices without dm-verity support.";
    return;
  }

  TemporaryFile temp_file;
  std::string content = "/dev/block/bootdevice/by-name/system\n2,1,0";
  ASSERT_TRUE(android::base::WriteStringToFile(content, temp_file.path));
  ASSERT_TRUE(verify_image(temp_file.path));
}
