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

#include "updater/build_info.h"

#include <stdio.h>

#include <set>
#include <vector>

#include <android-base/logging.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>

#include "updater/target_files.h"

bool BuildInfo::ParseTargetFile(const std::string_view target_file_path, bool extracted_input) {
  TargetFile target_file(std::string(target_file_path), extracted_input);
  if (!target_file.Open()) {
    return false;
  }

  if (!target_file.GetBuildProps(&build_props_)) {
    return false;
  }

  std::vector<FstabInfo> fstab_info_list;
  if (!target_file.ParseFstabInfo(&fstab_info_list)) {
    return false;
  }

  for (const auto& fstab_info : fstab_info_list) {
    for (const auto& directory : { "IMAGES", "RADIO" }) {
      std::string entry_name = directory + fstab_info.mount_point + ".img";
      if (!target_file.EntryExists(entry_name)) {
        LOG(WARNING) << "Failed to find the image entry in the target file: " << entry_name;
        continue;
      }

      temp_files_.emplace_back(work_dir_);
      auto& image_file = temp_files_.back();
      if (!target_file.ExtractImage(entry_name, fstab_info, work_dir_, &image_file)) {
        LOG(ERROR) << "Failed to set up source image files.";
        return false;
      }

      std::string mapped_path = image_file.path;
      // Rename the images to more readable ones if we want to keep the image.
      if (keep_images_) {
        mapped_path = work_dir_ + fstab_info.mount_point + ".img";
        image_file.release();
        if (rename(image_file.path, mapped_path.c_str()) != 0) {
          PLOG(ERROR) << "Failed to rename " << image_file.path << " to " << mapped_path;
          return false;
        }
      }

      LOG(INFO) << "Mounted " << fstab_info.mount_point << "\nMapping: " << fstab_info.blockdev_name
                << " to " << mapped_path;

      blockdev_map_.emplace(
          fstab_info.blockdev_name,
          FakeBlockDevice(fstab_info.blockdev_name, fstab_info.mount_point, mapped_path));
      break;
    }
  }

  return true;
}

std::string BuildInfo::GetProperty(const std::string_view key,
                                   const std::string_view default_value) const {
  // The logic to parse the ro.product properties should be in line with the generation script.
  // More details in common.py BuildInfo.GetBuildProp.
  // TODO(xunchang) handle the oem property and the source order defined in
  // ro.product.property_source_order
  const std::set<std::string, std::less<>> ro_product_props = {
    "ro.product.brand", "ro.product.device", "ro.product.manufacturer", "ro.product.model",
    "ro.product.name"
  };
  const std::vector<std::string> source_order = {
    "product", "odm", "vendor", "system_ext", "system",
  };
  if (ro_product_props.find(key) != ro_product_props.end()) {
    std::string_view key_suffix(key);
    CHECK(android::base::ConsumePrefix(&key_suffix, "ro.product"));
    for (const auto& source : source_order) {
      std::string resolved_key = "ro.product." + source + std::string(key_suffix);
      if (auto entry = build_props_.find(resolved_key); entry != build_props_.end()) {
        return entry->second;
      }
    }
    LOG(WARNING) << "Failed to find property: " << key;
    return std::string(default_value);
  } else if (key == "ro.build.fingerprint") {
    // clang-format off
    return android::base::StringPrintf("%s/%s/%s:%s/%s/%s:%s/%s",
        GetProperty("ro.product.brand", "").c_str(),
        GetProperty("ro.product.name", "").c_str(),
        GetProperty("ro.product.device", "").c_str(),
        GetProperty("ro.build.version.release", "").c_str(),
        GetProperty("ro.build.id", "").c_str(),
        GetProperty("ro.build.version.incremental", "").c_str(),
        GetProperty("ro.build.type", "").c_str(),
        GetProperty("ro.build.tags", "").c_str());
    // clang-format on
  }

  auto entry = build_props_.find(key);
  if (entry == build_props_.end()) {
    LOG(WARNING) << "Failed to find property: " << key;
    return std::string(default_value);
  }

  return entry->second;
}

std::string BuildInfo::FindBlockDeviceName(const std::string_view name) const {
  auto entry = blockdev_map_.find(name);
  if (entry == blockdev_map_.end()) {
    LOG(WARNING) << "Failed to find path to block device " << name;
    return "";
  }

  return entry->second.mounted_file_path;
}
