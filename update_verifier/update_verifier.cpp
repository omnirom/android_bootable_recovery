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

/*
 * This program verifies the integrity of the partitions after an A/B OTA
 * update. It gets invoked by init, and will only perform the verification if
 * it's the first boot post an A/B OTA update.
 *
 * It relies on dm-verity to capture any corruption on the partitions being
 * verified. dm-verity must be in enforcing mode, so that it will reboot the
 * device on dm-verity failures. When that happens, the bootloader should
 * mark the slot as unbootable and stops trying. We should never see a device
 * started in dm-verity logging mode but with isSlotMarkedSuccessful equals to
 * 0.
 *
 * The current slot will be marked as having booted successfully if the
 * verifier reaches the end after the verification.
 *
 * TODO: The actual verification part will be added later after we have the
 * A/B OTA package format in place.
 */

#include <string.h>

#include <hardware/boot_control.h>

#define LOG_TAG       "update_verifier"
#include <log/log.h>

int main(int argc, char** argv) {
  for (int i = 1; i < argc; i++) {
    SLOGI("Started with arg %d: %s\n", i, argv[i]);
  }

  const hw_module_t* hw_module;
  if (hw_get_module("bootctrl", &hw_module) != 0) {
    SLOGE("Error getting bootctrl module.\n");
    return -1;
  }

  boot_control_module_t* module = reinterpret_cast<boot_control_module_t*>(
      const_cast<hw_module_t*>(hw_module));
  module->init(module);

  unsigned current_slot = module->getCurrentSlot(module);
  int is_successful= module->isSlotMarkedSuccessful(module, current_slot);
  SLOGI("Booting slot %u: isSlotMarkedSuccessful=%d\n", current_slot, is_successful);

  if (is_successful == 0) {
    // The current slot has not booted successfully.

    // TODO: Add the actual verification after we have the A/B OTA package
    // format in place.

    // TODO: Assert the dm-verity mode. Bootloader should never boot a newly
    // flashed slot (isSlotMarkedSuccessful == 0) with dm-verity logging mode.

    int ret = module->markBootSuccessful(module);
    if (ret != 0) {
      SLOGE("Error marking booted successfully: %s\n", strerror(-ret));
      return -1;
    }
    SLOGI("Marked slot %u as booted successfully.\n", current_slot);
  }

  SLOGI("Leaving update_verifier.\n");
  return 0;
}
