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
 * It relies on dm-verity to capture any corruption on the partitions being
 * verified. dm-verity must be in enforcing mode, so that it will reboot the
 * device on dm-verity failures. When that happens, the bootloader should
 * mark the slot as unbootable and stops trying. Other dm-verity modes (
 * for example, veritymode=EIO) are not accepted and simply lead to a
 * verification failure.
 *
 * The current slot will be marked as having booted successfully if the
 * verifier reaches the end after the verification.
 *
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>
#include <android-base/unique_fd.h>
#include <cutils/properties.h>
#include <hardware/boot_control.h>
#define LOG_TAG       "update_verifier"
#include <log/log.h>

constexpr auto CARE_MAP_FILE = "/data/ota_package/care_map.txt";
constexpr int BLOCKSIZE = 4096;

static bool read_blocks(const std::string& blk_device_prefix, const std::string& range_str) {
    char slot_suffix[PROPERTY_VALUE_MAX];
    property_get("ro.boot.slot_suffix", slot_suffix, "");
    std::string blk_device = blk_device_prefix + std::string(slot_suffix);
    android::base::unique_fd fd(TEMP_FAILURE_RETRY(open(blk_device.c_str(), O_RDONLY)));
    if (fd.get() == -1) {
        SLOGE("Error reading partition %s: %s\n", blk_device.c_str(), strerror(errno));
        return false;
    }

    // For block range string, first integer 'count' equals 2 * total number of valid ranges,
    // followed by 'count' number comma separated integers. Every two integers reprensent a
    // block range with the first number included in range but second number not included.
    // For example '4,64536,65343,74149,74150' represents: [64536,65343) and [74149,74150).
    std::vector<std::string> ranges = android::base::Split(range_str, ",");
    size_t range_count;
    bool status = android::base::ParseUint(ranges[0].c_str(), &range_count);
    if (!status || (range_count == 0) || (range_count % 2 != 0) ||
            (range_count != ranges.size()-1)) {
        SLOGE("Error in parsing range string.\n");
        return false;
    }

    size_t blk_count = 0;
    for (size_t i = 1; i < ranges.size(); i += 2) {
        unsigned int range_start, range_end;
        bool parse_status = android::base::ParseUint(ranges[i].c_str(), &range_start);
        parse_status = parse_status && android::base::ParseUint(ranges[i+1].c_str(), &range_end);
        if (!parse_status || range_start >= range_end) {
            SLOGE("Invalid range pair %s, %s.\n", ranges[i].c_str(), ranges[i+1].c_str());
            return false;
        }

        if (lseek64(fd.get(), static_cast<off64_t>(range_start) * BLOCKSIZE, SEEK_SET) == -1) {
            SLOGE("lseek to %u failed: %s.\n", range_start, strerror(errno));
            return false;
        }

        size_t size = (range_end - range_start) * BLOCKSIZE;
        std::vector<uint8_t> buf(size);
        if (!android::base::ReadFully(fd.get(), buf.data(), size)) {
            SLOGE("Failed to read blocks %u to %u: %s.\n", range_start, range_end,
                  strerror(errno));
            return false;
        }
        blk_count += (range_end - range_start);
    }

    SLOGI("Finished reading %zu blocks on %s.\n", blk_count, blk_device.c_str());
    return true;
}

static bool verify_image(const std::string& care_map_name) {
    android::base::unique_fd care_map_fd(TEMP_FAILURE_RETRY(open(care_map_name.c_str(), O_RDONLY)));
    // If the device is flashed before the current boot, it may not have care_map.txt
    // in /data/ota_package. To allow the device to continue booting in this situation,
    // we should print a warning and skip the block verification.
    if (care_map_fd.get() == -1) {
        SLOGI("Warning: care map %s not found.\n", care_map_name.c_str());
        return true;
    }
    // Care map file has four lines (two lines if vendor partition is not present):
    // First line has the block device name, e.g./dev/block/.../by-name/system.
    // Second line holds all ranges of blocks to verify.
    // The next two lines have the same format but for vendor partition.
    std::string file_content;
    if (!android::base::ReadFdToString(care_map_fd.get(), &file_content)) {
        SLOGE("Error reading care map contents to string.\n");
        return false;
    }

    std::vector<std::string> lines;
    lines = android::base::Split(android::base::Trim(file_content), "\n");
    if (lines.size() != 2 && lines.size() != 4) {
        SLOGE("Invalid lines in care_map: found %zu lines, expecting 2 or 4 lines.\n",
              lines.size());
        return false;
    }

    for (size_t i = 0; i < lines.size(); i += 2) {
        if (!read_blocks(lines[i], lines[i+1])) {
            return false;
        }
    }

    return true;
}

int main(int argc, char** argv) {
  for (int i = 1; i < argc; i++) {
    SLOGI("Started with arg %d: %s\n", i, argv[i]);
  }

  const hw_module_t* hw_module;
  if (hw_get_module("bootctrl", &hw_module) != 0) {
    SLOGE("Error getting bootctrl module.\n");
    return -1;
  }

  boot_control_module_t* module = reinterpret_cast<boot_control_module_t*>(
      const_cast<hw_module_t*>(hw_module));
  module->init(module);

  unsigned current_slot = module->getCurrentSlot(module);
  int is_successful= module->isSlotMarkedSuccessful(module, current_slot);
  SLOGI("Booting slot %u: isSlotMarkedSuccessful=%d\n", current_slot, is_successful);
  if (is_successful == 0) {
    // The current slot has not booted successfully.
    char verity_mode[PROPERTY_VALUE_MAX];
    if (property_get("ro.boot.veritymode", verity_mode, "") == -1) {
      SLOGE("Failed to get dm-verity mode");
      return -1;
    } else if (strcasecmp(verity_mode, "eio") == 0) {
      // We shouldn't see verity in EIO mode if the current slot hasn't booted
      // successfully before. Therefore, fail the verification when veritymode=eio.
      SLOGE("Found dm-verity in EIO mode, skip verification.");
      return -1;
    } else if (strcmp(verity_mode, "enforcing") != 0) {
      SLOGE("Unexpected dm-verity mode : %s, expecting enforcing.", verity_mode);
      return -1;
    } else if (!verify_image(CARE_MAP_FILE)) {
      SLOGE("Failed to verify all blocks in care map file.\n");
      return -1;
    }

    int ret = module->markBootSuccessful(module);
    if (ret != 0) {
      SLOGE("Error marking booted successfully: %s\n", strerror(-ret));
      return -1;
    }
    SLOGI("Marked slot %u as booted successfully.\n", current_slot);
  }

  SLOGI("Leaving update_verifier.\n");
  return 0;
}
