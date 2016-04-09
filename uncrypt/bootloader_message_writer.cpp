/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/system_properties.h>

#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/stringprintf.h>
#include <android-base/unique_fd.h>
#include <fs_mgr.h>

#include "bootloader.h"

static struct fstab* read_fstab(std::string* err) {
  // The fstab path is always "/fstab.${ro.hardware}".
  std::string fstab_path = "/fstab.";
  char value[PROP_VALUE_MAX];
  if (__system_property_get("ro.hardware", value) == 0) {
    *err = "failed to get ro.hardware";
    return nullptr;
  }
  fstab_path += value;
  struct fstab* fstab = fs_mgr_read_fstab(fstab_path.c_str());
  if (fstab == nullptr) {
    *err = "failed to read " + fstab_path;
  }
  return fstab;
}

static std::string get_misc_blk_device(std::string* err) {
  struct fstab* fstab = read_fstab(err);
  if (fstab == nullptr) {
    return "";
  }
  fstab_rec* record = fs_mgr_get_entry_for_mount_point(fstab, "/misc");
  if (record == nullptr) {
    *err = "failed to find /misc partition";
    return "";
  }
  return record->blk_device;
}

static bool write_bootloader_message(const bootloader_message& boot, std::string* err) {
  std::string misc_blk_device = get_misc_blk_device(err);
  if (misc_blk_device.empty()) {
    return false;
  }
  android::base::unique_fd fd(open(misc_blk_device.c_str(), O_WRONLY | O_SYNC));
  if (fd.get() == -1) {
    *err = android::base::StringPrintf("failed to open %s: %s", misc_blk_device.c_str(),
                                       strerror(errno));
    return false;
  }
  if (!android::base::WriteFully(fd.get(), &boot, sizeof(boot))) {
    *err = android::base::StringPrintf("failed to write %s: %s", misc_blk_device.c_str(),
                                       strerror(errno));
    return false;
  }
  // TODO: O_SYNC and fsync duplicates each other?
  if (fsync(fd.get()) == -1) {
    *err = android::base::StringPrintf("failed to fsync %s: %s", misc_blk_device.c_str(),
                                       strerror(errno));
    return false;
  }
  return true;
}

bool clear_bootloader_message(std::string* err) {
  bootloader_message boot = {};
  return write_bootloader_message(boot, err);
}

bool write_bootloader_message(const std::vector<std::string>& options, std::string* err) {
  bootloader_message boot = {};
  strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
  strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
  for (const auto& s : options) {
    strlcat(boot.recovery, s.c_str(), sizeof(boot.recovery));
    if (s.back() != '\n') {
      strlcat(boot.recovery, "\n", sizeof(boot.recovery));
    }
  }
  return write_bootloader_message(boot, err);
}

extern "C" bool write_bootloader_message(const char* options) {
  std::string err;
  return write_bootloader_message({options}, &err);
}
