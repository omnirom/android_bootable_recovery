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

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <android-base/file.h>
#include <ziparchive/zip_archive.h>

// This class represents the mount information for each line in a fstab file.
class FstabInfo {
 public:
  FstabInfo(std::string blockdev_name, std::string mount_point, std::string fs_type)
      : blockdev_name(std::move(blockdev_name)),
        mount_point(std::move(mount_point)),
        fs_type(std::move(fs_type)) {}

  std::string blockdev_name;
  std::string mount_point;
  std::string fs_type;
};

// This class parses a target file from a zip file or an extracted directory. It also provides the
// function to read the its content for simulation.
class TargetFile {
 public:
  TargetFile(std::string path, bool extracted_input)
      : path_(std::move(path)), extracted_input_(extracted_input) {}

  // Opens the input target file (or extracted directory) and parses the misc_info.txt.
  bool Open();
  // Parses the build properties in all possible locations and save them in |props_map|
  bool GetBuildProps(std::map<std::string, std::string, std::less<>>* props_map) const;
  // Parses the fstab and save the information about each partition to mount into |fstab_info_list|.
  bool ParseFstabInfo(std::vector<FstabInfo>* fstab_info_list) const;
  // Returns true if the given entry exists in the target file.
  bool EntryExists(const std::string_view name) const;
  // Extracts the image file |entry_name|. Returns true on success.
  bool ExtractImage(const std::string_view entry_name, const FstabInfo& fstab_info,
                    const std::string_view work_dir, TemporaryFile* image_file) const;

 private:
  // Wrapper functions to read the entry from either the zipped target-file, or the extracted input
  // directory.
  bool ReadEntryToString(const std::string_view name, std::string* content) const;
  bool ExtractEntryToTempFile(const std::string_view name, TemporaryFile* temp_file) const;

  std::string path_;      // Path to the zipped target-file or an extracted directory.
  bool extracted_input_;  // True if the target-file has been extracted.
  ZipArchiveHandle handle_{ nullptr };

  // The properties under META/misc_info.txt
  std::map<std::string, std::string, std::less<>> misc_info_;
};
