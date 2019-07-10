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

#include "updater/target_files.h"

#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <memory>

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <sparse/sparse.h>

static bool SimgToImg(int input_fd, int output_fd) {
  if (lseek64(input_fd, 0, SEEK_SET) == -1) {
    PLOG(ERROR) << "Failed to lseek64 on the input sparse image";
    return false;
  }

  if (lseek64(output_fd, 0, SEEK_SET) == -1) {
    PLOG(ERROR) << "Failed to lseek64 on the output raw image";
    return false;
  }

  std::unique_ptr<sparse_file, decltype(&sparse_file_destroy)> s_file(
      sparse_file_import(input_fd, true, false), sparse_file_destroy);
  if (!s_file) {
    LOG(ERROR) << "Failed to import the sparse image.";
    return false;
  }

  if (sparse_file_write(s_file.get(), output_fd, false, false, false) < 0) {
    PLOG(ERROR) << "Failed to output the raw image file.";
    return false;
  }

  return true;
}

static bool ParsePropertyFile(const std::string_view prop_content,
                              std::map<std::string, std::string, std::less<>>* props_map) {
  LOG(INFO) << "Start parsing build property\n";
  std::vector<std::string> lines = android::base::Split(std::string(prop_content), "\n");
  for (const auto& line : lines) {
    if (line.empty() || line[0] == '#') continue;
    auto pos = line.find('=');
    if (pos == std::string::npos) continue;
    std::string key = line.substr(0, pos);
    std::string value = line.substr(pos + 1);
    LOG(INFO) << key << ": " << value;
    props_map->emplace(key, value);
  }

  return true;
}

static bool ParseFstab(const std::string_view fstab, std::vector<FstabInfo>* fstab_info_list) {
  LOG(INFO) << "parsing fstab\n";
  std::vector<std::string> lines = android::base::Split(std::string(fstab), "\n");
  for (const auto& line : lines) {
    if (line.empty() || line[0] == '#') continue;

    // <block_device>  <mount_point>  <fs_type>  <mount_flags>  optional:<fs_mgr_flags>
    std::vector<std::string> tokens = android::base::Split(line, " ");
    tokens.erase(std::remove(tokens.begin(), tokens.end(), ""), tokens.end());
    if (tokens.size() != 4 && tokens.size() != 5) {
      LOG(ERROR) << "Unexpected token size: " << tokens.size() << std::endl
                 << "Error parsing fstab line: " << line;
      return false;
    }

    const auto& blockdev = tokens[0];
    const auto& mount_point = tokens[1];
    const auto& fs_type = tokens[2];
    if (!android::base::StartsWith(mount_point, "/")) {
      LOG(WARNING) << "mount point '" << mount_point << "' does not start with '/'";
      continue;
    }

    // The simulator only supports ext4 and emmc for now.
    if (fs_type != "ext4" && fs_type != "emmc") {
      LOG(WARNING) << "Unsupported fs_type in " << line;
      continue;
    }

    fstab_info_list->emplace_back(blockdev, mount_point, fs_type);
  }

  return true;
}

bool TargetFile::EntryExists(const std::string_view name) const {
  if (extracted_input_) {
    std::string entry_path = path_ + "/" + std::string(name);
    if (access(entry_path.c_str(), O_RDONLY) != 0) {
      PLOG(WARNING) << "Failed to access " << entry_path;
      return false;
    }
    return true;
  }

  CHECK(handle_);
  ZipEntry img_entry;
  return FindEntry(handle_, name, &img_entry) == 0;
}

bool TargetFile::ReadEntryToString(const std::string_view name, std::string* content) const {
  if (extracted_input_) {
    std::string entry_path = path_ + "/" + std::string(name);
    return android::base::ReadFileToString(entry_path, content);
  }

  CHECK(handle_);
  ZipEntry entry;
  if (auto find_err = FindEntry(handle_, name, &entry); find_err != 0) {
    LOG(ERROR) << "failed to find " << name << " in the package: " << ErrorCodeString(find_err);
    return false;
  }

  if (entry.uncompressed_length == 0) {
    content->clear();
    return true;
  }

  content->resize(entry.uncompressed_length);
  if (auto extract_err = ExtractToMemory(
          handle_, &entry, reinterpret_cast<uint8_t*>(&content->at(0)), entry.uncompressed_length);
      extract_err != 0) {
    LOG(ERROR) << "failed to read " << name << " from package: " << ErrorCodeString(extract_err);
    return false;
  }

  return true;
}

bool TargetFile::ExtractEntryToTempFile(const std::string_view name,
                                        TemporaryFile* temp_file) const {
  if (extracted_input_) {
    std::string entry_path = path_ + "/" + std::string(name);
    return std::filesystem::copy_file(entry_path, temp_file->path,
                                      std::filesystem::copy_options::overwrite_existing);
  }

  CHECK(handle_);
  ZipEntry entry;
  if (auto find_err = FindEntry(handle_, name, &entry); find_err != 0) {
    LOG(ERROR) << "failed to find " << name << " in the package: " << ErrorCodeString(find_err);
    return false;
  }

  if (auto status = ExtractEntryToFile(handle_, &entry, temp_file->fd); status != 0) {
    LOG(ERROR) << "Failed to extract zip entry " << name << " : " << ErrorCodeString(status);
    return false;
  }
  return true;
}

bool TargetFile::Open() {
  if (!extracted_input_) {
    if (auto ret = OpenArchive(path_.c_str(), &handle_); ret != 0) {
      LOG(ERROR) << "failed to open source target file " << path_ << ": " << ErrorCodeString(ret);
      return false;
    }
  }

  // Parse the misc info.
  std::string misc_info_content;
  if (!ReadEntryToString("META/misc_info.txt", &misc_info_content)) {
    return false;
  }
  if (!ParsePropertyFile(misc_info_content, &misc_info_)) {
    return false;
  }

  return true;
}

bool TargetFile::GetBuildProps(std::map<std::string, std::string, std::less<>>* props_map) const {
  props_map->clear();
  // Parse the source zip to mock the system props and block devices. We try all the possible
  // locations for build props.
  constexpr std::string_view kPropLocations[] = {
    "SYSTEM/build.prop",
    "VENDOR/build.prop",
    "PRODUCT/build.prop",
    "SYSTEM_EXT/build.prop",
    "SYSTEM/vendor/build.prop",
    "SYSTEM/product/build.prop",
    "SYSTEM/system_ext/build.prop",
    "ODM/build.prop",  // legacy
    "ODM/etc/build.prop",
    "VENDOR/odm/build.prop",  // legacy
    "VENDOR/odm/etc/build.prop",
  };
  for (const auto& name : kPropLocations) {
    std::string build_prop_content;
    if (!ReadEntryToString(name, &build_prop_content)) {
      continue;
    }
    std::map<std::string, std::string, std::less<>> props;
    if (!ParsePropertyFile(build_prop_content, &props)) {
      LOG(ERROR) << "Failed to parse build prop in " << name;
      return false;
    }
    for (const auto& [key, value] : props) {
      if (auto it = props_map->find(key); it != props_map->end() && it->second != value) {
        LOG(WARNING) << "Property " << key << " has different values in property files, we got "
                     << it->second << " and " << value;
      }
      props_map->emplace(key, value);
    }
  }

  return true;
}

bool TargetFile::ExtractImage(const std::string_view entry_name, const FstabInfo& fstab_info,
                              const std::string_view work_dir, TemporaryFile* image_file) const {
  if (!EntryExists(entry_name)) {
    return false;
  }

  // We don't need extra work for 'emmc'; use the image file as the block device.
  if (fstab_info.fs_type == "emmc" || misc_info_.find("extfs_sparse_flag") == misc_info_.end()) {
    if (!ExtractEntryToTempFile(entry_name, image_file)) {
      return false;
    }
  } else {  // treated as ext4 sparse image
    TemporaryFile sparse_image{ std::string(work_dir) };
    if (!ExtractEntryToTempFile(entry_name, &sparse_image)) {
      return false;
    }

    // Convert the sparse image to raw.
    if (!SimgToImg(sparse_image.fd, image_file->fd)) {
      LOG(ERROR) << "Failed to convert " << fstab_info.mount_point << " to raw.";
      return false;
    }
  }

  return true;
}

bool TargetFile::ParseFstabInfo(std::vector<FstabInfo>* fstab_info_list) const {
  // Parse the fstab file and extract the image files. The location of the fstab actually depends
  // on some flags e.g. "no_recovery", "recovery_as_boot". Here we just try all possibilities.
  constexpr std::string_view kRecoveryFstabLocations[] = {
    "RECOVERY/RAMDISK/system/etc/recovery.fstab",
    "RECOVERY/RAMDISK/etc/recovery.fstab",
    "BOOT/RAMDISK/system/etc/recovery.fstab",
    "BOOT/RAMDISK/etc/recovery.fstab",
  };
  std::string fstab_content;
  for (const auto& name : kRecoveryFstabLocations) {
    if (std::string content; ReadEntryToString(name, &content)) {
      fstab_content = std::move(content);
      break;
    }
  }
  if (fstab_content.empty()) {
    LOG(ERROR) << "Failed to parse the recovery fstab file";
    return false;
  }

  // Extract the images and convert them to raw.
  if (!ParseFstab(fstab_content, fstab_info_list)) {
    LOG(ERROR) << "Failed to mount the block devices for source build.";
    return false;
  }

  return true;
}
