/*
 * Copyright (C) 2015 The Android Open Source Project
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

/*
 * update_verifier verifies the integrity of the partitions after an A/B OTA update. It gets invoked
 * by init, and will only perform the verification if it's the first boot post an A/B OTA update
 * (https://source.android.com/devices/tech/ota/ab/#after_reboot).
 *
 * update_verifier relies on device-mapper-verity (dm-verity) to capture any corruption on the
 * partitions being verified (https://source.android.com/security/verifiedboot). The verification
 * will be skipped, if dm-verity is not enabled on the device.
 *
 * Upon detecting verification failures, the device will be rebooted, although the trigger of the
 * reboot depends on the dm-verity mode.
 *   enforcing mode: dm-verity reboots the device
 *   eio mode: dm-verity fails the read and update_verifier reboots the device
 *   other mode: not supported and update_verifier reboots the device
 *
 * All these reboots prevent the device from booting into a known corrupt state. If the device
 * continuously fails to boot into the new slot, the bootloader should mark the slot as unbootable
 * and trigger a fallback to the old slot.
 *
 * The current slot will be marked as having booted successfully if the verifier reaches the end
 * after the verification.
 */

#include "update_verifier/update_verifier.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <future>
#include <thread>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/properties.h>
#include <android-base/strings.h>
#include <android-base/unique_fd.h>
#include <android/hardware/boot/1.0/IBootControl.h>
#include <android/os/IVold.h>
#include <binder/BinderService.h>
#include <binder/Status.h>
#include <cutils/android_reboot.h>

#include "care_map.pb.h"

using android::sp;
using android::hardware::boot::V1_0::IBootControl;
using android::hardware::boot::V1_0::BoolResult;
using android::hardware::boot::V1_0::CommandResult;

// TODO(xunchang) remove the prefix and use a default path instead.
constexpr const char* kDefaultCareMapPrefix = "/data/ota_package/care_map";

// Find directories in format of "/sys/block/dm-X".
static int dm_name_filter(const dirent* de) {
  if (android::base::StartsWith(de->d_name, "dm-")) {
    return 1;
  }
  return 0;
}

UpdateVerifier::UpdateVerifier()
    : care_map_prefix_(kDefaultCareMapPrefix),
      property_reader_([](const std::string& id) { return android::base::GetProperty(id, ""); }) {}

// Iterate the content of "/sys/block/dm-X/dm/name" and find all the dm-wrapped block devices.
// We will later read all the ("cared") blocks from "/dev/block/dm-X" to ensure the target
// partition's integrity.
std::map<std::string, std::string> UpdateVerifier::FindDmPartitions() {
  static constexpr auto DM_PATH_PREFIX = "/sys/block/";
  dirent** namelist;
  int n = scandir(DM_PATH_PREFIX, &namelist, dm_name_filter, alphasort);
  if (n == -1) {
    PLOG(ERROR) << "Failed to scan dir " << DM_PATH_PREFIX;
    return {};
  }
  if (n == 0) {
    LOG(ERROR) << "No dm block device found.";
    return {};
  }

  static constexpr auto DM_PATH_SUFFIX = "/dm/name";
  static constexpr auto DEV_PATH = "/dev/block/";
  std::map<std::string, std::string> dm_block_devices;
  while (n--) {
    std::string path = DM_PATH_PREFIX + std::string(namelist[n]->d_name) + DM_PATH_SUFFIX;
    std::string content;
    if (!android::base::ReadFileToString(path, &content)) {
      PLOG(WARNING) << "Failed to read " << path;
    } else {
      std::string dm_block_name = android::base::Trim(content);
      // AVB is using 'vroot' for the root block device but we're expecting 'system'.
      if (dm_block_name == "vroot") {
        dm_block_name = "system";
      } else if (android::base::EndsWith(dm_block_name, "-verity")) {
        auto npos = dm_block_name.rfind("-verity");
        dm_block_name = dm_block_name.substr(0, npos);
      } else if (!android::base::GetProperty("ro.boot.avb_version", "").empty()) {
        // Verified Boot 1.0 doesn't add a -verity suffix. On AVB 2 devices,
        // if DAP is enabled, then a -verity suffix must be used to
        // differentiate between dm-linear and dm-verity devices. If we get
        // here, we're AVB 2 and looking at a non-verity partition.
        continue;
      }

      dm_block_devices.emplace(dm_block_name, DEV_PATH + std::string(namelist[n]->d_name));
    }
    free(namelist[n]);
  }
  free(namelist);

  return dm_block_devices;
}

bool UpdateVerifier::ReadBlocks(const std::string partition_name,
                                const std::string& dm_block_device, const RangeSet& ranges) {
  // RangeSet::Split() splits the ranges into multiple groups with same number of blocks (except for
  // the last group).
  size_t thread_num = std::thread::hardware_concurrency() ?: 4;
  std::vector<RangeSet> groups = ranges.Split(thread_num);

  std::vector<std::future<bool>> threads;
  for (const auto& group : groups) {
    auto thread_func = [&group, &dm_block_device, &partition_name]() {
      android::base::unique_fd fd(TEMP_FAILURE_RETRY(open(dm_block_device.c_str(), O_RDONLY)));
      if (fd.get() == -1) {
        PLOG(ERROR) << "Error reading " << dm_block_device << " for partition " << partition_name;
        return false;
      }

      static constexpr size_t kBlockSize = 4096;
      std::vector<uint8_t> buf(1024 * kBlockSize);

      size_t block_count = 0;
      for (const auto& [range_start, range_end] : group) {
        if (lseek64(fd.get(), static_cast<off64_t>(range_start) * kBlockSize, SEEK_SET) == -1) {
          PLOG(ERROR) << "lseek to " << range_start << " failed";
          return false;
        }

        size_t remain = (range_end - range_start) * kBlockSize;
        while (remain > 0) {
          size_t to_read = std::min(remain, 1024 * kBlockSize);
          if (!android::base::ReadFully(fd.get(), buf.data(), to_read)) {
            PLOG(ERROR) << "Failed to read blocks " << range_start << " to " << range_end;
            return false;
          }
          remain -= to_read;
        }
        block_count += (range_end - range_start);
      }
      LOG(INFO) << "Finished reading " << block_count << " blocks on " << dm_block_device;
      return true;
    };

    threads.emplace_back(std::async(std::launch::async, thread_func));
  }

  bool ret = true;
  for (auto& t : threads) {
    ret = t.get() && ret;
  }
  LOG(INFO) << "Finished reading blocks on " << dm_block_device << " with " << thread_num
            << " threads.";
  return ret;
}

bool UpdateVerifier::VerifyPartitions() {
  auto dm_block_devices = FindDmPartitions();
  if (dm_block_devices.empty()) {
    LOG(ERROR) << "No dm-enabled block device is found.";
    return false;
  }

  for (const auto& [partition_name, ranges] : partition_map_) {
    if (dm_block_devices.find(partition_name) == dm_block_devices.end()) {
      LOG(ERROR) << "Failed to find dm block device for " << partition_name;
      return false;
    }

    if (!ReadBlocks(partition_name, dm_block_devices.at(partition_name), ranges)) {
      return false;
    }
  }

  return true;
}

bool UpdateVerifier::ParseCareMap() {
  partition_map_.clear();

  std::string care_map_name = care_map_prefix_ + ".pb";
  if (access(care_map_name.c_str(), R_OK) == -1) {
    LOG(ERROR) << care_map_name << " doesn't exist";
    return false;
  }

  android::base::unique_fd care_map_fd(TEMP_FAILURE_RETRY(open(care_map_name.c_str(), O_RDONLY)));
  // If the device is flashed before the current boot, it may not have care_map.txt in
  // /data/ota_package. To allow the device to continue booting in this situation, we should
  // print a warning and skip the block verification.
  if (care_map_fd.get() == -1) {
    PLOG(WARNING) << "Failed to open " << care_map_name;
    return false;
  }

  std::string file_content;
  if (!android::base::ReadFdToString(care_map_fd.get(), &file_content)) {
    PLOG(WARNING) << "Failed to read " << care_map_name;
    return false;
  }

  if (file_content.empty()) {
    LOG(WARNING) << "Unexpected empty care map";
    return false;
  }

  recovery_update_verifier::CareMap care_map;
  if (!care_map.ParseFromString(file_content)) {
    LOG(WARNING) << "Failed to parse " << care_map_name << " in protobuf format.";
    return false;
  }

  for (const auto& partition : care_map.partitions()) {
    if (partition.name().empty()) {
      LOG(WARNING) << "Unexpected empty partition name.";
      return false;
    }
    if (partition.ranges().empty()) {
      LOG(WARNING) << "Unexpected block ranges for partition " << partition.name();
      return false;
    }
    RangeSet ranges = RangeSet::Parse(partition.ranges());
    if (!ranges) {
      LOG(WARNING) << "Error parsing RangeSet string " << partition.ranges();
      return false;
    }

    // Continues to check other partitions if there is a fingerprint mismatch.
    if (partition.id().empty() || partition.id() == "unknown") {
      LOG(WARNING) << "Skip reading partition " << partition.name()
                   << ": property_id is not provided to get fingerprint.";
      continue;
    }

    std::string fingerprint = property_reader_(partition.id());
    if (fingerprint != partition.fingerprint()) {
      LOG(WARNING) << "Skip reading partition " << partition.name() << ": fingerprint "
                   << fingerprint << " doesn't match the expected value "
                   << partition.fingerprint();
      continue;
    }

    partition_map_.emplace(partition.name(), ranges);
  }

  if (partition_map_.empty()) {
    LOG(WARNING) << "No partition to verify";
    return false;
  }

  return true;
}

void UpdateVerifier::set_care_map_prefix(const std::string& prefix) {
  care_map_prefix_ = prefix;
}

void UpdateVerifier::set_property_reader(
    const std::function<std::string(const std::string&)>& property_reader) {
  property_reader_ = property_reader;
}

static int reboot_device() {
  if (android_reboot(ANDROID_RB_RESTART2, 0, nullptr) == -1) {
    LOG(ERROR) << "Failed to reboot.";
    return -1;
  }
  while (true) pause();
}

int update_verifier(int argc, char** argv) {
  for (int i = 1; i < argc; i++) {
    LOG(INFO) << "Started with arg " << i << ": " << argv[i];
  }

  sp<IBootControl> module = IBootControl::getService();
  if (module == nullptr) {
    LOG(ERROR) << "Error getting bootctrl module.";
    return reboot_device();
  }

  uint32_t current_slot = module->getCurrentSlot();
  BoolResult is_successful = module->isSlotMarkedSuccessful(current_slot);
  LOG(INFO) << "Booting slot " << current_slot << ": isSlotMarkedSuccessful="
            << static_cast<int32_t>(is_successful);

  if (is_successful == BoolResult::FALSE) {
    // The current slot has not booted successfully.

    bool skip_verification = false;
    std::string verity_mode = android::base::GetProperty("ro.boot.veritymode", "");
    if (verity_mode.empty()) {
      // Skip the verification if ro.boot.veritymode property is not set. This could be a result
      // that device doesn't support dm-verity, or has disabled that.
      LOG(WARNING) << "dm-verity not enabled; marking without verification.";
      skip_verification = true;
    } else if (android::base::EqualsIgnoreCase(verity_mode, "eio")) {
      // We shouldn't see verity in EIO mode if the current slot hasn't booted successfully before.
      // Continue the verification until we fail to read some blocks.
      LOG(WARNING) << "Found dm-verity in EIO mode.";
    } else if (android::base::EqualsIgnoreCase(verity_mode, "disabled")) {
      LOG(WARNING) << "dm-verity in disabled mode; marking without verification.";
      skip_verification = true;
    } else if (verity_mode != "enforcing") {
      LOG(ERROR) << "Unexpected dm-verity mode: " << verity_mode << ", expecting enforcing.";
      return reboot_device();
    }

    if (!skip_verification) {
      UpdateVerifier verifier;
      if (!verifier.ParseCareMap()) {
        LOG(WARNING) << "Failed to parse the care map file, skipping verification";
      } else if (!verifier.VerifyPartitions()) {
        LOG(ERROR) << "Failed to verify all blocks in care map file.";
        return reboot_device();
      }
    }

    bool supports_checkpoint = false;
    auto sm = android::defaultServiceManager();
    android::sp<android::IBinder> binder = sm->getService(android::String16("vold"));
    if (binder) {
      auto vold = android::interface_cast<android::os::IVold>(binder);
      android::binder::Status status = vold->supportsCheckpoint(&supports_checkpoint);
      if (!status.isOk()) {
        LOG(ERROR) << "Failed to check if checkpoints supported. Continuing";
      }
    } else {
      LOG(ERROR) << "Failed to obtain vold Binder. Continuing";
    }

    if (!supports_checkpoint) {
      CommandResult cr;
      module->markBootSuccessful([&cr](CommandResult result) { cr = result; });
      if (!cr.success) {
        LOG(ERROR) << "Error marking booted successfully: " << cr.errMsg;
        return reboot_device();
      }
      LOG(INFO) << "Marked slot " << current_slot << " as booted successfully.";
      // Clears the warm reset flag for next reboot.
      if (!android::base::SetProperty("ota.warm_reset", "0")) {
        LOG(WARNING) << "Failed to reset the warm reset flag";
      }
    } else {
      LOG(INFO) << "Deferred marking slot " << current_slot << " as booted successfully.";
    }
  }

  LOG(INFO) << "Leaving update_verifier.";
  return 0;
}
