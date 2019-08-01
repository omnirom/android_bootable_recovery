/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include <list>
#include <map>
#include <string>
#include <string_view>

#include <android-base/file.h>

// This class serves as the aggregation of the fake block device information during update
// simulation on host. In specific, it has the name of the block device, its mount point, and the
// path to the temporary file that fakes this block device.
class FakeBlockDevice {
 public:
  FakeBlockDevice(std::string block_device, std::string mount_point, std::string temp_file_path)
      : blockdev_name(std::move(block_device)),
        mount_point(std::move(mount_point)),
        mounted_file_path(std::move(temp_file_path)) {}

  std::string blockdev_name;
  std::string mount_point;
  std::string mounted_file_path;  // path to the temp file that mocks the block device
};

// This class stores the information of the source build. For example, it creates and maintains
// the temporary files to simulate the block devices on host. Therefore, the simulator runtime can
// query the information and run the update on host.
class BuildInfo {
 public:
  BuildInfo(const std::string_view work_dir, bool keep_images)
      : work_dir_(work_dir), keep_images_(keep_images) {}
  // Returns the value of the build properties.
  std::string GetProperty(const std::string_view key, const std::string_view default_value) const;
  // Returns the path to the mock block device.
  std::string FindBlockDeviceName(const std::string_view name) const;
  // Parses the given target-file, initializes the build properties and extracts the images.
  bool ParseTargetFile(const std::string_view target_file_path, bool extracted_input);

  std::string GetOemSettings() const {
    return oem_settings_;
  }
  void SetOemSettings(const std::string_view oem_settings) {
    oem_settings_ = oem_settings;
  }

 private:
  // A map to store the system properties during simulation.
  std::map<std::string, std::string, std::less<>> build_props_;
  // A file that contains the oem properties.
  std::string oem_settings_;
  // A map from the blockdev_name to the FakeBlockDevice object, which contains the path to the
  // temporary file.
  std::map<std::string, FakeBlockDevice, std::less<>> blockdev_map_;

  std::list<TemporaryFile> temp_files_;
  std::string work_dir_;  // A temporary directory to store the extracted image files
  bool keep_images_;
};
