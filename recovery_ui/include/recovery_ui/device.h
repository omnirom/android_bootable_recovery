/*
 * Copyright (C) 2011 The Android Open Source Project
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

#ifndef _RECOVERY_DEVICE_H
#define _RECOVERY_DEVICE_H

#include <stddef.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

// Forward declaration to avoid including "ui.h".
class RecoveryUI;

class BootState;

class Device {
 public:
  static constexpr const int kNoAction = -1;
  static constexpr const int kHighlightUp = -2;
  static constexpr const int kHighlightDown = -3;
  static constexpr const int kInvokeItem = -4;

  // ENTER vs REBOOT: The latter will trigger a reboot that goes through bootloader, which allows
  // using a new bootloader / recovery image if applicable. For example, REBOOT_RESCUE goes from
  // rescue -> bootloader -> rescue, whereas ENTER_RESCUE switches from recovery -> rescue
  // directly.
  enum BuiltinAction {
    NO_ACTION = 0,
    REBOOT = 1,
    APPLY_SDCARD = 2,
    // APPLY_CACHE was 3.
    APPLY_ADB_SIDELOAD = 4,
    WIPE_DATA = 5,
    WIPE_CACHE = 6,
    REBOOT_BOOTLOADER = 7,
    SHUTDOWN = 8,
    VIEW_RECOVERY_LOGS = 9,
    MOUNT_SYSTEM = 10,
    RUN_GRAPHICS_TEST = 11,
    RUN_LOCALE_TEST = 12,
    KEY_INTERRUPTED = 13,
    ENTER_FASTBOOT = 14,
    ENTER_RECOVERY = 15,
    ENTER_RESCUE = 16,
    REBOOT_FASTBOOT = 17,
    REBOOT_RECOVERY = 18,
    REBOOT_RESCUE = 19,
    REBOOT_FROM_FASTBOOT = 20,
    SHUTDOWN_FROM_FASTBOOT = 21,
  };

  explicit Device(RecoveryUI* ui);
  virtual ~Device() {}

  // Returns a raw pointer to the RecoveryUI object.
  virtual RecoveryUI* GetUI() {
    return ui_.get();
  }

  // Resets the UI object to the given UI. Used to override the default UI in case initialization
  // failed, or we want a different UI for some reason. The device object will take the ownership.
  virtual void ResetUI(RecoveryUI* ui) {
    ui_.reset(ui);
  }

  // Called before recovery mode started up, to perform whatever device-specific recovery mode
  // preparation as needed.
  virtual void PreRecovery() {}

  // Called when recovery starts up (after the UI has been obtained and initialized and after the
  // arguments have been parsed, but before anything else).
  virtual void StartRecovery() {}

  // Called before fastboot mode is started up, to perform whatever device-specific fastboot mode
  // preparation as needed.
  virtual void PreFastboot() {}

  // Called when fastboot starts up (after the UI has been obtained and initialized and after the
  // arguments have been parsed, but before anything else).
  virtual void StartFastboot() {}

  // Called from the main thread when recovery is at the main menu and waiting for input, and a key
  // is pressed. (Note that "at" the main menu does not necessarily mean the menu is visible;
  // recovery will be at the main menu with it invisible after an unsuccessful operation, such as
  // failed to install an OTA package, or if recovery is started with no command.)
  //
  // 'key' is the code of the key just pressed. (You can call IsKeyPressed() on the RecoveryUI
  // object you returned from GetUI() if you want to find out if other keys are held down.)
  //
  // 'visible' is true if the menu is visible.
  //
  // Returns one of the defined constants below in order to:
  //   - move the menu highlight (kHighlight{Up,Down}: negative value)
  //   - invoke the highlighted item (kInvokeItem: negative value)
  //   - do nothing (kNoAction: negative value)
  //   - invoke a specific action (a menu position: non-negative value)
  virtual int HandleMenuKey(int key, bool visible);

  // Returns the list of menu items (a vector of strings). The menu_position passed to
  // InvokeMenuItem() will correspond to the indexes into this array.
  virtual const std::vector<std::string>& GetMenuItems();

  // Performs a recovery action selected from the menu. 'menu_position' will be the index of the
  // selected menu item, or a non-negative value returned from HandleMenuKey(). The menu will be
  // hidden when this is called; implementations can call GetUI()->Print() to print information to
  // the screen. If the menu position is one of the builtin actions, you can just return the
  // corresponding enum value. If it is an action specific to your device, you actually perform it
  // here and return NO_ACTION.
  virtual BuiltinAction InvokeMenuItem(size_t menu_position);

  // Removes the menu item for the given action. This allows tailoring the menu based on the
  // runtime info, such as the availability of /cache or /sdcard.
  virtual void RemoveMenuItemForAction(Device::BuiltinAction action);

  // Called before and after we do a wipe data/factory reset operation, either via a reboot from the
  // main system with the --wipe_data flag, or when the user boots into recovery image manually and
  // selects the option from the menu, to perform whatever device-specific wiping actions as needed.
  // Returns true on success; returning false from PreWipeData will prevent the regular wipe, and
  // returning false from PostWipeData will cause the wipe to be considered a failure.
  virtual bool PreWipeData() {
    return true;
  }

  virtual bool PostWipeData() {
    return true;
  }

  void SetBootState(const BootState* state);
  // The getters for reason and stage may return std::nullopt until StartRecovery() is called. It's
  // the caller's responsibility to perform the check and handle the exception.
  std::optional<std::string> GetReason() const;
  std::optional<std::string> GetStage() const;

 private:
  // The RecoveryUI object that should be used to display the user interface for this device.
  std::unique_ptr<RecoveryUI> ui_;
  const BootState* boot_state_{ nullptr };
};

// Disable name mangling, as this function will be loaded via dlsym(3).
extern "C" {

// The device-specific library must define this function (or the default one will be used, if there
// is no device-specific library). It returns the Device object that recovery should use.
Device* make_device();
}

#endif  // _DEVICE_H
