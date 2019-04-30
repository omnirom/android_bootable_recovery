/*
 * Copyright (C) 2012 The Android Open Source Project
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

#pragma once

#include "install/install.h"
#include "recovery_ui/device.h"

// Applies a package via `adb sideload` or `adb rescue`. Returns the install result. When a reboot
// has been requested, INSTALL_REBOOT will be the return value, with the reboot target set in
// reboot_action.
InstallResult ApplyFromAdb(Device* device, bool rescue_mode, Device::BuiltinAction* reboot_action);
