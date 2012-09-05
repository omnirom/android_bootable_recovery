/*
 * Copyright (C) 2007 The Android Open Source Project
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

#ifndef REBOOT_H_
#define REBOOT_H_

typedef enum
{
    rb_current = 0,
    rb_system, 
    rb_recovery, 
    rb_poweroff, 
    rb_bootloader,     // May also be fastboot
    rb_download,
} RebootCommand;

// tw_isRebootCommandSupported: Return 1 if command is supported, 0 if the command is not supported, -1 on error
int tw_isRebootCommandSupported(RebootCommand command);

// tw_setRebootMode: Set the reboot state (without rebooting). Return 0 on success, -1 on error or unsupported
int tw_setRebootMode(RebootCommand command);

// tw_reboot: Reboot the system. Return -1 on error, no return on success
int tw_reboot(RebootCommand command);

#endif  // REBOOT_H_



