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

#include "device.h"

#include <android-base/logging.h>
#include <android-base/macros.h>

#include "ui.h"

// clang-format off
static constexpr const char* kItems[]{
  "Reboot system now",
  "Reboot to bootloader",
  "Apply update from ADB",
  "Apply update from SD card",
  "Wipe data/factory reset",
#ifndef AB_OTA_UPDATER
  "Wipe cache partition",
#endif  // !AB_OTA_UPDATER
  "Mount /system",
  "View recovery logs",
  "Run graphics test",
  "Run locale test",
  "Power off",
};
// clang-format on

// clang-format off
static constexpr Device::BuiltinAction kMenuActions[] {
  Device::REBOOT,
  Device::REBOOT_BOOTLOADER,
  Device::APPLY_ADB_SIDELOAD,
  Device::APPLY_SDCARD,
  Device::WIPE_DATA,
#ifndef AB_OTA_UPDATER
  Device::WIPE_CACHE,
#endif  // !AB_OTA_UPDATER
  Device::MOUNT_SYSTEM,
  Device::VIEW_RECOVERY_LOGS,
  Device::RUN_GRAPHICS_TEST,
  Device::RUN_LOCALE_TEST,
  Device::SHUTDOWN,
};
// clang-format on

static_assert(arraysize(kItems) == arraysize(kMenuActions),
              "kItems and kMenuActions should have the same length.");

static const std::vector<std::string> kMenuItems(kItems, kItems + arraysize(kItems));

const std::vector<std::string>& Device::GetMenuItems() {
  return kMenuItems;
}

Device::BuiltinAction Device::InvokeMenuItem(size_t menu_position) {
  // CHECK_LT(menu_position, );
  return kMenuActions[menu_position];
}

int Device::HandleMenuKey(int key, bool visible) {
  if (!visible) {
    return kNoAction;
  }

  switch (key) {
    case KEY_DOWN:
    case KEY_VOLUMEDOWN:
      return kHighlightDown;

    case KEY_UP:
    case KEY_VOLUMEUP:
      return kHighlightUp;

    case KEY_ENTER:
    case KEY_POWER:
      return kInvokeItem;

    default:
      // If you have all of the above buttons, any other buttons
      // are ignored. Otherwise, any button cycles the highlight.
      return ui_->HasThreeButtons() ? kNoAction : kHighlightDown;
  }
}
