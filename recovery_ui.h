/*
 * Copyright (C) 2009 The Android Open Source Project
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

#ifndef _RECOVERY_UI_H
#define _RECOVERY_UI_H

// Called when recovery starts up.  Returns 0.
extern int device_recovery_start();

// Called in the input thread when a new key (key_code) is pressed.
// *key_pressed is an array of KEY_MAX+1 bytes indicating which other
// keys are already pressed.  Return true if the text display should
// be toggled.
extern int device_toggle_display(volatile char* key_pressed, int key_code);

// Called in the input thread when a new key (key_code) is pressed.
// *key_pressed is an array of KEY_MAX+1 bytes indicating which other
// keys are already pressed.  Return true if the device should reboot
// immediately.
extern int device_reboot_now(volatile char* key_pressed, int key_code);

// Called from the main thread when recovery is waiting for input and
// a key is pressed.  key is the code of the key pressed; visible is
// true if the recovery menu is being shown.  Implementations can call
// ui_key_pressed() to discover if other keys are being held down.
// Return one of the defined constants below in order to:
//
//   - move the menu highlight (HIGHLIGHT_*)
//   - invoke the highlighted item (SELECT_ITEM)
//   - do nothing (NO_ACTION)
//   - invoke a specific action (a menu position: any non-negative number)
extern int device_handle_key(int key, int visible);

// Perform a recovery action selected from the menu.  'which' will be
// the item number of the selected menu item, or a non-negative number
// returned from device_handle_key().  The menu will be hidden when
// this is called; implementations can call ui_print() to print
// information to the screen.
extern int device_perform_action(int which);

// Called when we do a wipe data/factory reset operation (either via a
// reboot from the main system with the --wipe_data flag, or when the
// user boots into recovery manually and selects the option from the
// menu.)  Can perform whatever device-specific wiping actions are
// needed.  Return 0 on success.  The userdata and cache partitions
// are erased after this returns (whether it returns success or not).
int device_wipe_data();

#define NO_ACTION           -1

#define HIGHLIGHT_UP        -2
#define HIGHLIGHT_DOWN      -3
#define SELECT_ITEM         -4
#define UP_A_LEVEL          -5
#define HOME_MENU           -6
#define MENU_MENU           -7

// Again, just to keep custom recovery builds happy
#define ITEM_APPLY_CACHE     4

// Header text to display above the main menu.
extern char* MENU_HEADERS[];

// Text of menu items.
extern char* MENU_ITEMS[];

// NOTE: Main Menu index definitions moved to recovery.c

int sdcard_directory(const char* path);
int get_menu_selection(char** headers, char** items, int menu_only, int initial_selection);
void prompt_and_wait();
void finish_recovery(const char *send_intent);
#endif
