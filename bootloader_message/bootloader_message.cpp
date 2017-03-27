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

#include <bootloader_message/bootloader_message.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>

#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/properties.h>
#include <android-base/stringprintf.h>
#include <android-base/unique_fd.h>
#include <fs_mgr.h>

static std::string get_misc_blk_device(std::string* err) {
  std::unique_ptr<fstab, decltype(&fs_mgr_free_fstab)> fstab(fs_mgr_read_fstab_default(),
                                                             fs_mgr_free_fstab);
  if (!fstab) {
    *err = "failed to read default fstab";
    return "";
  }
  fstab_rec* record = fs_mgr_get_entry_for_mount_point(fstab.get(), "/misc");
  if (record == nullptr) {
    *err = "failed to find /misc partition";
    return "";
  }
  return record->blk_device;
}

// In recovery mode, recovery can get started and try to access the misc
// device before the kernel has actually created it.
static bool wait_for_device(const std::string& blk_device, std::string* err) {
  int tries = 0;
  int ret;
  err->clear();
  do {
    ++tries;
    struct stat buf;
    ret = stat(blk_device.c_str(), &buf);
    if (ret == -1) {
      *err += android::base::StringPrintf("failed to stat %s try %d: %s\n",
                                          blk_device.c_str(), tries, strerror(errno));
      sleep(1);
    }
  } while (ret && tries < 10);

  if (ret) {
    *err += android::base::StringPrintf("failed to stat %s\n", blk_device.c_str());
  }
  return ret == 0;
}

static bool read_misc_partition(void* p, size_t size, const std::string& misc_blk_device,
                                size_t offset, std::string* err) {
  if (!wait_for_device(misc_blk_device, err)) {
    return false;
  }
  android::base::unique_fd fd(open(misc_blk_device.c_str(), O_RDONLY));
  if (fd == -1) {
    *err = android::base::StringPrintf("failed to open %s: %s", misc_blk_device.c_str(),
                                       strerror(errno));
    return false;
  }
  if (lseek(fd, static_cast<off_t>(offset), SEEK_SET) != static_cast<off_t>(offset)) {
    *err = android::base::StringPrintf("failed to lseek %s: %s", misc_blk_device.c_str(),
                                       strerror(errno));
    return false;
  }
  if (!android::base::ReadFully(fd, p, size)) {
    *err = android::base::StringPrintf("failed to read %s: %s", misc_blk_device.c_str(),
                                       strerror(errno));
    return false;
  }
  return true;
}

static bool write_misc_partition(const void* p, size_t size, const std::string& misc_blk_device,
                                 size_t offset, std::string* err) {
  android::base::unique_fd fd(open(misc_blk_device.c_str(), O_WRONLY));
  if (fd == -1) {
    *err = android::base::StringPrintf("failed to open %s: %s", misc_blk_device.c_str(),
                                       strerror(errno));
    return false;
  }
  if (lseek(fd, static_cast<off_t>(offset), SEEK_SET) != static_cast<off_t>(offset)) {
    *err = android::base::StringPrintf("failed to lseek %s: %s", misc_blk_device.c_str(),
                                       strerror(errno));
    return false;
  }
  if (!android::base::WriteFully(fd, p, size)) {
    *err = android::base::StringPrintf("failed to write %s: %s", misc_blk_device.c_str(),
                                       strerror(errno));
    return false;
  }
  if (fsync(fd) == -1) {
    *err = android::base::StringPrintf("failed to fsync %s: %s", misc_blk_device.c_str(),
                                       strerror(errno));
    return false;
  }
  return true;
}

bool read_bootloader_message_from(bootloader_message* boot, const std::string& misc_blk_device,
                                  std::string* err) {
  return read_misc_partition(boot, sizeof(*boot), misc_blk_device,
                             BOOTLOADER_MESSAGE_OFFSET_IN_MISC, err);
}

bool read_bootloader_message(bootloader_message* boot, std::string* err) {
  std::string misc_blk_device = get_misc_blk_device(err);
  if (misc_blk_device.empty()) {
    return false;
  }
  return read_bootloader_message_from(boot, misc_blk_device, err);
}

bool write_bootloader_message_to(const bootloader_message& boot, const std::string& misc_blk_device,
                                 std::string* err) {
  return write_misc_partition(&boot, sizeof(boot), misc_blk_device,
                              BOOTLOADER_MESSAGE_OFFSET_IN_MISC, err);
}

bool write_bootloader_message(const bootloader_message& boot, std::string* err) {
  std::string misc_blk_device = get_misc_blk_device(err);
  if (misc_blk_device.empty()) {
    return false;
  }
  return write_bootloader_message_to(boot, misc_blk_device, err);
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

bool update_bootloader_message(const std::vector<std::string>& options, std::string* err) {
  bootloader_message boot;
  if (!read_bootloader_message(&boot, err)) {
    return false;
  }

  // Zero out the entire fields.
  memset(boot.command, 0, sizeof(boot.command));
  memset(boot.recovery, 0, sizeof(boot.recovery));

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

bool write_reboot_bootloader(std::string* err) {
  bootloader_message boot;
  if (!read_bootloader_message(&boot, err)) {
    return false;
  }
  if (boot.command[0] != '\0') {
    *err = "Bootloader command pending.";
    return false;
  }
  strlcpy(boot.command, "bootonce-bootloader", sizeof(boot.command));
  return write_bootloader_message(boot, err);
}

bool read_wipe_package(std::string* package_data, size_t size, std::string* err) {
  std::string misc_blk_device = get_misc_blk_device(err);
  if (misc_blk_device.empty()) {
    return false;
  }
  package_data->resize(size);
  return read_misc_partition(&(*package_data)[0], size, misc_blk_device,
                             WIPE_PACKAGE_OFFSET_IN_MISC, err);
}

bool write_wipe_package(const std::string& package_data, std::string* err) {
  std::string misc_blk_device = get_misc_blk_device(err);
  if (misc_blk_device.empty()) {
    return false;
  }
  return write_misc_partition(package_data.data(), package_data.size(), misc_blk_device,
                              WIPE_PACKAGE_OFFSET_IN_MISC, err);
}

extern "C" bool write_reboot_bootloader(void) {
  std::string err;
  return write_reboot_bootloader(&err);
}

extern "C" bool write_bootloader_message(const char* options) {
  std::string err;
  return write_bootloader_message({options}, &err);
}
