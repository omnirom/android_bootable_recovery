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
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/properties.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#include <cutils/properties.h> /* for property_list */
#include <fs_mgr/roots.h>
#include <ziparchive/zip_archive.h>

#include "bootloader_message/bootloader_message.h"
#include "install/adb_install.h"
#include "install/fuse_install.h"
#include "install/install.h"
#include "install/package.h"
#include "install/snapshot_utils.h"
#include "install/wipe_data.h"
#include "install/wipe_device.h"
#include "otautil/boot_state.h"
#include "otautil/error_code.h"
#include "otautil/paths.h"
#include "otautil/sysutil.h"
#include "recovery_ui/screen_ui.h"
#include "recovery_ui/ui.h"
#include "recovery_utils/battery_utils.h"
#include "recovery_utils/logging.h"
#include "recovery_utils/roots.h"

static constexpr const char* COMMAND_FILE = "/cache/recovery/command";
static constexpr const char* LAST_KMSG_FILE = "/cache/recovery/last_kmsg";
static constexpr const char* LAST_LOG_FILE = "/cache/recovery/last_log";
static constexpr const char* LOCALE_FILE = "/cache/recovery/last_locale";

static constexpr const char* CACHE_ROOT = "/cache";

static bool save_current_log = false;

/*
 * The recovery tool communicates with the main system through /cache files.
 *   /cache/recovery/command - INPUT - command line for tool, one arg per line
 *   /cache/recovery/log - OUTPUT - combined log file from recovery run(s)
 *
 * The arguments which may be supplied in the recovery.command file:
 *   --update_package=path - verify install an OTA package file
 *   --install_with_fuse - install the update package with FUSE. This allows installation of large
 *       packages on LP32 builds. Since the mmap will otherwise fail due to out of memory.
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
 * 7. FinishRecovery() erases BCB
 *    -- after this, rebooting will restart the main system --
 * 8. main() calls reboot() to boot main system
 *
 * OTA INSTALL
 * 1. main system downloads OTA package to /cache/some-filename.zip
 * 2. main system writes "--update_package=/cache/some-filename.zip"
 * 3. main system reboots into recovery
 * 4. get_args() writes BCB with "boot-recovery" and "--update_package=..."
 *    -- after this, rebooting will attempt to reinstall the update --
 * 5. InstallPackage() attempts to install the update
 *    NOTE: the package install must itself be restartable from any point
 * 6. FinishRecovery() erases BCB
 *    -- after this, rebooting will (try to) restart the main system --
 * 7. ** if install failed **
 *    7a. PromptAndWait() shows an error icon and waits for the user
 *    7b. the user reboots (pulling the battery, etc) into the main system
 */

static bool IsRoDebuggable() {
  return android::base::GetBoolProperty("ro.debuggable", false);
}

// Clear the recovery command and prepare to boot a (hopefully working) system,
// copy our log file to cache as well (for the system to read). This function is
// idempotent: call it as many times as you like.
static void FinishRecovery(RecoveryUI* ui) {
  std::string locale = ui->GetLocale();
  // Save the locale to cache, so if recovery is next started up without a '--locale' argument
  // (e.g., directly from the bootloader) it will use the last-known locale.
  if (!locale.empty() && HasCache()) {
    LOG(INFO) << "Saving locale \"" << locale << "\"";
    if (ensure_path_mounted(LOCALE_FILE) != 0) {
      LOG(ERROR) << "Failed to mount " << LOCALE_FILE;
    } else if (!android::base::WriteStringToFile(locale, LOCALE_FILE)) {
      PLOG(ERROR) << "Failed to save locale to " << LOCALE_FILE;
    }
  }

  copy_logs(save_current_log);

  // Reset to normal system boot so recovery won't cycle indefinitely.
  std::string err;
  if (!clear_bootloader_message(&err)) {
    LOG(ERROR) << "Failed to clear BCB message: " << err;
  }

  // Remove the command file, so recovery won't repeat indefinitely.
  if (HasCache()) {
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

  size_t chosen_item = device->GetUI()->ShowMenu(
      headers, items, 0, true,
      std::bind(&Device::HandleMenuKey, device, std::placeholders::_1, std::placeholders::_2));
  return (chosen_item == 1);
}

static bool ask_to_wipe_data(Device* device) {
  std::vector<std::string> headers{ "Wipe all user data?", "  THIS CAN NOT BE UNDONE!" };
  std::vector<std::string> items{ " Cancel", " Factory data reset" };

  size_t chosen_item = device->GetUI()->ShowPromptWipeDataConfirmationMenu(
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
    size_t chosen_item = device->GetUI()->ShowPromptWipeDataMenu(
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
      CHECK(device->GetReason().has_value());
      bool convert_fbe = device->GetReason().value() == "convert_fbe";
      if (WipeData(device, convert_fbe)) {
        return INSTALL_SUCCESS;
      } else {
        return INSTALL_ERROR;
      }
    }
  }
}

static void choose_recovery_file(Device* device) {
  std::vector<std::string> entries;
  if (HasCache()) {
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
    chosen_item = device->GetUI()->ShowMenu(
        headers, entries, chosen_item, true,
        std::bind(&Device::HandleMenuKey, device, std::placeholders::_1, std::placeholders::_2));

    // Handle WaitKey() interrupt.
    if (chosen_item == static_cast<size_t>(RecoveryUI::KeyError::INTERRUPTED)) {
      break;
    }
    if (entries[chosen_item] == "Back") break;

    device->GetUI()->ShowFile(entries[chosen_item]);
  }
}

static void run_graphics_test(RecoveryUI* ui) {
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

static void WriteUpdateInProgress() {
  std::string err;
  if (!update_bootloader_message({ "--reason=update_in_progress" }, &err)) {
    LOG(ERROR) << "Failed to WriteUpdateInProgress: " << err;
  }
}

static bool AskToReboot(Device* device, Device::BuiltinAction chosen_action) {
  bool is_non_ab = android::base::GetProperty("ro.boot.slot_suffix", "").empty();
  bool is_virtual_ab = android::base::GetBoolProperty("ro.virtual_ab.enabled", false);
  if (!is_non_ab && !is_virtual_ab) {
    // Only prompt for non-A/B or Virtual A/B devices.
    return true;
  }

  std::string header_text;
  std::string item_text;
  switch (chosen_action) {
    case Device::REBOOT:
      header_text = "reboot";
      item_text = " Reboot system now";
      break;
    case Device::SHUTDOWN:
      header_text = "power off";
      item_text = " Power off";
      break;
    default:
      LOG(FATAL) << "Invalid chosen action " << chosen_action;
      break;
  }

  std::vector<std::string> headers{ "WARNING: Previous installation has failed.",
                                    "  Your device may fail to boot if you " + header_text +
                                        " now.",
                                    "  Confirm reboot?" };
  std::vector<std::string> items{ " Cancel", item_text };

  size_t chosen_item = device->GetUI()->ShowMenu(
      headers, items, 0, true /* menu_only */,
      std::bind(&Device::HandleMenuKey, device, std::placeholders::_1, std::placeholders::_2));

  return (chosen_item == 1);
}

// Shows the recovery UI and waits for user input. Returns one of the device builtin actions, such
// as REBOOT, SHUTDOWN, or REBOOT_BOOTLOADER. Returning NO_ACTION means to take the default, which
// is to reboot or shutdown depending on if the --shutdown_after flag was passed to recovery.
static Device::BuiltinAction PromptAndWait(Device* device, InstallResult status) {
  auto ui = device->GetUI();
  bool update_in_progress = (device->GetReason().value_or("") == "update_in_progress");
  for (;;) {
    FinishRecovery(ui);
    switch (status) {
      case INSTALL_SUCCESS:
      case INSTALL_NONE:
      case INSTALL_SKIPPED:
      case INSTALL_RETRY:
      case INSTALL_KEY_INTERRUPTED:
        ui->SetBackground(RecoveryUI::NO_COMMAND);
        break;

      case INSTALL_ERROR:
      case INSTALL_CORRUPT:
        ui->SetBackground(RecoveryUI::ERROR);
        break;

      case INSTALL_REBOOT:
        // All the reboots should have been handled prior to entering PromptAndWait() or immediately
        // after installing a package.
        LOG(FATAL) << "Invalid status code of INSTALL_REBOOT";
        break;
    }
    ui->SetProgressType(RecoveryUI::EMPTY);

    std::vector<std::string> headers;
    if (update_in_progress) {
      headers = { "WARNING: Previous installation has failed.",
                  "  Your device may fail to boot if you reboot or power off now." };
    }

    size_t chosen_item = ui->ShowMenu(
        headers, device->GetMenuItems(), 0, false,
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
      case Device::REBOOT_FROM_FASTBOOT:    // Can not happen
      case Device::SHUTDOWN_FROM_FASTBOOT:  // Can not happen
      case Device::NO_ACTION:
        break;

      case Device::ENTER_FASTBOOT:
      case Device::ENTER_RECOVERY:
      case Device::REBOOT_BOOTLOADER:
      case Device::REBOOT_FASTBOOT:
      case Device::REBOOT_RECOVERY:
      case Device::REBOOT_RESCUE:
        return chosen_action;

      case Device::REBOOT:
      case Device::SHUTDOWN:
        if (!ui->IsTextVisible()) {
          return Device::REBOOT;
        }
        // okay to reboot; no need to ask.
        if (!update_in_progress) {
          return Device::REBOOT;
        }
        // An update might have been failed. Ask if user really wants to reboot.
        if (AskToReboot(device, chosen_action)) {
          return Device::REBOOT;
        }
        break;

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
      case Device::APPLY_SDCARD:
      case Device::ENTER_RESCUE: {
        save_current_log = true;

        update_in_progress = true;
        WriteUpdateInProgress();

        bool adb = true;
        Device::BuiltinAction reboot_action;
        if (chosen_action == Device::ENTER_RESCUE) {
          // Switch to graphics screen.
          ui->ShowText(false);
          status = ApplyFromAdb(device, true /* rescue_mode */, &reboot_action);
        } else if (chosen_action == Device::APPLY_ADB_SIDELOAD) {
          status = ApplyFromAdb(device, false /* rescue_mode */, &reboot_action);
        } else {
          adb = false;
          status = ApplyFromSdcard(device);
        }

        ui->Print("\nInstall from %s completed with status %d.\n", adb ? "ADB" : "SD card", status);
        if (status == INSTALL_REBOOT) {
          return reboot_action;
        }

        if (status == INSTALL_SUCCESS) {
          update_in_progress = false;
          if (!ui->IsTextVisible()) {
            return Device::NO_ACTION;  // reboot if logs aren't visible
          }
        } else {
          ui->SetBackground(RecoveryUI::ERROR);
          ui->Print("Installation aborted.\n");
          copy_logs(save_current_log);
        }
        break;
      }

      case Device::VIEW_RECOVERY_LOGS:
        choose_recovery_file(device);
        break;

      case Device::RUN_GRAPHICS_TEST:
        run_graphics_test(ui);
        break;

      case Device::RUN_LOCALE_TEST: {
        ScreenRecoveryUI* screen_ui = static_cast<ScreenRecoveryUI*>(ui);
        screen_ui->CheckBackgroundTextImages();
        break;
      }

      case Device::MOUNT_SYSTEM:
        // For Virtual A/B, set up the snapshot devices (if exist).
        if (!CreateSnapshotPartitions()) {
          ui->Print("Virtual A/B: snapshot partitions creation failed.\n");
          break;
        }
        if (ensure_path_mounted_at(android::fs_mgr::GetSystemRoot(), "/mnt/system") != -1) {
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

static bool IsBatteryOk(int* required_battery_level) {
  // GmsCore enters recovery mode to install package when having enough battery percentage.
  // Normally, the threshold is 40% without charger and 20% with charger. So we check the battery
  // level against a slightly lower limit.
  constexpr int BATTERY_OK_PERCENTAGE = 20;
  constexpr int BATTERY_WITH_CHARGER_OK_PERCENTAGE = 15;

  auto battery_info = GetBatteryInfo();
  *required_battery_level =
      battery_info.charging ? BATTERY_WITH_CHARGER_OK_PERCENTAGE : BATTERY_OK_PERCENTAGE;
  return battery_info.capacity >= *required_battery_level;
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
    { "install_with_fuse", no_argument, nullptr, 0 },
    { "just_exit", no_argument, nullptr, 'x' },
    { "locale", required_argument, nullptr, 0 },
    { "prompt_and_wipe_data", no_argument, nullptr, 0 },
    { "reason", required_argument, nullptr, 0 },
    { "rescue", no_argument, nullptr, 0 },
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
  bool install_with_fuse = false;  // memory map the update package by default.
  bool should_wipe_data = false;
  bool should_prompt_and_wipe_data = false;
  bool should_wipe_cache = false;
  bool should_wipe_ab = false;
  size_t wipe_package_size = 0;
  bool sideload = false;
  bool sideload_auto_reboot = false;
  bool rescue = false;
  bool just_exit = false;
  bool shutdown_after = false;
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
        if (option == "install_with_fuse") {
          install_with_fuse = true;
        } else if (option == "locale" || option == "fastboot" || option == "reason") {
          // Handled in recovery_main.cpp
        } else if (option == "prompt_and_wipe_data") {
          should_prompt_and_wipe_data = true;
        } else if (option == "rescue") {
          rescue = true;
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

  printf("stage is [%s]\n", device->GetStage().value_or("").c_str());
  printf("reason is [%s]\n", device->GetReason().value_or("").c_str());

  auto ui = device->GetUI();

  // Set background string to "installing security update" for security update,
  // otherwise set it to "installing system update".
  ui->SetSystemUpdateText(security_update);

  int st_cur, st_max;
  if (!device->GetStage().has_value() &&
      sscanf(device->GetStage().value().c_str(), "%d/%d", &st_cur, &st_max) == 2) {
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

  InstallResult status = INSTALL_SUCCESS;
  // next_action indicates the next target to reboot into upon finishing the install. It could be
  // overridden to a different reboot target per user request.
  Device::BuiltinAction next_action = shutdown_after ? Device::SHUTDOWN : Device::REBOOT;

  if (update_package != nullptr) {
    // It's not entirely true that we will modify the flash. But we want
    // to log the update attempt since update_package is non-NULL.
    save_current_log = true;

    if (int required_battery_level; retry_count == 0 && !IsBatteryOk(&required_battery_level)) {
      ui->Print("battery capacity is not enough for installing package: %d%% needed\n",
                required_battery_level);
      // Log the error code to last_install when installation skips due to low battery.
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

      bool should_use_fuse = false;
      if (!SetupPackageMount(update_package, &should_use_fuse)) {
        LOG(INFO) << "Failed to set up the package access, skipping installation";
        status = INSTALL_ERROR;
      } else if (install_with_fuse || should_use_fuse) {
        LOG(INFO) << "Installing package " << update_package << " with fuse";
        status = InstallWithFuseFromPath(update_package, ui);
      } else if (auto memory_package = Package::CreateMemoryPackage(
                     update_package,
                     std::bind(&RecoveryUI::SetProgress, ui, std::placeholders::_1));
                 memory_package != nullptr) {
        status = InstallPackage(memory_package.get(), update_package, should_wipe_cache,
                                retry_count, ui);
      } else {
        // We may fail to memory map the package on 32 bit builds for packages with 2GiB+ size.
        // In such cases, we will try to install the package with fuse. This is not the default
        // installation method because it introduces a layer of indirection from the kernel space.
        LOG(WARNING) << "Failed to memory map package " << update_package
                     << "; falling back to install with fuse";
        status = InstallWithFuseFromPath(update_package, ui);
      }
      if (status != INSTALL_SUCCESS) {
        ui->Print("Installation aborted.\n");

        // When I/O error or bspatch/imgpatch error happens, reboot and retry installation
        // RETRY_LIMIT times before we abandon this OTA update.
        static constexpr int RETRY_LIMIT = 4;
        if (status == INSTALL_RETRY && retry_count < RETRY_LIMIT) {
          copy_logs(save_current_log);
          retry_count += 1;
          set_retry_bootloader_message(retry_count, args);
          // Print retry count on screen.
          ui->Print("Retry attempt %d\n", retry_count);

          // Reboot back into recovery to retry the update.
          Reboot("recovery");
        }
        // If this is an eng or userdebug build, then automatically
        // turn the text display on if the script fails so the error
        // message is visible.
        if (IsRoDebuggable()) {
          ui->ShowText(true);
        }
      }
    }
  } else if (should_wipe_data) {
    save_current_log = true;
    CHECK(device->GetReason().has_value());
    bool convert_fbe = device->GetReason().value() == "convert_fbe";
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
    if (!WipeAbDevice(device, wipe_package_size)) {
      status = INSTALL_ERROR;
    }
  } else if (sideload) {
    // 'adb reboot sideload' acts the same as user presses key combinations to enter the sideload
    // mode. When 'sideload-auto-reboot' is used, text display will NOT be turned on by default. And
    // it will reboot after sideload finishes even if there are errors. This is to enable automated
    // testing.
    save_current_log = true;
    if (!sideload_auto_reboot) {
      ui->ShowText(true);
    }
    status = ApplyFromAdb(device, false /* rescue_mode */, &next_action);
    ui->Print("\nInstall from ADB complete (status: %d).\n", status);
    if (sideload_auto_reboot) {
      status = INSTALL_REBOOT;
      ui->Print("Rebooting automatically.\n");
    }
  } else if (rescue) {
    save_current_log = true;
    status = ApplyFromAdb(device, true /* rescue_mode */, &next_action);
    ui->Print("\nInstall from ADB complete (status: %d).\n", status);
  } else if (!just_exit) {
    // If this is an eng or userdebug build, automatically turn on the text display if no command
    // is specified. Note that this should be called before setting the background to avoid
    // flickering the background image.
    if (IsRoDebuggable()) {
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

  // Determine the next action.
  //  - If the state is INSTALL_REBOOT, device will reboot into the target as specified in
  //    `next_action`.
  //  - If the recovery menu is visible, prompt and wait for commands.
  //  - If the state is INSTALL_NONE, wait for commands (e.g. in user build, one manually boots
  //    into recovery to sideload a package or to wipe the device).
  //  - In all other cases, reboot the device. Therefore, normal users will observe the device
  //    rebooting a) immediately upon successful finish (INSTALL_SUCCESS); or b) an "error" screen
  //    for 5s followed by an automatic reboot.
  if (status != INSTALL_REBOOT) {
    if (status == INSTALL_NONE || ui->IsTextVisible()) {
      auto temp = PromptAndWait(device, status);
      if (temp != Device::NO_ACTION) {
        next_action = temp;
      }
    }
  }

  // Save logs and clean up before rebooting or shutting down.
  FinishRecovery(ui);

  return next_action;
}
