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

#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "otautil/rangeset.h"

// The update verifier performs verification upon the first boot to a new slot on A/B devices.
// During the verification, it reads all the blocks in the care_map. And if a failure happens,
// it rejects the current boot and triggers a fallback.

// Note that update_verifier should be backward compatible to not reject care_map.txt from old
// releases, which could otherwise fail to boot into the new release. For example, we've changed
// the care_map format between N and O. An O update_verifier would fail to work with N care_map.txt.
// This could be a result of sideloading an O OTA while the device having a pending N update.
int update_verifier(int argc, char** argv);

// The UpdateVerifier parses the content in the care map, and continues to verify the
// partitions by reading the cared blocks if there's no format error in the file. Otherwise,
// it should skip the verification to avoid bricking the device.
class UpdateVerifier {
 public:
  UpdateVerifier();

  // This function tries to process the care_map.pb as protobuf message; and falls back to use
  // care_map.txt if the pb format file doesn't exist. If the parsing succeeds, put the result
  // of the pair <partition_name, ranges> into the |partition_map_|.
  bool ParseCareMap();

  // Verifies the new boot by reading all the cared blocks for partitions in |partition_map_|.
  bool VerifyPartitions();

 private:
  friend class UpdateVerifierTest;
  // Finds all the dm-enabled partitions, and returns a map of <partition_name, block_device>.
  std::map<std::string, std::string> FindDmPartitions();

  // Returns true if we successfully read the blocks in |ranges| of the |dm_block_device|.
  bool ReadBlocks(const std::string partition_name, const std::string& dm_block_device,
                  const RangeSet& ranges);

  // Functions to override the care_map_prefix_ and property_reader_, used in test only.
  void set_care_map_prefix(const std::string& prefix);
  void set_property_reader(const std::function<std::string(const std::string&)>& property_reader);

  std::map<std::string, RangeSet> partition_map_;
  // The path to the care_map excluding the filename extension; default value:
  // "/data/ota_package/care_map"
  std::string care_map_prefix_;

  // The function to read the device property; default value: android::base::GetProperty()
  std::function<std::string(const std::string&)> property_reader_;
};
