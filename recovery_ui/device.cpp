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

#include "recovery_ui/device.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <android-base/logging.h>

#include "otautil/boot_state.h"
#include "recovery_ui/ui.h"

static std::vector<std::pair<std::string, Device::BuiltinAction>> g_menu_actions{
  { "Reboot system now", Device::REBOOT },
  { "Reboot to bootloader", Device::REBOOT_BOOTLOADER },
  { "Enter fastboot", Device::ENTER_FASTBOOT },
  { "Apply update from ADB", Device::APPLY_ADB_SIDELOAD },
  { "Apply update from SD card", Device::APPLY_SDCARD },
  { "Wipe data/factory reset", Device::WIPE_DATA },
  { "Wipe cache partition", Device::WIPE_CACHE },
  { "Mount /system", Device::MOUNT_SYSTEM },
  { "View recovery logs", Device::VIEW_RECOVERY_LOGS },
  { "Run graphics test", Device::RUN_GRAPHICS_TEST },
  { "Run locale test", Device::RUN_LOCALE_TEST },
  { "Enter rescue", Device::ENTER_RESCUE },
  { "Power off", Device::SHUTDOWN },
};

static std::vector<std::string> g_menu_items;

static void PopulateMenuItems() {
  g_menu_items.clear();
  std::transform(g_menu_actions.cbegin(), g_menu_actions.cend(), std::back_inserter(g_menu_items),
                 [](const auto& entry) { return entry.first; });
}

Device::Device(RecoveryUI* ui) : ui_(ui) {
  PopulateMenuItems();
}

void Device::RemoveMenuItemForAction(Device::BuiltinAction action) {
  g_menu_actions.erase(
      std::remove_if(g_menu_actions.begin(), g_menu_actions.end(),
                     [action](const auto& entry) { return entry.second == action; }));
  CHECK(!g_menu_actions.empty());

  // Re-populate the menu items.
  PopulateMenuItems();
}

const std::vector<std::string>& Device::GetMenuItems() {
  return g_menu_items;
}

Device::BuiltinAction Device::InvokeMenuItem(size_t menu_position) {
  return g_menu_actions[menu_position].second;
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

void Device::SetBootState(const BootState* state) {
  boot_state_ = state;
}

std::optional<std::string> Device::GetReason() const {
  return boot_state_ ? std::make_optional(boot_state_->reason()) : std::nullopt;
}

std::optional<std::string> Device::GetStage() const {
  return boot_state_ ? std::make_optional(boot_state_->stage()) : std::nullopt;
}
