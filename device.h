/*
 * Copyright (C) 2011 The Android Open Source Project
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

#ifndef _RECOVERY_DEVICE_H
#define _RECOVERY_DEVICE_H

#include "ui.h"

class Device {
  public:
    virtual ~Device() { }

    // Called to obtain the UI object that should be used to display
    // the recovery user interface for this device.  You should not
    // have called Init() on the UI object already, the caller will do
    // that after this method returns.
    virtual RecoveryUI* GetUI() = 0;

    // Called when recovery starts up (after the UI has been obtained
    // and initialized and after the arguments have been parsed, but
    // before anything else).
    virtual void StartRecovery() { };

    // enum KeyAction { NONE, TOGGLE, REBOOT };

    // // Called in the input thread when a new key (key_code) is
    // // pressed.  *key_pressed is an array of KEY_MAX+1 bytes
    // // indicating which other keys are already pressed.  Return a
    // // KeyAction to indicate action should be taken immediately.
    // // These actions happen when recovery is not waiting for input
    // // (eg, in the midst of installing a package).
    // virtual KeyAction CheckImmediateKeyAction(volatile char* key_pressed, int key_code) = 0;

    // Called from the main thread when recovery is at the main menu
    // and waiting for input, and a key is pressed.  (Note that "at"
    // the main menu does not necessarily mean the menu is visible;
    // recovery will be at the main menu with it invisible after an
    // unsuccessful operation [ie OTA package failure], or if recovery
    // is started with no command.)
    //
    // key is the code of the key just pressed.  (You can call
    // IsKeyPressed() on the RecoveryUI object you returned from GetUI
    // if you want to find out if other keys are held down.)
    //
    // visible is true if the menu is visible.
    //
    // Return one of the defined constants below in order to:
    //
    //   - move the menu highlight (kHighlight{Up,Down})
    //   - invoke the highlighted item (kInvokeItem)
    //   - do nothing (kNoAction)
    //   - invoke a specific action (a menu position: any non-negative number)
    virtual int HandleMenuKey(int key, int visible) = 0;

    enum BuiltinAction { NO_ACTION, REBOOT, APPLY_EXT,
                         APPLY_CACHE,   // APPLY_CACHE is deprecated; has no effect
                         APPLY_ADB_SIDELOAD, WIPE_DATA, WIPE_CACHE,
                         REBOOT_BOOTLOADER, SHUTDOWN, READ_RECOVERY_LASTLOG };

    // Perform a recovery action selected from the menu.
    // 'menu_position' will be the item number of the selected menu
    // item, or a non-negative number returned from
    // device_handle_key().  The menu will be hidden when this is
    // called; implementations can call ui_print() to print
    // information to the screen.  If the menu position is one of the
    // builtin actions, you can just return the corresponding enum
    // value.  If it is an action specific to your device, you
    // actually perform it here and return NO_ACTION.
    virtual BuiltinAction InvokeMenuItem(int menu_position) = 0;

    static const int kNoAction = -1;
    static const int kHighlightUp = -2;
    static const int kHighlightDown = -3;
    static const int kInvokeItem = -4;

    // Called when we do a wipe data/factory reset operation (either via a
    // reboot from the main system with the --wipe_data flag, or when the
    // user boots into recovery manually and selects the option from the
    // menu.)  Can perform whatever device-specific wiping actions are
    // needed.  Return 0 on success.  The userdata and cache partitions
    // are erased AFTER this returns (whether it returns success or not).
    virtual int WipeData() { return 0; }

    // Return the headers (an array of strings, one per line,
    // NULL-terminated) for the main menu.  Typically these tell users
    // what to push to move the selection and invoke the selected
    // item.
    virtual const char* const* GetMenuHeaders() = 0;

    // Return the list of menu items (an array of strings,
    // NULL-terminated).  The menu_position passed to InvokeMenuItem
    // will correspond to the indexes into this array.
    virtual const char* const* GetMenuItems() = 0;
};

// The device-specific library must define this function (or the
// default one will be used, if there is no device-specific library).
// It returns the Device object that recovery should use.
Device* make_device();

#endif  // _DEVICE_H
