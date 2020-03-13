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

#include "recovery_ui/ui.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <chrono>
#include <functional>
#include <string>
#include <thread>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/properties.h>
#include <android-base/strings.h>

#include "minui/minui.h"
#include "otautil/sysutil.h"

using namespace std::chrono_literals;

constexpr int UI_WAIT_KEY_TIMEOUT_SEC = 120;
constexpr const char* BRIGHTNESS_FILE = "/sys/class/leds/lcd-backlight/brightness";
constexpr const char* MAX_BRIGHTNESS_FILE = "/sys/class/leds/lcd-backlight/max_brightness";
constexpr const char* BRIGHTNESS_FILE_SDM = "/sys/class/backlight/panel0-backlight/brightness";
constexpr const char* MAX_BRIGHTNESS_FILE_SDM =
    "/sys/class/backlight/panel0-backlight/max_brightness";

constexpr int kDefaultTouchLowThreshold = 50;
constexpr int kDefaultTouchHighThreshold = 90;

RecoveryUI::RecoveryUI()
    : brightness_normal_(50),
      brightness_dimmed_(25),
      brightness_file_(BRIGHTNESS_FILE),
      max_brightness_file_(MAX_BRIGHTNESS_FILE),
      touch_screen_allowed_(false),
      fastbootd_logo_enabled_(false),
      touch_low_threshold_(android::base::GetIntProperty("ro.recovery.ui.touch_low_threshold",
                                                         kDefaultTouchLowThreshold)),
      touch_high_threshold_(android::base::GetIntProperty("ro.recovery.ui.touch_high_threshold",
                                                          kDefaultTouchHighThreshold)),
      key_interrupted_(false),
      key_queue_len(0),
      key_last_down(-1),
      key_long_press(false),
      key_down_count(0),
      enable_reboot(true),
      consecutive_power_keys(0),
      has_power_key(false),
      has_up_key(false),
      has_down_key(false),
      has_touch_screen(false),
      touch_slot_(0),
      is_bootreason_recovery_ui_(false),
      screensaver_state_(ScreensaverState::DISABLED) {
  memset(key_pressed, 0, sizeof(key_pressed));
}

RecoveryUI::~RecoveryUI() {
  ev_exit();
  input_thread_stopped_ = true;
  if (input_thread_.joinable()) {
    input_thread_.join();
  }
}

void RecoveryUI::OnKeyDetected(int key_code) {
  if (key_code == KEY_POWER) {
    has_power_key = true;
  } else if (key_code == KEY_DOWN || key_code == KEY_VOLUMEDOWN) {
    has_down_key = true;
  } else if (key_code == KEY_UP || key_code == KEY_VOLUMEUP) {
    has_up_key = true;
  } else if (key_code == ABS_MT_POSITION_X || key_code == ABS_MT_POSITION_Y) {
    has_touch_screen = true;
  }
}

bool RecoveryUI::InitScreensaver() {
  // Disabled.
  if (brightness_normal_ == 0 || brightness_dimmed_ > brightness_normal_) {
    return false;
  }
  if (access(brightness_file_.c_str(), R_OK | W_OK)) {
    brightness_file_ = BRIGHTNESS_FILE_SDM;
  }
  if (access(max_brightness_file_.c_str(), R_OK)) {
    max_brightness_file_ = MAX_BRIGHTNESS_FILE_SDM;
  }
  // Set the initial brightness level based on the max brightness. Note that reading the initial
  // value from BRIGHTNESS_FILE doesn't give the actual brightness value (bullhead, sailfish), so
  // we don't have a good way to query the default value.
  std::string content;
  if (!android::base::ReadFileToString(max_brightness_file_, &content)) {
    PLOG(WARNING) << "Failed to read max brightness";
    return false;
  }

  unsigned int max_value;
  if (!android::base::ParseUint(android::base::Trim(content), &max_value)) {
    LOG(WARNING) << "Failed to parse max brightness: " << content;
    return false;
  }

  brightness_normal_value_ = max_value * brightness_normal_ / 100.0;
  brightness_dimmed_value_ = max_value * brightness_dimmed_ / 100.0;
  if (!android::base::WriteStringToFile(std::to_string(brightness_normal_value_),
                                        brightness_file_)) {
    PLOG(WARNING) << "Failed to set brightness";
    return false;
  }

  LOG(INFO) << "Brightness: " << brightness_normal_value_ << " (" << brightness_normal_ << "%)";
  screensaver_state_ = ScreensaverState::NORMAL;
  return true;
}

bool RecoveryUI::Init(const std::string& /* locale */) {
  ev_init(std::bind(&RecoveryUI::OnInputEvent, this, std::placeholders::_1, std::placeholders::_2),
          touch_screen_allowed_);

  ev_iterate_available_keys(std::bind(&RecoveryUI::OnKeyDetected, this, std::placeholders::_1));

  if (touch_screen_allowed_) {
    ev_iterate_touch_inputs(std::bind(&RecoveryUI::OnKeyDetected, this, std::placeholders::_1));

    // Parse /proc/cmdline to determine if it's booting into recovery with a bootreason of
    // "recovery_ui". This specific reason is set by some (wear) bootloaders, to allow an easier way
    // to turn on text mode. It will only be set if the recovery boot is triggered from fastboot, or
    // with 'adb reboot recovery'. Note that this applies to all build variants. Otherwise the text
    // mode will be turned on automatically on debuggable builds, even without a swipe.
    std::string cmdline;
    if (android::base::ReadFileToString("/proc/cmdline", &cmdline)) {
      is_bootreason_recovery_ui_ = cmdline.find("bootreason=recovery_ui") != std::string::npos;
    } else {
      // Non-fatal, and won't affect Init() result.
      PLOG(WARNING) << "Failed to read /proc/cmdline";
    }
  }

  if (!InitScreensaver()) {
    LOG(INFO) << "Screensaver disabled";
  }

  // Create a separate thread that handles input events.
  input_thread_ = std::thread([this]() {
    while (!this->input_thread_stopped_) {
      if (!ev_wait(500)) {
        ev_dispatch();
      }
    }
  });

  return true;
}

void RecoveryUI::OnTouchDetected(int dx, int dy) {
  enum SwipeDirection { UP, DOWN, RIGHT, LEFT } direction;

  // We only consider a valid swipe if:
  // - the delta along one axis is below touch_low_threshold_;
  // - and the delta along the other axis is beyond touch_high_threshold_.
  if (abs(dy) < touch_low_threshold_ && abs(dx) > touch_high_threshold_) {
    direction = dx < 0 ? SwipeDirection::LEFT : SwipeDirection::RIGHT;
  } else if (abs(dx) < touch_low_threshold_ && abs(dy) > touch_high_threshold_) {
    direction = dy < 0 ? SwipeDirection::UP : SwipeDirection::DOWN;
  } else {
    LOG(DEBUG) << "Ignored " << dx << " " << dy << " (low: " << touch_low_threshold_
               << ", high: " << touch_high_threshold_ << ")";
    return;
  }

  // Allow turning on text mode with any swipe, if bootloader has set a bootreason of recovery_ui.
  if (is_bootreason_recovery_ui_ && !IsTextVisible()) {
    ShowText(true);
    return;
  }

  LOG(DEBUG) << "Swipe direction=" << direction;
  switch (direction) {
    case SwipeDirection::UP:
      ProcessKey(KEY_UP, 1);  // press up key
      ProcessKey(KEY_UP, 0);  // and release it
      break;

    case SwipeDirection::DOWN:
      ProcessKey(KEY_DOWN, 1);  // press down key
      ProcessKey(KEY_DOWN, 0);  // and release it
      break;

    case SwipeDirection::LEFT:
    case SwipeDirection::RIGHT:
      ProcessKey(KEY_POWER, 1);  // press power key
      ProcessKey(KEY_POWER, 0);  // and release it
      break;
  };
}

int RecoveryUI::OnInputEvent(int fd, uint32_t epevents) {
  struct input_event ev;
  if (ev_get_input(fd, epevents, &ev) == -1) {
    return -1;
  }

  // Touch inputs handling.
  //
  // We handle the touch inputs by tracking the position changes between initial contacting and
  // upon lifting. touch_start_X/Y record the initial positions, with touch_finger_down set. Upon
  // detecting the lift, we unset touch_finger_down and detect a swipe based on position changes.
  //
  // Per the doc Multi-touch Protocol at below, there are two protocols.
  // https://www.kernel.org/doc/Documentation/input/multi-touch-protocol.txt
  //
  // The main difference between the stateless type A protocol and the stateful type B slot protocol
  // lies in the usage of identifiable contacts to reduce the amount of data sent to userspace. The
  // slot protocol (i.e. type B) sends ABS_MT_TRACKING_ID with a unique id on initial contact, and
  // sends ABS_MT_TRACKING_ID -1 upon lifting the contact. Protocol A doesn't send
  // ABS_MT_TRACKING_ID -1 on lifting, but the driver may additionally report BTN_TOUCH event.
  //
  // For protocol A, we rely on BTN_TOUCH to recognize lifting, while for protocol B we look for
  // ABS_MT_TRACKING_ID being -1.
  //
  // Touch input events will only be available if touch_screen_allowed_ is set.

  if (ev.type == EV_SYN) {
    if (touch_screen_allowed_ && ev.code == SYN_REPORT) {
      // There might be multiple SYN_REPORT events. We should only detect a swipe after lifting the
      // contact.
      if (touch_finger_down_ && !touch_swiping_) {
        touch_start_X_ = touch_X_;
        touch_start_Y_ = touch_Y_;
        touch_swiping_ = true;
      } else if (!touch_finger_down_ && touch_swiping_) {
        touch_swiping_ = false;
        OnTouchDetected(touch_X_ - touch_start_X_, touch_Y_ - touch_start_Y_);
      }
    }
    return 0;
  }

  if (ev.type == EV_REL) {
    if (ev.code == REL_Y) {
      // accumulate the up or down motion reported by
      // the trackball.  When it exceeds a threshold
      // (positive or negative), fake an up/down
      // key event.
      rel_sum += ev.value;
      if (rel_sum > 3) {
        ProcessKey(KEY_DOWN, 1);  // press down key
        ProcessKey(KEY_DOWN, 0);  // and release it
        rel_sum = 0;
      } else if (rel_sum < -3) {
        ProcessKey(KEY_UP, 1);  // press up key
        ProcessKey(KEY_UP, 0);  // and release it
        rel_sum = 0;
      }
    }
  } else {
    rel_sum = 0;
  }

  if (touch_screen_allowed_ && ev.type == EV_ABS) {
    if (ev.code == ABS_MT_SLOT) {
      touch_slot_ = ev.value;
    }
    // Ignore other fingers.
    if (touch_slot_ > 0) return 0;

    switch (ev.code) {
      case ABS_MT_POSITION_X:
        touch_X_ = ev.value;
        touch_finger_down_ = true;
        break;

      case ABS_MT_POSITION_Y:
        touch_Y_ = ev.value;
        touch_finger_down_ = true;
        break;

      case ABS_MT_TRACKING_ID:
        // Protocol B: -1 marks lifting the contact.
        if (ev.value < 0) touch_finger_down_ = false;
        break;
    }
    return 0;
  }

  if (ev.type == EV_KEY && ev.code <= KEY_MAX) {
    if (touch_screen_allowed_) {
      if (ev.code == BTN_TOUCH) {
        // A BTN_TOUCH with value 1 indicates the start of contact (protocol A), with 0 means
        // lifting the contact.
        touch_finger_down_ = (ev.value == 1);
      }

      // Intentionally ignore BTN_TOUCH and BTN_TOOL_FINGER, which would otherwise trigger
      // additional scrolling (because in ScreenRecoveryUI::ShowFile(), we consider keys other than
      // KEY_POWER and KEY_UP as KEY_DOWN).
      if (ev.code == BTN_TOUCH || ev.code == BTN_TOOL_FINGER) {
        return 0;
      }
    }

    ProcessKey(ev.code, ev.value);
  }

  return 0;
}

// Processes a key-up or -down event. A key is "registered" when it is pressed and then released,
// with no other keypresses or releases in between. Registered keys are passed to CheckKey() to
// see if it should trigger a visibility toggle, an immediate reboot, or be queued to be processed
// next time the foreground thread wants a key (eg, for the menu).
//
// We also keep track of which keys are currently down so that CheckKey() can call IsKeyPressed()
// to see what other keys are held when a key is registered.
//
// updown == 1 for key down events; 0 for key up events
void RecoveryUI::ProcessKey(int key_code, int updown) {
  bool register_key = false;
  bool long_press = false;

  {
    std::lock_guard<std::mutex> lg(key_press_mutex);
    key_pressed[key_code] = updown;
    if (updown) {
      ++key_down_count;
      key_last_down = key_code;
      key_long_press = false;
      std::thread time_key_thread(&RecoveryUI::TimeKey, this, key_code, key_down_count);
      time_key_thread.detach();
    } else {
      if (key_last_down == key_code) {
        long_press = key_long_press;
        register_key = true;
      }
      key_last_down = -1;
    }
  }

  bool reboot_enabled = enable_reboot;
  if (register_key) {
    switch (CheckKey(key_code, long_press)) {
      case RecoveryUI::IGNORE:
        break;

      case RecoveryUI::TOGGLE:
        ShowText(!IsTextVisible());
        break;

      case RecoveryUI::REBOOT:
        if (reboot_enabled) {
          Reboot("userrequested,recovery,ui");
        }
        break;

      case RecoveryUI::ENQUEUE:
        EnqueueKey(key_code);
        break;
    }
  }
}

void RecoveryUI::TimeKey(int key_code, int count) {
  std::this_thread::sleep_for(750ms);  // 750 ms == "long"
  bool long_press = false;
  {
    std::lock_guard<std::mutex> lg(key_press_mutex);
    if (key_last_down == key_code && key_down_count == count) {
      long_press = key_long_press = true;
    }
  }
  if (long_press) KeyLongPress(key_code);
}

void RecoveryUI::EnqueueKey(int key_code) {
  std::lock_guard<std::mutex> lg(key_queue_mutex);
  const int queue_max = sizeof(key_queue) / sizeof(key_queue[0]);
  if (key_queue_len < queue_max) {
    key_queue[key_queue_len++] = key_code;
    key_queue_cond.notify_one();
  }
}

void RecoveryUI::SetScreensaverState(ScreensaverState state) {
  switch (state) {
    case ScreensaverState::NORMAL:
      if (android::base::WriteStringToFile(std::to_string(brightness_normal_value_),
                                           brightness_file_)) {
        screensaver_state_ = ScreensaverState::NORMAL;
        LOG(INFO) << "Brightness: " << brightness_normal_value_ << " (" << brightness_normal_
                  << "%)";
      } else {
        LOG(WARNING) << "Unable to set brightness to normal";
      }
      break;
    case ScreensaverState::DIMMED:
      if (android::base::WriteStringToFile(std::to_string(brightness_dimmed_value_),
                                           brightness_file_)) {
        LOG(INFO) << "Brightness: " << brightness_dimmed_value_ << " (" << brightness_dimmed_
                  << "%)";
        screensaver_state_ = ScreensaverState::DIMMED;
      } else {
        LOG(WARNING) << "Unable to set brightness to dim";
      }
      break;
    case ScreensaverState::OFF:
      if (android::base::WriteStringToFile("0", brightness_file_)) {
        LOG(INFO) << "Brightness: 0 (off)";
        screensaver_state_ = ScreensaverState::OFF;
      } else {
        LOG(WARNING) << "Unable to set brightness to off";
      }
      break;
    default:
      LOG(ERROR) << "Invalid screensaver state";
  }
}

int RecoveryUI::WaitKey() {
  std::unique_lock<std::mutex> lk(key_queue_mutex);

  // Check for a saved key queue interruption.
  if (key_interrupted_) {
    SetScreensaverState(ScreensaverState::NORMAL);
    return static_cast<int>(KeyError::INTERRUPTED);
  }

  // Time out after UI_WAIT_KEY_TIMEOUT_SEC, unless a USB cable is plugged in.
  do {
    bool rc = key_queue_cond.wait_for(lk, std::chrono::seconds(UI_WAIT_KEY_TIMEOUT_SEC), [this] {
      return this->key_queue_len != 0 || key_interrupted_;
    });
    if (key_interrupted_) {
      SetScreensaverState(ScreensaverState::NORMAL);
      return static_cast<int>(KeyError::INTERRUPTED);
    }
    if (screensaver_state_ != ScreensaverState::DISABLED) {
      if (!rc) {
        // Must be after a timeout. Lower the brightness level: NORMAL -> DIMMED; DIMMED -> OFF.
        if (screensaver_state_ == ScreensaverState::NORMAL) {
          SetScreensaverState(ScreensaverState::DIMMED);
        } else if (screensaver_state_ == ScreensaverState::DIMMED) {
          SetScreensaverState(ScreensaverState::OFF);
        }
      } else if (screensaver_state_ != ScreensaverState::NORMAL) {
        // Drop the first key if it's changing from OFF to NORMAL.
        if (screensaver_state_ == ScreensaverState::OFF) {
          if (key_queue_len > 0) {
            memcpy(&key_queue[0], &key_queue[1], sizeof(int) * --key_queue_len);
          }
        }

        // Reset the brightness to normal.
        SetScreensaverState(ScreensaverState::NORMAL);
      }
    }
  } while (IsUsbConnected() && key_queue_len == 0);

  int key = static_cast<int>(KeyError::TIMED_OUT);
  if (key_queue_len > 0) {
    key = key_queue[0];
    memcpy(&key_queue[0], &key_queue[1], sizeof(int) * --key_queue_len);
  }
  return key;
}

void RecoveryUI::InterruptKey() {
  {
    std::lock_guard<std::mutex> lg(key_queue_mutex);
    key_interrupted_ = true;
  }
  key_queue_cond.notify_one();
}

bool RecoveryUI::IsUsbConnected() {
  int fd = open("/sys/class/android_usb/android0/state", O_RDONLY);
  if (fd < 0) {
    printf("failed to open /sys/class/android_usb/android0/state: %s\n", strerror(errno));
    return 0;
  }

  char buf;
  // USB is connected if android_usb state is CONNECTED or CONFIGURED.
  int connected = (TEMP_FAILURE_RETRY(read(fd, &buf, 1)) == 1) && (buf == 'C');
  if (close(fd) < 0) {
    printf("failed to close /sys/class/android_usb/android0/state: %s\n", strerror(errno));
  }
  return connected;
}

bool RecoveryUI::IsKeyPressed(int key) {
  std::lock_guard<std::mutex> lg(key_press_mutex);
  int pressed = key_pressed[key];
  return pressed;
}

bool RecoveryUI::IsLongPress() {
  std::lock_guard<std::mutex> lg(key_press_mutex);
  bool result = key_long_press;
  return result;
}

bool RecoveryUI::HasThreeButtons() const {
  return has_power_key && has_up_key && has_down_key;
}

bool RecoveryUI::HasPowerKey() const {
  return has_power_key;
}

bool RecoveryUI::HasTouchScreen() const {
  return has_touch_screen;
}

void RecoveryUI::FlushKeys() {
  std::lock_guard<std::mutex> lg(key_queue_mutex);
  key_queue_len = 0;
}

RecoveryUI::KeyAction RecoveryUI::CheckKey(int key, bool is_long_press) {
  {
    std::lock_guard<std::mutex> lg(key_press_mutex);
    key_long_press = false;
  }

  // If we have power and volume up keys, that chord is the signal to toggle the text display.
  if (HasThreeButtons() || (HasPowerKey() && HasTouchScreen() && touch_screen_allowed_)) {
    if ((key == KEY_VOLUMEUP || key == KEY_UP) && IsKeyPressed(KEY_POWER)) {
      return TOGGLE;
    }
  } else {
    // Otherwise long press of any button toggles to the text display,
    // and there's no way to toggle back (but that's pretty useless anyway).
    if (is_long_press && !IsTextVisible()) {
      return TOGGLE;
    }

    // Also, for button-limited devices, a long press is translated to KEY_ENTER.
    if (is_long_press && IsTextVisible()) {
      EnqueueKey(KEY_ENTER);
      return IGNORE;
    }
  }

  // Press power seven times in a row to reboot.
  if (key == KEY_POWER) {
    bool reboot_enabled = enable_reboot;

    if (reboot_enabled) {
      ++consecutive_power_keys;
      if (consecutive_power_keys >= 7) {
        return REBOOT;
      }
    }
  } else {
    consecutive_power_keys = 0;
  }

  return (IsTextVisible() || screensaver_state_ == ScreensaverState::OFF) ? ENQUEUE : IGNORE;
}

void RecoveryUI::KeyLongPress(int) {}

void RecoveryUI::SetEnableReboot(bool enabled) {
  std::lock_guard<std::mutex> lg(key_press_mutex);
  enable_reboot = enabled;
}
