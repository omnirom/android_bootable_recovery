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

#include <string>

#include <hardware/boot_control.h>
#include <hardware/hardware.h>

#include <libboot_control/libboot_control.h>

using android::bootable::BootControl;

struct boot_control_private_t {
  // The base struct needs to be first in the list.
  boot_control_module_t base;

  BootControl impl;
};

namespace {

void BootControl_init(boot_control_module_t* module) {
  auto& impl = reinterpret_cast<boot_control_private_t*>(module)->impl;
  impl.Init();
}

unsigned int BootControl_getNumberSlots(boot_control_module_t* module) {
  auto& impl = reinterpret_cast<boot_control_private_t*>(module)->impl;
  return impl.GetNumberSlots();
}

unsigned int BootControl_getCurrentSlot(boot_control_module_t* module) {
  auto& impl = reinterpret_cast<boot_control_private_t*>(module)->impl;
  return impl.GetCurrentSlot();
}

int BootControl_markBootSuccessful(boot_control_module_t* module) {
  auto& impl = reinterpret_cast<boot_control_private_t*>(module)->impl;
  return impl.MarkBootSuccessful() ? 0 : -1;
}

int BootControl_setActiveBootSlot(boot_control_module_t* module, unsigned int slot) {
  auto& impl = reinterpret_cast<boot_control_private_t*>(module)->impl;
  return impl.SetActiveBootSlot(slot) ? 0 : -1;
}

int BootControl_setSlotAsUnbootable(struct boot_control_module* module, unsigned int slot) {
  auto& impl = reinterpret_cast<boot_control_private_t*>(module)->impl;
  return impl.SetSlotAsUnbootable(slot) ? 0 : -1;
}

int BootControl_isSlotBootable(struct boot_control_module* module, unsigned int slot) {
  auto& impl = reinterpret_cast<boot_control_private_t*>(module)->impl;
  return impl.IsSlotBootable(slot) ? 0 : -1;
}

int BootControl_isSlotMarkedSuccessful(struct boot_control_module* module, unsigned int slot) {
  auto& impl = reinterpret_cast<boot_control_private_t*>(module)->impl;
  return impl.IsSlotMarkedSuccessful(slot) ? 0 : -1;
}

const char* BootControl_getSuffix(boot_control_module_t* module, unsigned int slot) {
  auto& impl = reinterpret_cast<boot_control_private_t*>(module)->impl;
  return impl.GetSuffix(slot);
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
};
