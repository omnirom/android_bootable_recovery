/*
 * Copyright (C) 2008 The Android Open Source Project
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

#ifndef _BOOTLOADER_MESSAGE_H
#define _BOOTLOADER_MESSAGE_H

#include <stddef.h>

// Spaces used by misc partition are as below:
// 0   - 2K     Bootloader Message
// 2K  - 16K    Used by Vendor's bootloader
// 16K - 64K    Used by uncrypt and recovery to store wipe_package for A/B devices
// Note that these offsets are admitted by bootloader,recovery and uncrypt, so they
// are not configurable without changing all of them.
static const size_t BOOTLOADER_MESSAGE_OFFSET_IN_MISC = 0;
static const size_t WIPE_PACKAGE_OFFSET_IN_MISC = 16 * 1024;

/* Bootloader Message
 *
 * This structure describes the content of a block in flash
 * that is used for recovery and the bootloader to talk to
 * each other.
 *
 * The command field is updated by linux when it wants to
 * reboot into recovery or to update radio or bootloader firmware.
 * It is also updated by the bootloader when firmware update
 * is complete (to boot into recovery for any final cleanup)
 *
 * The status field is written by the bootloader after the
 * completion of an "update-radio" or "update-hboot" command.
 *
 * The recovery field is only written by linux and used
 * for the system to send a message to recovery or the
 * other way around.
 *
 * The stage field is written by packages which restart themselves
 * multiple times, so that the UI can reflect which invocation of the
 * package it is.  If the value is of the format "#/#" (eg, "1/3"),
 * the UI will add a simple indicator of that status.
 *
 * The slot_suffix field is used for A/B implementations where the
 * bootloader does not set the androidboot.ro.boot.slot_suffix kernel
 * commandline parameter. This is used by fs_mgr to mount /system and
 * other partitions with the slotselect flag set in fstab. A/B
 * implementations are free to use all 32 bytes and may store private
 * data past the first NUL-byte in this field.
 */
struct bootloader_message {
    char command[32];
    char status[32];
    char recovery[768];

    // The 'recovery' field used to be 1024 bytes.  It has only ever
    // been used to store the recovery command line, so 768 bytes
    // should be plenty.  We carve off the last 256 bytes to store the
    // stage string (for multistage packages) and possible future
    // expansion.
    char stage[32];
    char slot_suffix[32];
    char reserved[192];
};

#ifdef __cplusplus

#include <string>
#include <vector>

bool read_bootloader_message(bootloader_message* boot, std::string* err);
bool write_bootloader_message(const bootloader_message& boot, std::string* err);
bool write_bootloader_message(const std::vector<std::string>& options, std::string* err);
bool clear_bootloader_message(std::string* err);

bool read_wipe_package(std::string* package_data, size_t size, std::string* err);
bool write_wipe_package(const std::string& package_data, std::string* err);


#else

#include <stdbool.h>

// C Interface.
bool write_bootloader_message(const char* options);

#endif  // ifdef __cplusplus

#endif  // _BOOTLOADER_MESSAGE_H
