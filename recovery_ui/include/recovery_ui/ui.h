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

#include <linux/input.h>  // KEY_MAX

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

static constexpr const char* DEFAULT_LOCALE = "en-US";

// Abstract class for controlling the user interface during recovery.
class RecoveryUI {
 public:
  enum Icon {
    NONE,
    INSTALLING_UPDATE,
    ERASING,
    NO_COMMAND,
    ERROR,
  };

  enum ProgressType {
    EMPTY,
    INDETERMINATE,
    DETERMINATE,
  };

  enum KeyAction {
    ENQUEUE,
    TOGGLE,
    REBOOT,
    IGNORE,
  };

  enum class KeyError : int {
    TIMED_OUT = -1,
    INTERRUPTED = -2,
  };

  RecoveryUI();

  virtual ~RecoveryUI();

  // Initializes the object; called before anything else. UI texts will be initialized according
  // to the given locale. Returns true on success.
  virtual bool Init(const std::string& locale);

  virtual std::string GetLocale() const = 0;

  // Shows a stage indicator. Called immediately after Init().
  virtual void SetStage(int current, int max) = 0;

  // Sets the overall recovery state ("background image").
  virtual void SetBackground(Icon icon) = 0;
  virtual void SetSystemUpdateText(bool security_update) = 0;

  // --- progress indicator ---
  virtual void SetProgressType(ProgressType determinate) = 0;

  // Shows a progress bar and define the scope of the next operation:
  //   portion - fraction of the progress bar the next operation will use
  //   seconds - expected time interval (progress bar moves at this minimum rate)
  virtual void ShowProgress(float portion, float seconds) = 0;

  // Sets progress bar position (0.0 - 1.0 within the scope defined by the last call to
  // ShowProgress).
  virtual void SetProgress(float fraction) = 0;

  // --- text log ---

  virtual void ShowText(bool visible) = 0;

  virtual bool IsTextVisible() = 0;

  virtual bool WasTextEverVisible() = 0;

  // Writes a message to the on-screen log (shown if the user has toggled on the text display).
  // Print() will also dump the message to stdout / log file, while PrintOnScreenOnly() not.
  virtual void Print(const char* fmt, ...) __printflike(2, 3) = 0;
  virtual void PrintOnScreenOnly(const char* fmt, ...) __printflike(2, 3) = 0;

  // Shows the contents of the given file. Caller ensures the patition that contains the file has
  // been mounted.
  virtual void ShowFile(const std::string& filename) = 0;

  // --- key handling ---

  // Waits for a key and return it. May return TIMED_OUT after timeout and
  // KeyError::INTERRUPTED on a key interrupt.
  virtual int WaitKey();

  // Wakes up the UI if it is waiting on key input, causing WaitKey to return KeyError::INTERRUPTED.
  virtual void InterruptKey();

  virtual bool IsKeyPressed(int key);
  virtual bool IsLongPress();

  // Returns true if you have the volume up/down and power trio typical of phones and tablets, false
  // otherwise.
  virtual bool HasThreeButtons() const;

  // Returns true if it has a power key.
  virtual bool HasPowerKey() const;

  // Returns true if it supports touch inputs.
  virtual bool HasTouchScreen() const;

  // Erases any queued-up keys.
  virtual void FlushKeys();

  // Called on each key press, even while operations are in progress. Return value indicates whether
  // an immediate operation should be triggered (toggling the display, rebooting the device), or if
  // the key should be enqueued for use by the main thread.
  virtual KeyAction CheckKey(int key, bool is_long_press);

  // Called when a key is held down long enough to have been a long-press (but before the key is
  // released). This means that if the key is eventually registered (released without any other keys
  // being pressed in the meantime), CheckKey will be called with 'is_long_press' true.
  virtual void KeyLongPress(int key);

  // Normally in recovery there's a key sequence that triggers immediate reboot of the device,
  // regardless of what recovery is doing (with the default CheckKey implementation, it's pressing
  // the power button 7 times in row). Call this to enable or disable that feature. It is enabled by
  // default.
  virtual void SetEnableReboot(bool enabled);

  // --- menu display ---

  virtual void SetTitle(const std::vector<std::string>& lines) = 0;

  // Displays a menu with the given 'headers' and 'items'. The supplied 'key_handler' callback,
  // which is typically bound to Device::HandleMenuKey(), should return the expected action for the
  // given key code and menu visibility (e.g. to move the cursor or to select an item). Caller sets
  // 'menu_only' to true to ensure only a menu item gets selected and returned. Otherwise if
  // 'menu_only' is false, ShowMenu() will forward any non-negative value returned from the
  // key_handler, which may be beyond the range of menu items. This could be used to trigger a
  // device-specific action, even without that being listed in the menu. Caller needs to handle
  // such a case accordingly (e.g. by calling Device::InvokeMenuItem() to process the action).
  // Returns a non-negative value (the chosen item number or device-specific action code), or
  // static_cast<size_t>(TIMED_OUT) if timed out waiting for input or
  // static_cast<size_t>(ERR_KEY_INTERTUPT) if interrupted, such as by InterruptKey().
  virtual size_t ShowMenu(const std::vector<std::string>& headers,
                          const std::vector<std::string>& items, size_t initial_selection,
                          bool menu_only, const std::function<int(int, bool)>& key_handler) = 0;

  // Displays the localized wipe data menu with pre-generated graphs. If there's an issue
  // with the graphs, falls back to use the backup string headers and items instead. The initial
  // selection is the 0th item in the menu, which is expected to reboot the device without a wipe.
  virtual size_t ShowPromptWipeDataMenu(const std::vector<std::string>& backup_headers,
                                        const std::vector<std::string>& backup_items,
                                        const std::function<int(int, bool)>& key_handler) = 0;
  // Displays the localized wipe data confirmation menu with pre-generated images. Falls back to
  // the text strings upon failures. The initial selection is the 0th item, which returns to the
  // upper level menu.
  virtual size_t ShowPromptWipeDataConfirmationMenu(
      const std::vector<std::string>& backup_headers, const std::vector<std::string>& backup_items,
      const std::function<int(int, bool)>& key_handler) = 0;

  // Set whether or not the fastbootd logo is displayed.
  void SetEnableFastbootdLogo(bool enable) {
    fastbootd_logo_enabled_ = enable;
  }

  // Resets the key interrupt status.
  void ResetKeyInterruptStatus() {
    key_interrupted_ = false;
  }

  // Returns the key interrupt status.
  bool IsKeyInterrupted() const {
    return key_interrupted_;
  }

 protected:
  void EnqueueKey(int key_code);

  // The normal and dimmed brightness percentages (default: 50 and 25, which means 50% and 25% of
  // the max_brightness). Because the absolute values may vary across devices. These two values can
  // be configured via subclassing. Setting brightness_normal_ to 0 to disable screensaver.
  unsigned int brightness_normal_;
  unsigned int brightness_dimmed_;
  std::string brightness_file_;
  std::string max_brightness_file_;

  // Whether we should listen for touch inputs (default: false).
  bool touch_screen_allowed_;

  bool fastbootd_logo_enabled_;

 private:
  enum class ScreensaverState {
    DISABLED,
    NORMAL,
    DIMMED,
    OFF,
  };

  // The sensitivity when detecting a swipe.
  const int touch_low_threshold_;
  const int touch_high_threshold_;

  void OnKeyDetected(int key_code);
  void OnTouchDetected(int dx, int dy);
  int OnInputEvent(int fd, uint32_t epevents);
  void ProcessKey(int key_code, int updown);
  void TimeKey(int key_code, int count);

  bool IsUsbConnected();

  bool InitScreensaver();
  void SetScreensaverState(ScreensaverState state);

  // Key event input queue
  std::mutex key_queue_mutex;
  std::condition_variable key_queue_cond;
  bool key_interrupted_;
  int key_queue[256], key_queue_len;

  // key press events
  std::mutex key_press_mutex;
  char key_pressed[KEY_MAX + 1];
  int key_last_down;
  bool key_long_press;
  int key_down_count;
  bool enable_reboot;

  int rel_sum;
  int consecutive_power_keys;

  bool has_power_key;
  bool has_up_key;
  bool has_down_key;
  bool has_touch_screen;

  // Touch event related variables. See the comments in RecoveryUI::OnInputEvent().
  int touch_slot_;
  int touch_X_;
  int touch_Y_;
  int touch_start_X_;
  int touch_start_Y_;
  bool touch_finger_down_;
  bool touch_swiping_;
  bool is_bootreason_recovery_ui_;

  std::thread input_thread_;
  std::atomic<bool> input_thread_stopped_{ false };

  ScreensaverState screensaver_state_;

  // The following two contain the absolute values computed from brightness_normal_ and
  // brightness_dimmed_ respectively.
  unsigned int brightness_normal_value_;
  unsigned int brightness_dimmed_value_;
};

#endif  // RECOVERY_UI_H
