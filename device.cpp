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

// TODO: this is a lie for, say, fugu.
static const char* HEADERS[] = {
    "Volume up/down to move highlight.",
    "Power button to select.",
    "",
    NULL
};

static const char* ITEMS[] = {
    "Reboot system now",
    "Reboot to bootloader",
    "Apply update from ADB",
    "Apply update from SD card",
    "Wipe data/factory reset",
    "Wipe cache partition",
    "Mount /system",
    "View recovery logs",
    "Power off",
    NULL
};

const char* const* Device::GetMenuHeaders() { return HEADERS; }
const char* const* Device::GetMenuItems() { return ITEMS; }

Device::BuiltinAction Device::InvokeMenuItem(int menu_position) {
  switch (menu_position) {
    case 0: return REBOOT;
    case 1: return REBOOT_BOOTLOADER;
    case 2: return APPLY_ADB_SIDELOAD;
    case 3: return APPLY_SDCARD;
    case 4: return WIPE_DATA;
    case 5: return WIPE_CACHE;
    case 6: return MOUNT_SYSTEM;
    case 7: return VIEW_RECOVERY_LOGS;
    case 8: return SHUTDOWN;
    default: return NO_ACTION;
  }
}
