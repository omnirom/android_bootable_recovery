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

#ifndef _RECOVERY_FIRMWARE_H
#define _RECOVERY_FIRMWARE_H

/* Save a radio or bootloader update image for later installation.
 * The type should be one of "hboot" or "radio".
 * Takes ownership of type and data.  Returns nonzero on error.
 */
int remember_firmware_update(const char *type, const char *data, int length);

/* Returns true if a firmware update has been saved. */
int firmware_update_pending();

/* If an update was saved, reboot into the bootloader now to install it.
 * Returns 0 if no radio image was defined, nonzero on error,
 * doesn't return at all on success...
 */
int maybe_install_firmware_update(const char *send_intent);

#endif
