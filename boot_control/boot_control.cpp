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

#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>

#include <string>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/stringprintf.h>
#include <android-base/unique_fd.h>
#include <hardware/boot_control.h>
#include <hardware/hardware.h>

#include <bootloader_message/bootloader_message.h>

struct boot_control_private_t {
  // The base struct needs to be first in the list.
  boot_control_module_t base;

  // Whether this struct was initialized with data from the bootloader message
  // that doesn't change until next reboot.
  bool initialized;

  // The path to the misc_device as reported in the fstab.
  const char* misc_device;

  // The number of slots present on the device.
  unsigned int num_slots;

  // The slot where we are running from.
  unsigned int current_slot;
};

namespace {

// The number of boot attempts that should be made from a new slot before
// rolling back to the previous slot.
constexpr unsigned int kDefaultBootAttempts = 7;
static_assert(kDefaultBootAttempts < 8, "tries_remaining field only has 3 bits");

constexpr unsigned int kMaxNumSlots =
    sizeof(bootloader_control::slot_info) / sizeof(bootloader_control::slot_info[0]);
constexpr const char* kSlotSuffixes[kMaxNumSlots] = { "_a", "_b", "_c", "_d" };
constexpr off_t kBootloaderControlOffset = offsetof(bootloader_message_ab, slot_suffix);

static uint32_t CRC32(const uint8_t* buf, size_t size) {
  static uint32_t crc_table[256];

  // Compute the CRC-32 table only once.
  if (!crc_table[1]) {
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t crc = i;
      for (uint32_t j = 0; j < 8; ++j) {
        uint32_t mask = -(crc & 1);
        crc = (crc >> 1) ^ (0xEDB88320 & mask);
      }
      crc_table[i] = crc;
    }
  }

  uint32_t ret = -1;
  for (size_t i = 0; i < size; ++i) {
    ret = (ret >> 8) ^ crc_table[(ret ^ buf[i]) & 0xFF];
  }

  return ~ret;
}

// Return the little-endian representation of the CRC-32 of the first fields
// in |boot_ctrl| up to the crc32_le field.
uint32_t BootloaderControlLECRC(const bootloader_control* boot_ctrl) {
  return htole32(
      CRC32(reinterpret_cast<const uint8_t*>(boot_ctrl), offsetof(bootloader_control, crc32_le)));
}

bool LoadBootloaderControl(const char* misc_device, bootloader_control* buffer) {
  android::base::unique_fd fd(open(misc_device, O_RDONLY));
  if (fd.get() == -1) {
    PLOG(ERROR) << "failed to open " << misc_device;
    return false;
  }
  if (lseek(fd, kBootloaderControlOffset, SEEK_SET) != kBootloaderControlOffset) {
    PLOG(ERROR) << "failed to lseek " << misc_device;
    return false;
  }
  if (!android::base::ReadFully(fd.get(), buffer, sizeof(bootloader_control))) {
    PLOG(ERROR) << "failed to read " << misc_device;
    return false;
  }
  return true;
}

bool UpdateAndSaveBootloaderControl(const char* misc_device, bootloader_control* buffer) {
  buffer->crc32_le = BootloaderControlLECRC(buffer);
  android::base::unique_fd fd(open(misc_device, O_WRONLY | O_SYNC));
  if (fd.get() == -1) {
    PLOG(ERROR) << "failed to open " << misc_device;
    return false;
  }
  if (lseek(fd.get(), kBootloaderControlOffset, SEEK_SET) != kBootloaderControlOffset) {
    PLOG(ERROR) << "failed to lseek " << misc_device;
    return false;
  }
  if (!android::base::WriteFully(fd.get(), buffer, sizeof(bootloader_control))) {
    PLOG(ERROR) << "failed to write " << misc_device;
    return false;
  }
  return true;
}

void InitDefaultBootloaderControl(const boot_control_private_t* module,
                                  bootloader_control* boot_ctrl) {
  memset(boot_ctrl, 0, sizeof(*boot_ctrl));

  if (module->current_slot < kMaxNumSlots) {
    strlcpy(boot_ctrl->slot_suffix, kSlotSuffixes[module->current_slot],
            sizeof(boot_ctrl->slot_suffix));
  }
  boot_ctrl->magic = BOOT_CTRL_MAGIC;
  boot_ctrl->version = BOOT_CTRL_VERSION;

  // Figure out the number of slots by checking if the partitions exist,
  // otherwise assume the maximum supported by the header.
  boot_ctrl->nb_slot = kMaxNumSlots;
  std::string base_path = module->misc_device;
  size_t last_path_sep = base_path.rfind('/');
  if (last_path_sep != std::string::npos) {
    // We test the existence of the "boot" partition on each possible slot,
    // which is a partition required by Android Bootloader Requirements.
    base_path = base_path.substr(0, last_path_sep + 1) + "boot";
    int last_existing_slot = -1;
    int first_missing_slot = -1;
    for (unsigned int slot = 0; slot < kMaxNumSlots; ++slot) {
      std::string partition_path = base_path + kSlotSuffixes[slot];
      struct stat part_stat;
      int err = stat(partition_path.c_str(), &part_stat);
      if (!err) {
        last_existing_slot = slot;
        LOG(INFO) << "Found slot: " << kSlotSuffixes[slot];
      } else if (err < 0 && errno == ENOENT && first_missing_slot == -1) {
        first_missing_slot = slot;
      }
    }
    // We only declare that we found the actual number of slots if we found all
    // the boot partitions up to the number of slots, and no boot partition
    // after that. Not finding any of the boot partitions implies a problem so
    // we just leave the number of slots in the maximum value.
    if ((last_existing_slot != -1 && last_existing_slot + 1 == first_missing_slot) ||
        (first_missing_slot == -1 && last_existing_slot + 1 == kMaxNumSlots)) {
      boot_ctrl->nb_slot = last_existing_slot + 1;
      LOG(INFO) << "Found a system with " << last_existing_slot + 1 << " slots.";
    }
  }

  for (unsigned int slot = 0; slot < kMaxNumSlots; ++slot) {
    slot_metadata entry = {};

    if (slot < boot_ctrl->nb_slot) {
      entry.priority = 7;
      entry.tries_remaining = kDefaultBootAttempts;
      entry.successful_boot = 0;
    } else {
      entry.priority = 0;  // Unbootable
    }

    // When the boot_control stored on disk is invalid, we assume that the
    // current slot is successful. The bootloader should repair this situation
    // before booting and write a valid boot_control slot, so if we reach this
    // stage it means that the misc partition was corrupted since boot.
    if (module->current_slot == slot) {
      entry.successful_boot = 1;
    }

    boot_ctrl->slot_info[slot] = entry;
  }
  boot_ctrl->recovery_tries_remaining = 0;

  boot_ctrl->crc32_le = BootloaderControlLECRC(boot_ctrl);
}

// Return the index of the slot suffix passed or -1 if not a valid slot suffix.
int SlotSuffixToIndex(const char* suffix) {
  for (unsigned int slot = 0; slot < kMaxNumSlots; ++slot) {
    if (!strcmp(kSlotSuffixes[slot], suffix)) return slot;
  }
  return -1;
}

// Initialize the boot_control_private struct with the information from
// the bootloader_message buffer stored in |boot_ctrl|. Returns whether the
// initialization succeeded.
bool BootControl_lazyInitialization(boot_control_private_t* module) {
  if (module->initialized) return true;

  // Initialize the current_slot from the read-only property. If the property
  // was not set (from either the command line or the device tree), we can later
  // initialize it from the bootloader_control struct.
  std::string suffix_prop = android::base::GetProperty("ro.boot.slot_suffix", "");
  module->current_slot = SlotSuffixToIndex(suffix_prop.c_str());

  std::string err;
  std::string device = get_bootloader_message_blk_device(&err);
  if (device.empty()) return false;

  bootloader_control boot_ctrl;
  if (!LoadBootloaderControl(device.c_str(), &boot_ctrl)) return false;

  // Note that since there isn't a module unload function this memory is leaked.
  module->misc_device = strdup(device.c_str());
  module->initialized = true;

  // Validate the loaded data, otherwise we will destroy it and re-initialize it
  // with the current information.
  uint32_t computed_crc32 = BootloaderControlLECRC(&boot_ctrl);
  if (boot_ctrl.crc32_le != computed_crc32) {
    LOG(WARNING) << "Invalid boot control found, expected CRC-32 0x" << std::hex << computed_crc32
                 << " but found 0x" << std::hex << boot_ctrl.crc32_le << ". Re-initializing.";
    InitDefaultBootloaderControl(module, &boot_ctrl);
    UpdateAndSaveBootloaderControl(device.c_str(), &boot_ctrl);
  }

  module->num_slots = boot_ctrl.nb_slot;
  return true;
}

void BootControl_init(boot_control_module_t* module) {
  BootControl_lazyInitialization(reinterpret_cast<boot_control_private_t*>(module));
}

unsigned int BootControl_getNumberSlots(boot_control_module_t* module) {
  return reinterpret_cast<boot_control_private_t*>(module)->num_slots;
}

unsigned int BootControl_getCurrentSlot(boot_control_module_t* module) {
  return reinterpret_cast<boot_control_private_t*>(module)->current_slot;
}

int BootControl_markBootSuccessful(boot_control_module_t* module) {
  boot_control_private_t* const bootctrl_module = reinterpret_cast<boot_control_private_t*>(module);

  bootloader_control bootctrl;
  if (!LoadBootloaderControl(bootctrl_module->misc_device, &bootctrl)) return -1;

  bootctrl.slot_info[bootctrl_module->current_slot].successful_boot = 1;
  // tries_remaining == 0 means that the slot is not bootable anymore, make
  // sure we mark the current slot as bootable if it succeeds in the last
  // attempt.
  bootctrl.slot_info[bootctrl_module->current_slot].tries_remaining = 1;
  if (!UpdateAndSaveBootloaderControl(bootctrl_module->misc_device, &bootctrl)) return -1;
  return 0;
}

int BootControl_setActiveBootSlot(boot_control_module_t* module, unsigned int slot) {
  boot_control_private_t* const bootctrl_module = reinterpret_cast<boot_control_private_t*>(module);

  if (slot >= kMaxNumSlots || slot >= bootctrl_module->num_slots) {
    // Invalid slot number.
    return -1;
  }

  bootloader_control bootctrl;
  if (!LoadBootloaderControl(bootctrl_module->misc_device, &bootctrl)) return -1;

  // Set every other slot with a lower priority than the new "active" slot.
  const unsigned int kActivePriority = 15;
  const unsigned int kActiveTries = 6;
  for (unsigned int i = 0; i < bootctrl_module->num_slots; ++i) {
    if (i != slot) {
      if (bootctrl.slot_info[i].priority >= kActivePriority)
        bootctrl.slot_info[i].priority = kActivePriority - 1;
    }
  }

  // Note that setting a slot as active doesn't change the successful bit.
  // The successful bit will only be changed by setSlotAsUnbootable().
  bootctrl.slot_info[slot].priority = kActivePriority;
  bootctrl.slot_info[slot].tries_remaining = kActiveTries;

  // Setting the current slot as active is a way to revert the operation that
  // set *another* slot as active at the end of an updater. This is commonly
  // used to cancel the pending update. We should only reset the verity_corrpted
  // bit when attempting a new slot, otherwise the verity bit on the current
  // slot would be flip.
  if (slot != bootctrl_module->current_slot) bootctrl.slot_info[slot].verity_corrupted = 0;

  if (!UpdateAndSaveBootloaderControl(bootctrl_module->misc_device, &bootctrl)) return -1;
  return 0;
}

int BootControl_setSlotAsUnbootable(struct boot_control_module* module, unsigned int slot) {
  boot_control_private_t* const bootctrl_module = reinterpret_cast<boot_control_private_t*>(module);

  if (slot >= kMaxNumSlots || slot >= bootctrl_module->num_slots) {
    // Invalid slot number.
    return -1;
  }

  bootloader_control bootctrl;
  if (!LoadBootloaderControl(bootctrl_module->misc_device, &bootctrl)) return -1;

  // The only way to mark a slot as unbootable, regardless of the priority is to
  // set the tries_remaining to 0.
  bootctrl.slot_info[slot].successful_boot = 0;
  bootctrl.slot_info[slot].tries_remaining = 0;
  if (!UpdateAndSaveBootloaderControl(bootctrl_module->misc_device, &bootctrl)) return -1;
  return 0;
}

int BootControl_isSlotBootable(struct boot_control_module* module, unsigned int slot) {
  boot_control_private_t* const bootctrl_module = reinterpret_cast<boot_control_private_t*>(module);

  if (slot >= kMaxNumSlots || slot >= bootctrl_module->num_slots) {
    // Invalid slot number.
    return -1;
  }

  bootloader_control bootctrl;
  if (!LoadBootloaderControl(bootctrl_module->misc_device, &bootctrl)) return -1;

  return bootctrl.slot_info[slot].tries_remaining;
}

int BootControl_isSlotMarkedSuccessful(struct boot_control_module* module, unsigned int slot) {
  boot_control_private_t* const bootctrl_module = reinterpret_cast<boot_control_private_t*>(module);

  if (slot >= kMaxNumSlots || slot >= bootctrl_module->num_slots) {
    // Invalid slot number.
    return -1;
  }

  bootloader_control bootctrl;
  if (!LoadBootloaderControl(bootctrl_module->misc_device, &bootctrl)) return -1;

  return bootctrl.slot_info[slot].successful_boot && bootctrl.slot_info[slot].tries_remaining;
}

const char* BootControl_getSuffix(boot_control_module_t* module, unsigned int slot) {
  if (slot >= kMaxNumSlots || slot >= reinterpret_cast<boot_control_private_t*>(module)->num_slots) {
    return NULL;
  }
  return kSlotSuffixes[slot];
}

static int BootControl_open(const hw_module_t* module __unused, const char* id __unused,
                            hw_device_t** device __unused) {
  /* Nothing to do currently. */
  return 0;
}

struct hw_module_methods_t BootControl_methods = {
  .open = BootControl_open,
};

}  // namespace

boot_control_private_t HAL_MODULE_INFO_SYM = {
  .base =
      {
          .common =
              {
                  .tag = HARDWARE_MODULE_TAG,
                  .module_api_version = BOOT_CONTROL_MODULE_API_VERSION_0_1,
                  .hal_api_version = HARDWARE_HAL_API_VERSION,
                  .id = BOOT_CONTROL_HARDWARE_MODULE_ID,
                  .name = "AOSP reference bootctrl HAL",
                  .author = "The Android Open Source Project",
                  .methods = &BootControl_methods,
              },
          .init = BootControl_init,
          .getNumberSlots = BootControl_getNumberSlots,
          .getCurrentSlot = BootControl_getCurrentSlot,
          .markBootSuccessful = BootControl_markBootSuccessful,
          .setActiveBootSlot = BootControl_setActiveBootSlot,
          .setSlotAsUnbootable = BootControl_setSlotAsUnbootable,
          .isSlotBootable = BootControl_isSlotBootable,
          .getSuffix = BootControl_getSuffix,
          .isSlotMarkedSuccessful = BootControl_isSlotMarkedSuccessful,
      },
  .initialized = false,
  .misc_device = nullptr,
  .num_slots = 0,
  .current_slot = 0,
};
