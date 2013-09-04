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

#ifndef RECOVERY_UI_H
#define RECOVERY_UI_H

#include <linux/input.h>
#include <pthread.h>
#include <time.h>

// Abstract class for controlling the user interface during recovery.
class RecoveryUI {
  public:
    RecoveryUI();

    virtual ~RecoveryUI() { }

    // Initialize the object; called before anything else.
    virtual void Init();

    // After calling Init(), you can tell the UI what locale it is operating in.
    virtual void SetLocale(const char* locale) { }

    // Set the overall recovery state ("background image").
    enum Icon { NONE, INSTALLING_UPDATE, ERASING, NO_COMMAND, ERROR };
    virtual void SetBackground(Icon icon) = 0;

    // --- progress indicator ---
    enum ProgressType { EMPTY, INDETERMINATE, DETERMINATE };
    virtual void SetProgressType(ProgressType determinate) = 0;

    // Show a progress bar and define the scope of the next operation:
    //   portion - fraction of the progress bar the next operation will use
    //   seconds - expected time interval (progress bar moves at this minimum rate)
    virtual void ShowProgress(float portion, float seconds) = 0;

    // Set progress bar position (0.0 - 1.0 within the scope defined
    // by the last call to ShowProgress).
    virtual void SetProgress(float fraction) = 0;

    // --- text log ---

    virtual void ShowText(bool visible) = 0;

    virtual bool IsTextVisible() = 0;

    virtual bool WasTextEverVisible() = 0;

    // Write a message to the on-screen log (shown if the user has
    // toggled on the text display).
    virtual void Print(const char* fmt, ...) = 0; // __attribute__((format(printf, 1, 2))) = 0;

    // --- key handling ---

    // Wait for keypress and return it.  May return -1 after timeout.
    virtual int WaitKey();

    virtual bool IsKeyPressed(int key);

    // Erase any queued-up keys.
    virtual void FlushKeys();

    // Called on each keypress, even while operations are in progress.
    // Return value indicates whether an immediate operation should be
    // triggered (toggling the display, rebooting the device), or if
    // the key should be enqueued for use by the main thread.
    enum KeyAction { ENQUEUE, TOGGLE, REBOOT, IGNORE };
    virtual KeyAction CheckKey(int key);

    virtual void NextCheckKeyIsLong(bool is_long_press);

    // --- menu display ---

    // Display some header text followed by a menu of items, which appears
    // at the top of the screen (in place of any scrolling ui_print()
    // output, if necessary).
    virtual void StartMenu(const char* const * headers, const char* const * items,
                           int initial_selection) = 0;

    // Set the menu highlight to the given index, and return it (capped to
    // the range [0..numitems).
    virtual int SelectMenu(int sel) = 0;

    // End menu mode, resetting the text overlay so that ui_print()
    // statements will be displayed.
    virtual void EndMenu() = 0;

protected:
    void EnqueueKey(int key_code);

private:
    // Key event input queue
    pthread_mutex_t key_queue_mutex;
    pthread_cond_t key_queue_cond;
    int key_queue[256], key_queue_len;
    char key_pressed[KEY_MAX + 1];     // under key_queue_mutex
    int key_last_down;                 // under key_queue_mutex
    clock_t key_down_time;             // under key_queue_mutex
    int rel_sum;

    pthread_t input_t;

    static void* input_thread(void* cookie);
    static int input_callback(int fd, short revents, void* data);
    void process_key(int key_code, int updown);
    bool usb_connected();
};

#endif  // RECOVERY_UI_H
