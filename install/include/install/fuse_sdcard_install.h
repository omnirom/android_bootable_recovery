/*
 * Copyright (C) 2019 The Android Open Source Project
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

<<<<<<< HEAD:adb_install.h
#include <sys/types.h>

//class RecoveryUI;

//static void set_usb_driver(bool enabled);
//static void maybe_restart_adbd();
int apply_from_adb(const char* install_file, pid_t* child_pid);
=======
#include "recovery_ui/device.h"
#include "recovery_ui/ui.h"
>>>>>>> android-10.0.0_r25:install/include/install/fuse_sdcard_install.h

int ApplyFromSdcard(Device* device, RecoveryUI* ui);
