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

#include <update_verifier/update_verifier.h>

#include <string>
#include <unordered_map>
#include <vector>

#include <android-base/file.h>
#include <android-base/properties.h>
#include <android-base/strings.h>
#include <android-base/test_utils.h>
#include <google/protobuf/repeated_field.h>
#include <gtest/gtest.h>

#include "care_map.pb.h"

class UpdateVerifierTest : public ::testing::Test {
 protected:
  void SetUp() override {
    std::string verity_mode = android::base::GetProperty("ro.boot.veritymode", "");
    verity_supported = android::base::EqualsIgnoreCase(verity_mode, "enforcing");
  }

  // Returns a serialized string of the proto3 message according to the given partition info.
  std::string ConstructProto(
      std::vector<std::unordered_map<std::string, std::string>>& partitions) {
    UpdateVerifier::CareMap result;
    for (const auto& partition : partitions) {
      UpdateVerifier::CareMap::PartitionInfo info;
      if (partition.find("name") != partition.end()) {
        info.set_name(partition.at("name"));
      }
      if (partition.find("ranges") != partition.end()) {
        info.set_ranges(partition.at("ranges"));
      }
      if (partition.find("fingerprint") != partition.end()) {
        info.set_fingerprint(partition.at("fingerprint"));
      }

      *result.add_partitions() = info;
    }

    return result.SerializeAsString();
  }

  bool verity_supported;
  TemporaryFile care_map_file;
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

  std::string content = "system\n2,0,1";
  ASSERT_TRUE(android::base::WriteStringToFile(content, care_map_file.path));
  ASSERT_TRUE(verify_image(care_map_file.path));

  // Leading and trailing newlines should be accepted.
  ASSERT_TRUE(android::base::WriteStringToFile("\n" + content + "\n\n", care_map_file.path));
  ASSERT_TRUE(verify_image(care_map_file.path));
}

TEST_F(UpdateVerifierTest, verify_image_empty_care_map) {
  ASSERT_FALSE(verify_image(care_map_file.path));
}

TEST_F(UpdateVerifierTest, verify_image_wrong_lines) {
  // The care map file can have only 2 / 4 / 6 lines.
  ASSERT_TRUE(android::base::WriteStringToFile("line1", care_map_file.path));
  ASSERT_FALSE(verify_image(care_map_file.path));

  ASSERT_TRUE(android::base::WriteStringToFile("line1\nline2\nline3", care_map_file.path));
  ASSERT_FALSE(verify_image(care_map_file.path));
}

TEST_F(UpdateVerifierTest, verify_image_malformed_care_map) {
  // This test relies on dm-verity support.
  if (!verity_supported) {
    GTEST_LOG_(INFO) << "Test skipped on devices without dm-verity support.";
    return;
  }

  std::string content = "system\n2,1,0";
  ASSERT_TRUE(android::base::WriteStringToFile(content, care_map_file.path));
  ASSERT_FALSE(verify_image(care_map_file.path));
}

TEST_F(UpdateVerifierTest, verify_image_legacy_care_map) {
  // This test relies on dm-verity support.
  if (!verity_supported) {
    GTEST_LOG_(INFO) << "Test skipped on devices without dm-verity support.";
    return;
  }

  std::string content = "/dev/block/bootdevice/by-name/system\n2,1,0";
  ASSERT_TRUE(android::base::WriteStringToFile(content, care_map_file.path));
  ASSERT_TRUE(verify_image(care_map_file.path));
}

TEST_F(UpdateVerifierTest, verify_image_protobuf_care_map_smoke) {
  // This test relies on dm-verity support.
  if (!verity_supported) {
    GTEST_LOG_(INFO) << "Test skipped on devices without dm-verity support.";
    return;
  }

  std::vector<std::unordered_map<std::string, std::string>> partitions = {
    { { "name", "system" }, { "ranges", "2,0,1" } },
  };

  std::string proto = ConstructProto(partitions);
  ASSERT_TRUE(android::base::WriteStringToFile(proto, care_map_file.path));
  ASSERT_TRUE(verify_image(care_map_file.path));
}

TEST_F(UpdateVerifierTest, verify_image_protobuf_care_map_missing_name) {
  // This test relies on dm-verity support.
  if (!verity_supported) {
    GTEST_LOG_(INFO) << "Test skipped on devices without dm-verity support.";
    return;
  }

  std::vector<std::unordered_map<std::string, std::string>> partitions = {
    { { "ranges", "2,0,1" } },
  };

  std::string proto = ConstructProto(partitions);
  ASSERT_TRUE(android::base::WriteStringToFile(proto, care_map_file.path));
  ASSERT_FALSE(verify_image(care_map_file.path));
}

TEST_F(UpdateVerifierTest, verify_image_protobuf_care_map_bad_ranges) {
  // This test relies on dm-verity support.
  if (!verity_supported) {
    GTEST_LOG_(INFO) << "Test skipped on devices without dm-verity support.";
    return;
  }

  std::vector<std::unordered_map<std::string, std::string>> partitions = {
    { { "name", "system" }, { "ranges", "3,0,1" } },
  };

  std::string proto = ConstructProto(partitions);
  ASSERT_TRUE(android::base::WriteStringToFile(proto, care_map_file.path));
  ASSERT_FALSE(verify_image(care_map_file.path));
}
