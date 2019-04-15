/*
 * Copyright (C) 2007 The Android Open Source Project
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

#include "recovery.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <linux/fs.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/properties.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#include <android-base/unique_fd.h>
#include <bootloader_message/bootloader_message.h>
#include <cutils/properties.h> /* for property_list */
#include <healthhalutils/HealthHalUtils.h>
#include <ziparchive/zip_archive.h>

#include "common.h"
#include "fsck_unshare_blocks.h"
#include "install/adb_install.h"
#include "install/fuse_sdcard_install.h"
#include "install/install.h"
#include "install/package.h"
#include "install/wipe_data.h"
#include "otautil/error_code.h"
#include "otautil/logging.h"
#include "otautil/paths.h"
#include "otautil/roots.h"
#include "otautil/sysutil.h"
#include "recovery_ui/screen_ui.h"
#include "recovery_ui/ui.h"

static constexpr const char* COMMAND_FILE = "/cache/recovery/command";
static constexpr const char* LAST_KMSG_FILE = "/cache/recovery/last_kmsg";
static constexpr const char* LAST_LOG_FILE = "/cache/recovery/last_log";
static constexpr const char* LOCALE_FILE = "/cache/recovery/last_locale";

static constexpr const char* CACHE_ROOT = "/cache";

// We define RECOVERY_API_VERSION in Android.mk, which will be picked up by build system and packed
// into target_files.zip. Assert the version defined in code and in Android.mk are consistent.
static_assert(kRecoveryApiVersion == RECOVERY_API_VERSION, "Mismatching recovery API versions.");

static bool save_current_log = false;
std::string stage;
const char* reason = nullptr;

/*
 * The recovery tool communicates with the main system through /cache files.
 *   /cache/recovery/command - INPUT - command line for tool, one arg per line
 *   /cache/recovery/log - OUTPUT - combined log file from recovery run(s)
 *
 * The arguments which may be supplied in the recovery.command file:
 *   --update_package=path - verify install an OTA package file
 *   --wipe_data - erase user data (and cache), then reboot
 *   --prompt_and_wipe_data - prompt the user that data is corrupt, with their consent erase user
 *       data (and cache), then reboot
 *   --wipe_cache - wipe cache (but not user data), then reboot
 *   --show_text - show the recovery text menu, used by some bootloader (e.g. http://b/36872519).
 *   --set_encrypted_filesystem=on|off - enables / diasables encrypted fs
 *   --just_exit - do nothing; exit and reboot
 *
 * After completing, we remove /cache/recovery/command and reboot.
 * Arguments may also be supplied in the bootloader control block (BCB).
 * These important scenarios must be safely restartable at any point:
 *
 * FACTORY RESET
 * 1. user selects "factory reset"
 * 2. main system writes "--wipe_data" to /cache/recovery/command
 * 3. main system reboots into recovery
 * 4. get_args() writes BCB with "boot-recovery" and "--wipe_data"
 *    -- after this, rebooting will restart the erase --
 * 5. erase_volume() reformats /data
 * 6. erase_volume() reformats /cache
 * 7. finish_recovery() erases BCB
 *    -- after this, rebooting will restart the main system --
 * 8. main() calls reboot() to boot main system
 *
 * OTA INSTALL
 * 1. main system downloads OTA package to /cache/some-filename.zip
 * 2. main system writes "--update_package=/cache/some-filename.zip"
 * 3. main system reboots into recovery
 * 4. get_args() writes BCB with "boot-recovery" and "--update_package=..."
 *    -- after this, rebooting will attempt to reinstall the update --
 * 5. install_package() attempts to install the update
 *    NOTE: the package install must itself be restartable from any point
 * 6. finish_recovery() erases BCB
 *    -- after this, rebooting will (try to) restart the main system --
 * 7. ** if install failed **
 *    7a. prompt_and_wait() shows an error icon and waits for the user
 *    7b. the user reboots (pulling the battery, etc) into the main system
 */

bool is_ro_debuggable() {
    return android::base::GetBoolProperty("ro.debuggable", false);
}

// Clear the recovery command and prepare to boot a (hopefully working) system,
// copy our log file to cache as well (for the system to read). This function is
// idempotent: call it as many times as you like.
static void finish_recovery() {
  std::string locale = ui->GetLocale();
  // Save the locale to cache, so if recovery is next started up without a '--locale' argument
  // (e.g., directly from the bootloader) it will use the last-known locale.
  if (!locale.empty() && has_cache) {
    LOG(INFO) << "Saving locale \"" << locale << "\"";
    if (ensure_path_mounted(LOCALE_FILE) != 0) {
      LOG(ERROR) << "Failed to mount " << LOCALE_FILE;
    } else if (!android::base::WriteStringToFile(locale, LOCALE_FILE)) {
      PLOG(ERROR) << "Failed to save locale to " << LOCALE_FILE;
    }
  }

  copy_logs(save_current_log, has_cache, sehandle);

  // Reset to normal system boot so recovery won't cycle indefinitely.
  std::string err;
  if (!clear_bootloader_message(&err)) {
    LOG(ERROR) << "Failed to clear BCB message: " << err;
  }

  // Remove the command file, so recovery won't repeat indefinitely.
  if (has_cache) {
    if (ensure_path_mounted(COMMAND_FILE) != 0 || (unlink(COMMAND_FILE) && errno != ENOENT)) {
      LOG(WARNING) << "Can't unlink " << COMMAND_FILE;
    }
    ensure_path_unmounted(CACHE_ROOT);
  }

  sync();  // For good measure.
}

static bool yes_no(Device* device, const char* question1, const char* question2) {
  std::vector<std::string> headers{ question1, question2 };
  std::vector<std::string> items{ " No", " Yes" };

  size_t chosen_item = ui->ShowMenu(
      headers, items, 0, true,
      std::bind(&Device::HandleMenuKey, device, std::placeholders::_1, std::placeholders::_2));
  return (chosen_item == 1);
}

static bool ask_to_wipe_data(Device* device) {
  std::vector<std::string> headers{ "Wipe all user data?", "  THIS CAN NOT BE UNDONE!" };
  std::vector<std::string> items{ " Cancel", " Factory data reset" };

  size_t chosen_item = ui->ShowPromptWipeDataConfirmationMenu(
      headers, items,
      std::bind(&Device::HandleMenuKey, device, std::placeholders::_1, std::placeholders::_2));

  return (chosen_item == 1);
}

static InstallResult prompt_and_wipe_data(Device* device) {
  // Use a single string and let ScreenRecoveryUI handles the wrapping.
  std::vector<std::string> wipe_data_menu_headers{
    "Can't load Android system. Your data may be corrupt. "
    "If you continue to get this message, you may need to "
    "perform a factory data reset and erase all user data "
    "stored on this device.",
  };
  // clang-format off
  std::vector<std::string> wipe_data_menu_items {
    "Try again",
    "Factory data reset",
  };
  // clang-format on
  for (;;) {
    size_t chosen_item = ui->ShowPromptWipeDataMenu(
        wipe_data_menu_headers, wipe_data_menu_items,
        std::bind(&Device::HandleMenuKey, device, std::placeholders::_1, std::placeholders::_2));
    // If ShowMenu() returned RecoveryUI::KeyError::INTERRUPTED, WaitKey() was interrupted.
    if (chosen_item == static_cast<size_t>(RecoveryUI::KeyError::INTERRUPTED)) {
      return INSTALL_KEY_INTERRUPTED;
    }
    if (chosen_item != 1) {
      return INSTALL_SUCCESS;  // Just reboot, no wipe; not a failure, user asked for it
    }

    if (ask_to_wipe_data(device)) {
      bool convert_fbe = reason && strcmp(reason, "convert_fbe") == 0;
      if (WipeData(device, convert_fbe)) {
        return INSTALL_SUCCESS;
      } else {
        return INSTALL_ERROR;
      }
    }
  }
}

// Secure-wipe a given partition. It uses BLKSECDISCARD, if supported. Otherwise, it goes with
// BLKDISCARD (if device supports BLKDISCARDZEROES) or BLKZEROOUT.
static bool secure_wipe_partition(const std::string& partition) {
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
  std::string err_str;
  if (!read_wipe_package(&wipe_package, wipe_package_size, &err_str)) {
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
static bool CheckWipePackage(Package* wipe_package) {
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

  return CheckPackageMetadata(metadata, OtaType::BRICK) == 0;
}

std::vector<std::string> GetWipePartitionList(Package* wipe_package) {
  ZipArchiveHandle zip = wipe_package->GetZipArchiveHandle();
  if (!zip) {
    LOG(ERROR) << "Failed to get ZipArchiveHandle";
    return {};
  }

  static constexpr const char* RECOVERY_WIPE_ENTRY_NAME = "recovery.wipe";

  std::string partition_list_content;
  ZipString path(RECOVERY_WIPE_ENTRY_NAME);
  ZipEntry entry;
  if (FindEntry(zip, path, &entry) == 0) {
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

    static constexpr const char* RECOVERY_WIPE_ON_DEVICE = "/etc/recovery.wipe";
    if (!android::base::ReadFileToString(RECOVERY_WIPE_ON_DEVICE, &partition_list_content)) {
      PLOG(ERROR) << "failed to read \"" << RECOVERY_WIPE_ON_DEVICE << "\"";
      return {};
    }
  }

  std::vector<std::string> result;
  std::vector<std::string> lines = android::base::Split(partition_list_content, "\n");
  for (const std::string& line : lines) {
    std::string partition = android::base::Trim(line);
    // Ignore '#' comment or empty lines.
    if (android::base::StartsWith(partition, "#") || partition.empty()) {
      continue;
    }
    result.push_back(line);
  }

  return result;
}

// Wipes the current A/B device, with a secure wipe of all the partitions in RECOVERY_WIPE.
static bool wipe_ab_device(size_t wipe_package_size) {
  ui->SetBackground(RecoveryUI::ERASING);
  ui->SetProgressType(RecoveryUI::INDETERMINATE);

  auto wipe_package = ReadWipePackage(wipe_package_size);
  if (!wipe_package) {
    LOG(ERROR) << "Failed to open wipe package";
    return false;
  }

  if (!CheckWipePackage(wipe_package.get())) {
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
    secure_wipe_partition(partition);
  }
  return true;
}

static void choose_recovery_file(Device* device) {
  std::vector<std::string> entries;
  if (has_cache) {
    for (int i = 0; i < KEEP_LOG_COUNT; i++) {
      auto add_to_entries = [&](const char* filename) {
        std::string log_file(filename);
        if (i > 0) {
          log_file += "." + std::to_string(i);
        }

        if (ensure_path_mounted(log_file) == 0 && access(log_file.c_str(), R_OK) == 0) {
          entries.push_back(std::move(log_file));
        }
      };

      // Add LAST_LOG_FILE + LAST_LOG_FILE.x
      add_to_entries(LAST_LOG_FILE);

      // Add LAST_KMSG_FILE + LAST_KMSG_FILE.x
      add_to_entries(LAST_KMSG_FILE);
    }
  } else {
    // If cache partition is not found, view /tmp/recovery.log instead.
    if (access(Paths::Get().temporary_log_file().c_str(), R_OK) == -1) {
      return;
    } else {
      entries.push_back(Paths::Get().temporary_log_file());
    }
  }

  entries.push_back("Back");

  std::vector<std::string> headers{ "Select file to view" };

  size_t chosen_item = 0;
  while (true) {
    chosen_item = ui->ShowMenu(
        headers, entries, chosen_item, true,
        std::bind(&Device::HandleMenuKey, device, std::placeholders::_1, std::placeholders::_2));

    // Handle WaitKey() interrupt.
    if (chosen_item == static_cast<size_t>(RecoveryUI::KeyError::INTERRUPTED)) {
      break;
    }
    if (entries[chosen_item] == "Back") break;

    ui->ShowFile(entries[chosen_item]);
  }
}

static void run_graphics_test() {
  // Switch to graphics screen.
  ui->ShowText(false);

  ui->SetProgressType(RecoveryUI::INDETERMINATE);
  ui->SetBackground(RecoveryUI::INSTALLING_UPDATE);
  sleep(1);

  ui->SetBackground(RecoveryUI::ERROR);
  sleep(1);

  ui->SetBackground(RecoveryUI::NO_COMMAND);
  sleep(1);

  ui->SetBackground(RecoveryUI::ERASING);
  sleep(1);

  // Calling SetBackground() after SetStage() to trigger a redraw.
  ui->SetStage(1, 3);
  ui->SetBackground(RecoveryUI::INSTALLING_UPDATE);
  sleep(1);
  ui->SetStage(2, 3);
  ui->SetBackground(RecoveryUI::INSTALLING_UPDATE);
  sleep(1);
  ui->SetStage(3, 3);
  ui->SetBackground(RecoveryUI::INSTALLING_UPDATE);
  sleep(1);

  ui->SetStage(-1, -1);
  ui->SetBackground(RecoveryUI::INSTALLING_UPDATE);

  ui->SetProgressType(RecoveryUI::DETERMINATE);
  ui->ShowProgress(1.0, 10.0);
  float fraction = 0.0;
  for (size_t i = 0; i < 100; ++i) {
    fraction += .01;
    ui->SetProgress(fraction);
    usleep(100000);
  }

  ui->ShowText(true);
}

// Returns REBOOT, SHUTDOWN, or REBOOT_BOOTLOADER. Returning NO_ACTION means to take the default,
// which is to reboot or shutdown depending on if the --shutdown_after flag was passed to recovery.
static Device::BuiltinAction prompt_and_wait(Device* device, int status) {
  for (;;) {
    finish_recovery();
    switch (status) {
      case INSTALL_SUCCESS:
      case INSTALL_NONE:
        ui->SetBackground(RecoveryUI::NO_COMMAND);
        break;

      case INSTALL_ERROR:
      case INSTALL_CORRUPT:
        ui->SetBackground(RecoveryUI::ERROR);
        break;
    }
    ui->SetProgressType(RecoveryUI::EMPTY);

    size_t chosen_item = ui->ShowMenu(
        {}, device->GetMenuItems(), 0, false,
        std::bind(&Device::HandleMenuKey, device, std::placeholders::_1, std::placeholders::_2));
    // Handle Interrupt key
    if (chosen_item == static_cast<size_t>(RecoveryUI::KeyError::INTERRUPTED)) {
      return Device::KEY_INTERRUPTED;
    }
    // Device-specific code may take some action here. It may return one of the core actions
    // handled in the switch statement below.
    Device::BuiltinAction chosen_action =
        (chosen_item == static_cast<size_t>(RecoveryUI::KeyError::TIMED_OUT))
            ? Device::REBOOT
            : device->InvokeMenuItem(chosen_item);

    switch (chosen_action) {
      case Device::NO_ACTION:
        break;

      case Device::REBOOT:
      case Device::SHUTDOWN:
      case Device::REBOOT_BOOTLOADER:
      case Device::ENTER_FASTBOOT:
      case Device::ENTER_RECOVERY:
        return chosen_action;

      case Device::WIPE_DATA:
        save_current_log = true;
        if (ui->IsTextVisible()) {
          if (ask_to_wipe_data(device)) {
            WipeData(device, false);
          }
        } else {
          WipeData(device, false);
          return Device::NO_ACTION;
        }
        break;

      case Device::WIPE_CACHE: {
        save_current_log = true;
        std::function<bool()> confirm_func = [&device]() {
          return yes_no(device, "Wipe cache?", "  THIS CAN NOT BE UNDONE!");
        };
        WipeCache(ui, ui->IsTextVisible() ? confirm_func : nullptr);
        if (!ui->IsTextVisible()) return Device::NO_ACTION;
        break;
      }
      case Device::APPLY_ADB_SIDELOAD:
      case Device::APPLY_SDCARD: {
        save_current_log = true;
        bool adb = (chosen_action == Device::APPLY_ADB_SIDELOAD);
        if (adb) {
          status = apply_from_adb(ui);
        } else {
          status = ApplyFromSdcard(device, ui);
        }

        if (status != INSTALL_SUCCESS) {
          ui->SetBackground(RecoveryUI::ERROR);
          ui->Print("Installation aborted.\n");
          copy_logs(save_current_log, has_cache, sehandle);
        } else if (!ui->IsTextVisible()) {
          return Device::NO_ACTION;  // reboot if logs aren't visible
        } else {
          ui->Print("\nInstall from %s complete.\n", adb ? "ADB" : "SD card");
        }
        break;
      }

      case Device::VIEW_RECOVERY_LOGS:
        choose_recovery_file(device);
        break;

      case Device::RUN_GRAPHICS_TEST:
        run_graphics_test();
        break;

      case Device::RUN_LOCALE_TEST: {
        ScreenRecoveryUI* screen_ui = static_cast<ScreenRecoveryUI*>(ui);
        screen_ui->CheckBackgroundTextImages();
        break;
      }
      case Device::MOUNT_SYSTEM:
        // the system partition is mounted at /mnt/system
        if (ensure_path_mounted_at(get_system_root(), "/mnt/system") != -1) {
          ui->Print("Mounted /system.\n");
        }
        break;

      case Device::KEY_INTERRUPTED:
        return Device::KEY_INTERRUPTED;
    }
  }
}

static void print_property(const char* key, const char* name, void* /* cookie */) {
  printf("%s=%s\n", key, name);
}

static bool is_battery_ok(int* required_battery_level) {
  using android::hardware::health::V1_0::BatteryStatus;
  using android::hardware::health::V2_0::get_health_service;
  using android::hardware::health::V2_0::IHealth;
  using android::hardware::health::V2_0::Result;
  using android::hardware::health::V2_0::toString;

  android::sp<IHealth> health = get_health_service();

  static constexpr int BATTERY_READ_TIMEOUT_IN_SEC = 10;
  int wait_second = 0;
  while (true) {
    auto charge_status = BatteryStatus::UNKNOWN;

    if (health == nullptr) {
      LOG(WARNING) << "no health implementation is found, assuming defaults";
    } else {
      health
          ->getChargeStatus([&charge_status](auto res, auto out_status) {
            if (res == Result::SUCCESS) {
              charge_status = out_status;
            }
          })
          .isOk();  // should not have transport error
    }

    // Treat unknown status as charged.
    bool charged = (charge_status != BatteryStatus::DISCHARGING &&
                    charge_status != BatteryStatus::NOT_CHARGING);

    Result res = Result::UNKNOWN;
    int32_t capacity = INT32_MIN;
    if (health != nullptr) {
      health
          ->getCapacity([&res, &capacity](auto out_res, auto out_capacity) {
            res = out_res;
            capacity = out_capacity;
          })
          .isOk();  // should not have transport error
    }

    LOG(INFO) << "charge_status " << toString(charge_status) << ", charged " << charged
              << ", status " << toString(res) << ", capacity " << capacity;
    // At startup, the battery drivers in devices like N5X/N6P take some time to load
    // the battery profile. Before the load finishes, it reports value 50 as a fake
    // capacity. BATTERY_READ_TIMEOUT_IN_SEC is set that the battery drivers are expected
    // to finish loading the battery profile earlier than 10 seconds after kernel startup.
    if (res == Result::SUCCESS && capacity == 50) {
      if (wait_second < BATTERY_READ_TIMEOUT_IN_SEC) {
        sleep(1);
        wait_second++;
        continue;
      }
    }
    // If we can't read battery percentage, it may be a device without battery. In this
    // situation, use 100 as a fake battery percentage.
    if (res != Result::SUCCESS) {
      capacity = 100;
    }

    // GmsCore enters recovery mode to install package when having enough battery percentage.
    // Normally, the threshold is 40% without charger and 20% with charger. So we should check
    // battery with a slightly lower limitation.
    static constexpr int BATTERY_OK_PERCENTAGE = 20;
    static constexpr int BATTERY_WITH_CHARGER_OK_PERCENTAGE = 15;
    *required_battery_level = charged ? BATTERY_WITH_CHARGER_OK_PERCENTAGE : BATTERY_OK_PERCENTAGE;
    return capacity >= *required_battery_level;
  }
}

// Set the retry count to |retry_count| in BCB.
static void set_retry_bootloader_message(int retry_count, const std::vector<std::string>& args) {
  std::vector<std::string> options;
  for (const auto& arg : args) {
    if (!android::base::StartsWith(arg, "--retry_count")) {
      options.push_back(arg);
    }
  }

  // Update the retry counter in BCB.
  options.push_back(android::base::StringPrintf("--retry_count=%d", retry_count));
  std::string err;
  if (!update_bootloader_message(options, &err)) {
    LOG(ERROR) << err;
  }
}

static bool bootreason_in_blacklist() {
  std::string bootreason = android::base::GetProperty("ro.boot.bootreason", "");
  if (!bootreason.empty()) {
    // More bootreasons can be found in "system/core/bootstat/bootstat.cpp".
    static const std::vector<std::string> kBootreasonBlacklist{
      "kernel_panic",
      "Panic",
    };
    for (const auto& str : kBootreasonBlacklist) {
      if (android::base::EqualsIgnoreCase(str, bootreason)) return true;
    }
  }
  return false;
}

static void log_failure_code(ErrorCode code, const std::string& update_package) {
  std::vector<std::string> log_buffer = {
    update_package,
    "0",  // install result
    "error: " + std::to_string(code),
  };
  std::string log_content = android::base::Join(log_buffer, "\n");
  const std::string& install_file = Paths::Get().temporary_install_file();
  if (!android::base::WriteStringToFile(log_content, install_file)) {
    PLOG(ERROR) << "Failed to write " << install_file;
  }

  // Also write the info into last_log.
  LOG(INFO) << log_content;
}

Device::BuiltinAction start_recovery(Device* device, const std::vector<std::string>& args) {
  static constexpr struct option OPTIONS[] = {
    { "fastboot", no_argument, nullptr, 0 },
    { "fsck_unshare_blocks", no_argument, nullptr, 0 },
    { "just_exit", no_argument, nullptr, 'x' },
    { "locale", required_argument, nullptr, 0 },
    { "prompt_and_wipe_data", no_argument, nullptr, 0 },
    { "reason", required_argument, nullptr, 0 },
    { "retry_count", required_argument, nullptr, 0 },
    { "security", no_argument, nullptr, 0 },
    { "show_text", no_argument, nullptr, 't' },
    { "shutdown_after", no_argument, nullptr, 0 },
    { "sideload", no_argument, nullptr, 0 },
    { "sideload_auto_reboot", no_argument, nullptr, 0 },
    { "update_package", required_argument, nullptr, 0 },
    { "wipe_ab", no_argument, nullptr, 0 },
    { "wipe_cache", no_argument, nullptr, 0 },
    { "wipe_data", no_argument, nullptr, 0 },
    { "wipe_package_size", required_argument, nullptr, 0 },
    { nullptr, 0, nullptr, 0 },
  };

  const char* update_package = nullptr;
  bool should_wipe_data = false;
  bool should_prompt_and_wipe_data = false;
  bool should_wipe_cache = false;
  bool should_wipe_ab = false;
  size_t wipe_package_size = 0;
  bool sideload = false;
  bool sideload_auto_reboot = false;
  bool just_exit = false;
  bool shutdown_after = false;
  bool fsck_unshare_blocks = false;
  int retry_count = 0;
  bool security_update = false;
  std::string locale;

  auto args_to_parse = StringVectorToNullTerminatedArray(args);

  int arg;
  int option_index;
  // Parse everything before the last element (which must be a nullptr). getopt_long(3) expects a
  // null-terminated char* array, but without counting null as an arg (i.e. argv[argc] should be
  // nullptr).
  while ((arg = getopt_long(args_to_parse.size() - 1, args_to_parse.data(), "", OPTIONS,
                            &option_index)) != -1) {
    switch (arg) {
      case 't':
        // Handled in recovery_main.cpp
        break;
      case 'x':
        just_exit = true;
        break;
      case 0: {
        std::string option = OPTIONS[option_index].name;
        if (option == "fsck_unshare_blocks") {
          fsck_unshare_blocks = true;
        } else if (option == "locale" || option == "fastboot") {
          // Handled in recovery_main.cpp
        } else if (option == "prompt_and_wipe_data") {
          should_prompt_and_wipe_data = true;
        } else if (option == "reason") {
          reason = optarg;
        } else if (option == "retry_count") {
          android::base::ParseInt(optarg, &retry_count, 0);
        } else if (option == "security") {
          security_update = true;
        } else if (option == "sideload") {
          sideload = true;
        } else if (option == "sideload_auto_reboot") {
          sideload = true;
          sideload_auto_reboot = true;
        } else if (option == "shutdown_after") {
          shutdown_after = true;
        } else if (option == "update_package") {
          update_package = optarg;
        } else if (option == "wipe_ab") {
          should_wipe_ab = true;
        } else if (option == "wipe_cache") {
          should_wipe_cache = true;
        } else if (option == "wipe_data") {
          should_wipe_data = true;
        } else if (option == "wipe_package_size") {
          android::base::ParseUint(optarg, &wipe_package_size);
        }
        break;
      }
      case '?':
        LOG(ERROR) << "Invalid command argument";
        continue;
    }
  }
  optind = 1;

  printf("stage is [%s]\n", stage.c_str());
  printf("reason is [%s]\n", reason);

  // Set background string to "installing security update" for security update,
  // otherwise set it to "installing system update".
  ui->SetSystemUpdateText(security_update);

  int st_cur, st_max;
  if (!stage.empty() && sscanf(stage.c_str(), "%d/%d", &st_cur, &st_max) == 2) {
    ui->SetStage(st_cur, st_max);
  }

  std::vector<std::string> title_lines =
      android::base::Split(android::base::GetProperty("ro.bootimage.build.fingerprint", ""), ":");
  title_lines.insert(std::begin(title_lines), "Android Recovery");
  ui->SetTitle(title_lines);

  ui->ResetKeyInterruptStatus();
  device->StartRecovery();

  printf("Command:");
  for (const auto& arg : args) {
    printf(" \"%s\"", arg.c_str());
  }
  printf("\n\n");

  property_list(print_property, nullptr);
  printf("\n");

  ui->Print("Supported API: %d\n", kRecoveryApiVersion);

  int status = INSTALL_SUCCESS;

  if (update_package != nullptr) {
    // It's not entirely true that we will modify the flash. But we want
    // to log the update attempt since update_package is non-NULL.
    save_current_log = true;

    int required_battery_level;
    if (retry_count == 0 && !is_battery_ok(&required_battery_level)) {
      ui->Print("battery capacity is not enough for installing package: %d%% needed\n",
                required_battery_level);
      // Log the error code to last_install when installation skips due to
      // low battery.
      log_failure_code(kLowBattery, update_package);
      status = INSTALL_SKIPPED;
    } else if (retry_count == 0 && bootreason_in_blacklist()) {
      // Skip update-on-reboot when bootreason is kernel_panic or similar
      ui->Print("bootreason is in the blacklist; skip OTA installation\n");
      log_failure_code(kBootreasonInBlacklist, update_package);
      status = INSTALL_SKIPPED;
    } else {
      // It's a fresh update. Initialize the retry_count in the BCB to 1; therefore we can later
      // identify the interrupted update due to unexpected reboots.
      if (retry_count == 0) {
        set_retry_bootloader_message(retry_count + 1, args);
      }

      status = install_package(update_package, should_wipe_cache, true, retry_count, ui);
      if (status != INSTALL_SUCCESS) {
        ui->Print("Installation aborted.\n");

        // When I/O error or bspatch/imgpatch error happens, reboot and retry installation
        // RETRY_LIMIT times before we abandon this OTA update.
        static constexpr int RETRY_LIMIT = 4;
        if (status == INSTALL_RETRY && retry_count < RETRY_LIMIT) {
          copy_logs(save_current_log, has_cache, sehandle);
          retry_count += 1;
          set_retry_bootloader_message(retry_count, args);
          // Print retry count on screen.
          ui->Print("Retry attempt %d\n", retry_count);

          // Reboot and retry the update
          if (!reboot("reboot,recovery")) {
            ui->Print("Reboot failed\n");
          } else {
            while (true) {
              pause();
            }
          }
        }
        // If this is an eng or userdebug build, then automatically
        // turn the text display on if the script fails so the error
        // message is visible.
        if (is_ro_debuggable()) {
          ui->ShowText(true);
        }
      }
    }
  } else if (should_wipe_data) {
    save_current_log = true;
    bool convert_fbe = reason && strcmp(reason, "convert_fbe") == 0;
    if (!WipeData(device, convert_fbe)) {
      status = INSTALL_ERROR;
    }
  } else if (should_prompt_and_wipe_data) {
    // Trigger the logging to capture the cause, even if user chooses to not wipe data.
    save_current_log = true;

    ui->ShowText(true);
    ui->SetBackground(RecoveryUI::ERROR);
    status = prompt_and_wipe_data(device);
    if (status != INSTALL_KEY_INTERRUPTED) {
      ui->ShowText(false);
    }
  } else if (should_wipe_cache) {
    save_current_log = true;
    if (!WipeCache(ui, nullptr)) {
      status = INSTALL_ERROR;
    }
  } else if (should_wipe_ab) {
    if (!wipe_ab_device(wipe_package_size)) {
      status = INSTALL_ERROR;
    }
  } else if (sideload) {
    // 'adb reboot sideload' acts the same as user presses key combinations
    // to enter the sideload mode. When 'sideload-auto-reboot' is used, text
    // display will NOT be turned on by default. And it will reboot after
    // sideload finishes even if there are errors. Unless one turns on the
    // text display during the installation. This is to enable automated
    // testing.
    save_current_log = true;
    if (!sideload_auto_reboot) {
      ui->ShowText(true);
    }
    status = apply_from_adb(ui);
    ui->Print("\nInstall from ADB complete (status: %d).\n", status);
    if (sideload_auto_reboot) {
      ui->Print("Rebooting automatically.\n");
    }
  } else if (fsck_unshare_blocks) {
    if (!do_fsck_unshare_blocks()) {
      status = INSTALL_ERROR;
    }
  } else if (!just_exit) {
    // If this is an eng or userdebug build, automatically turn on the text display if no command
    // is specified. Note that this should be called before setting the background to avoid
    // flickering the background image.
    if (is_ro_debuggable()) {
      ui->ShowText(true);
    }
    status = INSTALL_NONE;  // No command specified
    ui->SetBackground(RecoveryUI::NO_COMMAND);
  }

  if (status == INSTALL_ERROR || status == INSTALL_CORRUPT) {
    ui->SetBackground(RecoveryUI::ERROR);
    if (!ui->IsTextVisible()) {
      sleep(5);
    }
  }

  Device::BuiltinAction after = shutdown_after ? Device::SHUTDOWN : Device::REBOOT;
  // 1. If the recovery menu is visible, prompt and wait for commands.
  // 2. If the state is INSTALL_NONE, wait for commands. (i.e. In user build, manually reboot into
  //    recovery to sideload a package.)
  // 3. sideload_auto_reboot is an option only available in user-debug build, reboot the device
  //    without waiting.
  // 4. In all other cases, reboot the device. Therefore, normal users will observe the device
  //    reboot after it shows the "error" screen for 5s.
  if ((status == INSTALL_NONE && !sideload_auto_reboot) || ui->IsTextVisible()) {
    Device::BuiltinAction temp = prompt_and_wait(device, status);
    if (temp != Device::NO_ACTION) {
      after = temp;
    }
  }

  // Save logs and clean up before rebooting or shutting down.
  finish_recovery();

  return after;
}
