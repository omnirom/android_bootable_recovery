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

#include <string>

// This class parses a given target file for the build properties and image files. Then it creates
// and maintains the temporary files to simulate the block devices on host.
class TargetFiles {
 public:
  TargetFiles(std::string path, std::string work_dir)
      : path_(std::move(path)), work_dir_(std::move(work_dir)) {}

  std::string GetProperty(const std::string_view key, const std::string_view default_value) const;

  std::string FindBlockDeviceName(const std::string_view name) const;

 private:
  std::string path_;  // Path to the target file.

  std::string work_dir_;  // A temporary directory to store the extracted image files
};
