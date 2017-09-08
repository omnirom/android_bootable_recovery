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
 * This program verifies the integrity of the partitions after an A/B OTA
 * update. It gets invoked by init, and will only perform the verification if
 * it's the first boot post an A/B OTA update.
 *
 * Update_verifier relies on dm-verity to capture any corruption on the partitions
 * being verified. And its behavior varies depending on the dm-verity mode.
 * Upon detection of failures:
 *   enforcing mode: dm-verity reboots the device
 *   eio mode: dm-verity fails the read and update_verifier reboots the device
 *   other mode: not supported and update_verifier reboots the device
 *
 * After a predefined number of failing boot attempts, the bootloader should
 * mark the slot as unbootable and stops trying. Other dm-verity modes (
 * for example, veritymode=EIO) are not accepted and simply lead to a
 * verification failure.
 *
 * The current slot will be marked as having booted successfully if the
 * verifier reaches the end after the verification.
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/properties.h>
#include <android-base/strings.h>
#include <android-base/unique_fd.h>
#include <android/hardware/boot/1.0/IBootControl.h>
#include <cutils/android_reboot.h>

using android::sp;
using android::hardware::boot::V1_0::IBootControl;
using android::hardware::boot::V1_0::BoolResult;
using android::hardware::boot::V1_0::CommandResult;

constexpr auto CARE_MAP_FILE = "/data/ota_package/care_map.txt";
constexpr auto DM_PATH_PREFIX = "/sys/block/";
constexpr auto DM_PATH_SUFFIX = "/dm/name";
constexpr auto DEV_PATH = "/dev/block/";
constexpr int BLOCKSIZE = 4096;

// Find directories in format of "/sys/block/dm-X".
static int dm_name_filter(const dirent* de) {
  if (android::base::StartsWith(de->d_name, "dm-")) {
    return 1;
  }
  return 0;
}

static bool read_blocks(const std::string& partition, const std::string& range_str) {
  if (partition != "system" && partition != "vendor") {
    LOG(ERROR) << "partition name must be system or vendor: " << partition;
    return false;
  }
  // Iterate the content of "/sys/block/dm-X/dm/name". If it matches "system"
  // (or "vendor"), then dm-X is a dm-wrapped system/vendor partition.
  // Afterwards, update_verifier will read every block on the care_map_file of
  // "/dev/block/dm-X" to ensure the partition's integrity.
  dirent** namelist;
  int n = scandir(DM_PATH_PREFIX, &namelist, dm_name_filter, alphasort);
  if (n == -1) {
    PLOG(ERROR) << "Failed to scan dir " << DM_PATH_PREFIX;
    return false;
  }
  if (n == 0) {
    LOG(ERROR) << "dm block device not found for " << partition;
    return false;
  }

  std::string dm_block_device;
  while (n--) {
    std::string path = DM_PATH_PREFIX + std::string(namelist[n]->d_name) + DM_PATH_SUFFIX;
    std::string content;
    if (!android::base::ReadFileToString(path, &content)) {
      PLOG(WARNING) << "Failed to read " << path;
    } else if (android::base::Trim(content) == partition) {
      dm_block_device = DEV_PATH + std::string(namelist[n]->d_name);
      while (n--) {
        free(namelist[n]);
      }
      break;
    }
    free(namelist[n]);
  }
  free(namelist);

  if (dm_block_device.empty()) {
    LOG(ERROR) << "Failed to find dm block device for " << partition;
    return false;
  }
  android::base::unique_fd fd(TEMP_FAILURE_RETRY(open(dm_block_device.c_str(), O_RDONLY)));
  if (fd.get() == -1) {
    PLOG(ERROR) << "Error reading " << dm_block_device << " for partition " << partition;
    return false;
  }

  // For block range string, first integer 'count' equals 2 * total number of valid ranges,
  // followed by 'count' number comma separated integers. Every two integers reprensent a
  // block range with the first number included in range but second number not included.
  // For example '4,64536,65343,74149,74150' represents: [64536,65343) and [74149,74150).
  std::vector<std::string> ranges = android::base::Split(range_str, ",");
  size_t range_count;
  bool status = android::base::ParseUint(ranges[0], &range_count);
  if (!status || (range_count == 0) || (range_count % 2 != 0) ||
      (range_count != ranges.size() - 1)) {
    LOG(ERROR) << "Error in parsing range string.";
    return false;
  }

  size_t blk_count = 0;
  for (size_t i = 1; i < ranges.size(); i += 2) {
    unsigned int range_start, range_end;
    bool parse_status = android::base::ParseUint(ranges[i], &range_start);
    parse_status = parse_status && android::base::ParseUint(ranges[i + 1], &range_end);
    if (!parse_status || range_start >= range_end) {
      LOG(ERROR) << "Invalid range pair " << ranges[i] << ", " << ranges[i + 1];
      return false;
    }

    if (lseek64(fd.get(), static_cast<off64_t>(range_start) * BLOCKSIZE, SEEK_SET) == -1) {
      PLOG(ERROR) << "lseek to " << range_start << " failed";
      return false;
    }

    size_t size = (range_end - range_start) * BLOCKSIZE;
    std::vector<uint8_t> buf(size);
    if (!android::base::ReadFully(fd.get(), buf.data(), size)) {
      PLOG(ERROR) << "Failed to read blocks " << range_start << " to " << range_end;
      return false;
    }
    blk_count += (range_end - range_start);
  }

  LOG(INFO) << "Finished reading " << blk_count << " blocks on " << dm_block_device;
  return true;
}

static bool verify_image(const std::string& care_map_name) {
    android::base::unique_fd care_map_fd(TEMP_FAILURE_RETRY(open(care_map_name.c_str(), O_RDONLY)));
    // If the device is flashed before the current boot, it may not have care_map.txt
    // in /data/ota_package. To allow the device to continue booting in this situation,
    // we should print a warning and skip the block verification.
    if (care_map_fd.get() == -1) {
        PLOG(WARNING) << "Failed to open " << care_map_name;
        return true;
    }
    // Care map file has four lines (two lines if vendor partition is not present):
    // First line has the block partition name (system/vendor).
    // Second line holds all ranges of blocks to verify.
    // The next two lines have the same format but for vendor partition.
    std::string file_content;
    if (!android::base::ReadFdToString(care_map_fd.get(), &file_content)) {
        LOG(ERROR) << "Error reading care map contents to string.";
        return false;
    }

    std::vector<std::string> lines;
    lines = android::base::Split(android::base::Trim(file_content), "\n");
    if (lines.size() != 2 && lines.size() != 4) {
        LOG(ERROR) << "Invalid lines in care_map: found " << lines.size()
                   << " lines, expecting 2 or 4 lines.";
        return false;
    }

    for (size_t i = 0; i < lines.size(); i += 2) {
        if (!read_blocks(lines[i], lines[i+1])) {
            return false;
        }
    }

    return true;
}

static int reboot_device() {
  if (android_reboot(ANDROID_RB_RESTART2, 0, nullptr) == -1) {
    LOG(ERROR) << "Failed to reboot.";
    return -1;
  }
  while (true) pause();
}

int main(int argc, char** argv) {
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

#ifdef PRODUCT_SUPPORTS_VERITY
    std::string verity_mode = android::base::GetProperty("ro.boot.veritymode", "");
    if (verity_mode.empty()) {
      LOG(ERROR) << "Failed to get dm-verity mode.";
      return reboot_device();
    } else if (android::base::EqualsIgnoreCase(verity_mode, "eio")) {
      // We shouldn't see verity in EIO mode if the current slot hasn't booted successfully before.
      // Continue the verification until we fail to read some blocks.
      LOG(WARNING) << "Found dm-verity in EIO mode.";
    } else if (verity_mode != "enforcing") {
      LOG(ERROR) << "Unexpected dm-verity mode : " << verity_mode << ", expecting enforcing.";
      return reboot_device();
    }

    if (!verify_image(CARE_MAP_FILE)) {
      LOG(ERROR) << "Failed to verify all blocks in care map file.";
      return reboot_device();
    }
#else
    LOG(WARNING) << "dm-verity not enabled; marking without verification.";
#endif

    CommandResult cr;
    module->markBootSuccessful([&cr](CommandResult result) { cr = result; });
    if (!cr.success) {
      LOG(ERROR) << "Error marking booted successfully: " << cr.errMsg;
      return reboot_device();
    }
    LOG(INFO) << "Marked slot " << current_slot << " as booted successfully.";
  }

  LOG(INFO) << "Leaving update_verifier.";
  return 0;
}
