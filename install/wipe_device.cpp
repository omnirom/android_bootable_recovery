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

#include "install/wipe_device.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <stdint.h>
#include <sys/ioctl.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>
#include <android-base/unique_fd.h>
#include <ziparchive/zip_archive.h>

#include "bootloader_message/bootloader_message.h"
#include "install/install.h"
#include "install/package.h"
#include "recovery_ui/device.h"
#include "recovery_ui/ui.h"

std::vector<std::string> GetWipePartitionList(Package* wipe_package) {
  ZipArchiveHandle zip = wipe_package->GetZipArchiveHandle();
  if (!zip) {
    LOG(ERROR) << "Failed to get ZipArchiveHandle";
    return {};
  }

  constexpr char RECOVERY_WIPE_ENTRY_NAME[] = "recovery.wipe";

  std::string partition_list_content;
  ZipEntry entry;
  if (FindEntry(zip, RECOVERY_WIPE_ENTRY_NAME, &entry) == 0) {
    uint32_t length = entry.uncompressed_length;
    partition_list_content = std::string(length, '\0');
    if (auto err = ExtractToMemory(
            zip, &entry, reinterpret_cast<uint8_t*>(partition_list_content.data()), length);
        err != 0) {
      LOG(ERROR) << "Failed to extract " << RECOVERY_WIPE_ENTRY_NAME << ": "
                 << ErrorCodeString(err);
      return {};
    }
  } else {
    LOG(INFO) << "Failed to find " << RECOVERY_WIPE_ENTRY_NAME
              << ", falling back to use the partition list on device.";

    constexpr char RECOVERY_WIPE_ON_DEVICE[] = "/etc/recovery.wipe";
    if (!android::base::ReadFileToString(RECOVERY_WIPE_ON_DEVICE, &partition_list_content)) {
      PLOG(ERROR) << "failed to read \"" << RECOVERY_WIPE_ON_DEVICE << "\"";
      return {};
    }
  }

  std::vector<std::string> result;
  auto lines = android::base::Split(partition_list_content, "\n");
  for (const auto& line : lines) {
    auto partition = android::base::Trim(line);
    // Ignore '#' comment or empty lines.
    if (android::base::StartsWith(partition, "#") || partition.empty()) {
      continue;
    }
    result.push_back(line);
  }

  return result;
}

// Secure-wipes a given partition. It uses BLKSECDISCARD, if supported. Otherwise, it goes with
// BLKDISCARD (if device supports BLKDISCARDZEROES) or BLKZEROOUT.
static bool SecureWipePartition(const std::string& partition) {
  android::base::unique_fd fd(TEMP_FAILURE_RETRY(open(partition.c_str(), O_WRONLY)));
  if (fd == -1) {
    PLOG(ERROR) << "Failed to open \"" << partition << "\"";
    return false;
  }

  uint64_t range[2] = { 0, 0 };
  if (ioctl(fd, BLKGETSIZE64, &range[1]) == -1 || range[1] == 0) {
    PLOG(ERROR) << "Failed to get partition size";
    return false;
  }
  LOG(INFO) << "Secure-wiping \"" << partition << "\" from " << range[0] << " to " << range[1];

  LOG(INFO) << "  Trying BLKSECDISCARD...";
  if (ioctl(fd, BLKSECDISCARD, &range) == -1) {
    PLOG(WARNING) << "  Failed";

    // Use BLKDISCARD if it zeroes out blocks, otherwise use BLKZEROOUT.
    unsigned int zeroes;
    if (ioctl(fd, BLKDISCARDZEROES, &zeroes) == 0 && zeroes != 0) {
      LOG(INFO) << "  Trying BLKDISCARD...";
      if (ioctl(fd, BLKDISCARD, &range) == -1) {
        PLOG(ERROR) << "  Failed";
        return false;
      }
    } else {
      LOG(INFO) << "  Trying BLKZEROOUT...";
      if (ioctl(fd, BLKZEROOUT, &range) == -1) {
        PLOG(ERROR) << "  Failed";
        return false;
      }
    }
  }

  LOG(INFO) << "  Done";
  return true;
}

static std::unique_ptr<Package> ReadWipePackage(size_t wipe_package_size) {
  if (wipe_package_size == 0) {
    LOG(ERROR) << "wipe_package_size is zero";
    return nullptr;
  }

  std::string wipe_package;
  if (std::string err_str; !read_wipe_package(&wipe_package, wipe_package_size, &err_str)) {
    PLOG(ERROR) << "Failed to read wipe package" << err_str;
    return nullptr;
  }

  return Package::CreateMemoryPackage(
      std::vector<uint8_t>(wipe_package.begin(), wipe_package.end()), nullptr);
}

// Checks if the wipe package matches expectation. If the check passes, reads the list of
// partitions to wipe from the package. Checks include
// 1. verify the package.
// 2. check metadata (ota-type, pre-device and serial number if having one).
static bool CheckWipePackage(Package* wipe_package, RecoveryUI* ui) {
  if (!verify_package(wipe_package, ui)) {
    LOG(ERROR) << "Failed to verify package";
    return false;
  }

  ZipArchiveHandle zip = wipe_package->GetZipArchiveHandle();
  if (!zip) {
    LOG(ERROR) << "Failed to get ZipArchiveHandle";
    return false;
  }

  std::map<std::string, std::string> metadata;
  if (!ReadMetadataFromPackage(zip, &metadata)) {
    LOG(ERROR) << "Failed to parse metadata in the zip file";
    return false;
  }

  return CheckPackageMetadata(metadata, OtaType::BRICK);
}

bool WipeAbDevice(Device* device, size_t wipe_package_size) {
  auto ui = device->GetUI();
  ui->SetBackground(RecoveryUI::ERASING);
  ui->SetProgressType(RecoveryUI::INDETERMINATE);

  auto wipe_package = ReadWipePackage(wipe_package_size);
  if (!wipe_package) {
    LOG(ERROR) << "Failed to open wipe package";
    return false;
  }

  if (!CheckWipePackage(wipe_package.get(), ui)) {
    LOG(ERROR) << "Failed to verify wipe package";
    return false;
  }

  auto partition_list = GetWipePartitionList(wipe_package.get());
  if (partition_list.empty()) {
    LOG(ERROR) << "Empty wipe ab partition list";
    return false;
  }

  for (const auto& partition : partition_list) {
    // Proceed anyway even if it fails to wipe some partition.
    SecureWipePartition(partition);
  }
  return true;
}
