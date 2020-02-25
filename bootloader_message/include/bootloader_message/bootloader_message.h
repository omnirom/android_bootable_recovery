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

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

// Spaces used by misc partition are as below:
// 0   - 2K     For bootloader_message
// 2K  - 16K    Used by Vendor's bootloader (the 2K - 4K range may be optionally used
//              as bootloader_message_ab struct)
// 16K - 32K    Used by uncrypt and recovery to store wipe_package for A/B devices
// 32K - 64K    System space, used for miscellanious AOSP features. See below.
// Note that these offsets are admitted by bootloader,recovery and uncrypt, so they
// are not configurable without changing all of them.
constexpr size_t BOOTLOADER_MESSAGE_OFFSET_IN_MISC = 0;
constexpr size_t VENDOR_SPACE_OFFSET_IN_MISC = 2 * 1024;
constexpr size_t WIPE_PACKAGE_OFFSET_IN_MISC = 16 * 1024;
constexpr size_t SYSTEM_SPACE_OFFSET_IN_MISC = 32 * 1024;
constexpr size_t SYSTEM_SPACE_SIZE_IN_MISC = 32 * 1024;

/* Bootloader Message (2-KiB)
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
 * The status field was used by the bootloader after the completion
 * of an "update-radio" or "update-hboot" command, which has been
 * deprecated since Froyo.
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
 * We used to have slot_suffix field for A/B boot control metadata in
 * this struct, which gets unintentionally cleared by recovery or
 * uncrypt. Move it into struct bootloader_message_ab to avoid the
 * issue.
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

    // The 'reserved' field used to be 224 bytes when it was initially
    // carved off from the 1024-byte recovery field. Bump it up to
    // 1184-byte so that the entire bootloader_message struct rounds up
    // to 2048-byte.
    char reserved[1184];
};

// Holds Virtual A/B merge status information. Current version is 1. New fields
// must be added to the end.
struct misc_virtual_ab_message {
  uint8_t version;
  uint32_t magic;
  uint8_t merge_status;  // IBootControl 1.1, MergeStatus enum.
  uint8_t source_slot;   // Slot number when merge_status was written.
  uint8_t reserved[57];
} __attribute__((packed));

#define MISC_VIRTUAL_AB_MESSAGE_VERSION 2
#define MISC_VIRTUAL_AB_MAGIC_HEADER 0x56740AB0

#if (__STDC_VERSION__ >= 201112L) || defined(__cplusplus)
static_assert(sizeof(struct misc_virtual_ab_message) == 64,
              "struct misc_virtual_ab_message has wrong size");
#endif

// This struct is not meant to be used directly, rather, it is to make
// computation of offsets easier. New fields must be added to the end.
struct misc_system_space_layout {
  misc_virtual_ab_message virtual_ab_message;
} __attribute__((packed));

#ifdef __cplusplus

#include <string>
#include <vector>

// Gets the block device name of /misc partition.
std::string get_misc_blk_device(std::string* err);
// Return the block device name for the bootloader message partition and waits
// for the device for up to 10 seconds. In case of error returns the empty
// string.
std::string get_bootloader_message_blk_device(std::string* err);

// Writes |size| bytes of data from buffer |p| to |misc_blk_device| at |offset|. If the write fails,
// sets the error message in |err|.
bool write_misc_partition(const void* p, size_t size, const std::string& misc_blk_device,
                          size_t offset, std::string* err);

// Read bootloader message into boot. Error message will be set in err.
bool read_bootloader_message(bootloader_message* boot, std::string* err);

// Read bootloader message from the specified misc device into boot.
bool read_bootloader_message_from(bootloader_message* boot, const std::string& misc_blk_device,
                                  std::string* err);

// Write bootloader message to BCB.
bool write_bootloader_message(const bootloader_message& boot, std::string* err);

// Write bootloader message to the specified BCB device.
bool write_bootloader_message_to(const bootloader_message& boot,
                                 const std::string& misc_blk_device, std::string* err);

// Write bootloader message (boots into recovery with the options) to BCB. Will
// set the command and recovery fields, and reset the rest.
bool write_bootloader_message(const std::vector<std::string>& options, std::string* err);

// Write bootloader message (boots into recovery with the options) to the specific BCB device. Will
// set the command and recovery fields, and reset the rest.
bool write_bootloader_message_to(const std::vector<std::string>& options,
                                 const std::string& misc_blk_device, std::string* err);

// Update bootloader message (boots into recovery with the options) to BCB. Will
// only update the command and recovery fields.
bool update_bootloader_message(const std::vector<std::string>& options, std::string* err);

// Update bootloader message (boots into recovery with the |options|) in |boot|. Will only update
// the command and recovery fields.
bool update_bootloader_message_in_struct(bootloader_message* boot,
                                         const std::vector<std::string>& options);

// Clear BCB.
bool clear_bootloader_message(std::string* err);

// Writes the reboot-bootloader reboot reason to the bootloader_message.
bool write_reboot_bootloader(std::string* err);

// Read the wipe package from BCB (from offset WIPE_PACKAGE_OFFSET_IN_MISC).
bool read_wipe_package(std::string* package_data, size_t size, std::string* err);

// Write the wipe package into BCB (to offset WIPE_PACKAGE_OFFSET_IN_MISC).
bool write_wipe_package(const std::string& package_data, std::string* err);

// Read or write the Virtual A/B message from system space in /misc.
bool ReadMiscVirtualAbMessage(misc_virtual_ab_message* message, std::string* err);
bool WriteMiscVirtualAbMessage(const misc_virtual_ab_message& message, std::string* err);

#else

#include <stdbool.h>

// C Interface.
bool write_bootloader_message(const char* options);
bool write_reboot_bootloader(void);

#endif  // ifdef __cplusplus

#endif  // _BOOTLOADER_MESSAGE_H
