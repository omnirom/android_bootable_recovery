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

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <android-base/file.h>
#include <android-base/properties.h>
#include <android-base/strings.h>
#include <google/protobuf/repeated_field.h>
#include <gtest/gtest.h>

#include "care_map.pb.h"

using namespace std::string_literals;

class UpdateVerifierTest : public ::testing::Test {
 protected:
  void SetUp() override {
    std::string verity_mode = android::base::GetProperty("ro.boot.veritymode", "");
    verity_supported = android::base::EqualsIgnoreCase(verity_mode, "enforcing");

    care_map_prefix_ = care_map_dir_.path + "/care_map"s;
    care_map_pb_ = care_map_dir_.path + "/care_map.pb"s;
    care_map_txt_ = care_map_dir_.path + "/care_map.txt"s;
    // Overrides the the care_map_prefix.
    verifier_.set_care_map_prefix(care_map_prefix_);

    property_id_ = "ro.build.fingerprint";
    fingerprint_ = android::base::GetProperty(property_id_, "");
    // Overrides the property_reader if we cannot read the given property on the device.
    if (fingerprint_.empty()) {
      fingerprint_ = "mock_fingerprint";
      verifier_.set_property_reader([](const std::string& /* id */) { return "mock_fingerprint"; });
    }
  }

  void TearDown() override {
    unlink(care_map_pb_.c_str());
    unlink(care_map_txt_.c_str());
  }

  // Returns a serialized string of the proto3 message according to the given partition info.
  std::string ConstructProto(
      std::vector<std::unordered_map<std::string, std::string>>& partitions) {
    recovery_update_verifier::CareMap result;
    for (const auto& partition : partitions) {
      recovery_update_verifier::CareMap::PartitionInfo info;
      if (partition.find("name") != partition.end()) {
        info.set_name(partition.at("name"));
      }
      if (partition.find("ranges") != partition.end()) {
        info.set_ranges(partition.at("ranges"));
      }
      if (partition.find("id") != partition.end()) {
        info.set_id(partition.at("id"));
      }
      if (partition.find("fingerprint") != partition.end()) {
        info.set_fingerprint(partition.at("fingerprint"));
      }

      *result.add_partitions() = info;
    }

    return result.SerializeAsString();
  }

  bool verity_supported;
  UpdateVerifier verifier_;

  TemporaryDir care_map_dir_;
  std::string care_map_prefix_;
  std::string care_map_pb_;
  std::string care_map_txt_;

  std::string property_id_;
  std::string fingerprint_;
};

TEST_F(UpdateVerifierTest, verify_image_no_care_map) {
  ASSERT_FALSE(verifier_.ParseCareMap());
}

TEST_F(UpdateVerifierTest, verify_image_text_format) {
  // This test relies on dm-verity support.
  if (!verity_supported) {
    GTEST_LOG_(INFO) << "Test skipped on devices without dm-verity support.";
    return;
  }

  std::string content = "system\n2,0,1";
  ASSERT_TRUE(android::base::WriteStringToFile(content, care_map_txt_));
  // CareMap in text format is no longer supported.
  ASSERT_FALSE(verifier_.ParseCareMap());
}

TEST_F(UpdateVerifierTest, verify_image_empty_care_map) {
  ASSERT_FALSE(verifier_.ParseCareMap());
}

TEST_F(UpdateVerifierTest, verify_image_protobuf_care_map_smoke) {
  // This test relies on dm-verity support.
  if (!verity_supported) {
    GTEST_LOG_(INFO) << "Test skipped on devices without dm-verity support.";
    return;
  }

  std::vector<std::unordered_map<std::string, std::string>> partitions = {
    {
        { "name", "system" },
        { "ranges", "2,0,1" },
        { "id", property_id_ },
        { "fingerprint", fingerprint_ },
    },
  };

  std::string proto = ConstructProto(partitions);
  ASSERT_TRUE(android::base::WriteStringToFile(proto, care_map_pb_));
  ASSERT_TRUE(verifier_.ParseCareMap());
  ASSERT_TRUE(verifier_.VerifyPartitions());
}

TEST_F(UpdateVerifierTest, verify_image_protobuf_care_map_missing_name) {
  // This test relies on dm-verity support.
  if (!verity_supported) {
    GTEST_LOG_(INFO) << "Test skipped on devices without dm-verity support.";
    return;
  }

  std::vector<std::unordered_map<std::string, std::string>> partitions = {
    {
        { "ranges", "2,0,1" },
        { "id", property_id_ },
        { "fingerprint", fingerprint_ },
    },
  };

  std::string proto = ConstructProto(partitions);
  ASSERT_TRUE(android::base::WriteStringToFile(proto, care_map_pb_));
  ASSERT_FALSE(verifier_.ParseCareMap());
}

TEST_F(UpdateVerifierTest, verify_image_protobuf_care_map_bad_ranges) {
  // This test relies on dm-verity support.
  if (!verity_supported) {
    GTEST_LOG_(INFO) << "Test skipped on devices without dm-verity support.";
    return;
  }

  std::vector<std::unordered_map<std::string, std::string>> partitions = {
    {
        { "name", "system" },
        { "ranges", "3,0,1" },
        { "id", property_id_ },
        { "fingerprint", fingerprint_ },
    },
  };

  std::string proto = ConstructProto(partitions);
  ASSERT_TRUE(android::base::WriteStringToFile(proto, care_map_pb_));
  ASSERT_FALSE(verifier_.ParseCareMap());
}

TEST_F(UpdateVerifierTest, verify_image_protobuf_empty_fingerprint) {
  // This test relies on dm-verity support.
  if (!verity_supported) {
    GTEST_LOG_(INFO) << "Test skipped on devices without dm-verity support.";
    return;
  }

  std::vector<std::unordered_map<std::string, std::string>> partitions = {
    {
        { "name", "system" },
        { "ranges", "2,0,1" },
    },
  };

  std::string proto = ConstructProto(partitions);
  ASSERT_TRUE(android::base::WriteStringToFile(proto, care_map_pb_));
  ASSERT_FALSE(verifier_.ParseCareMap());
}

TEST_F(UpdateVerifierTest, verify_image_protobuf_fingerprint_mismatch) {
  // This test relies on dm-verity support.
  if (!verity_supported) {
    GTEST_LOG_(INFO) << "Test skipped on devices without dm-verity support.";
    return;
  }

  std::vector<std::unordered_map<std::string, std::string>> partitions = {
    {
        { "name", "system" },
        { "ranges", "2,0,1" },
        { "id", property_id_ },
        { "fingerprint", "unsupported_fingerprint" },
    },
  };

  std::string proto = ConstructProto(partitions);
  ASSERT_TRUE(android::base::WriteStringToFile(proto, care_map_pb_));
  ASSERT_FALSE(verifier_.ParseCareMap());
}
