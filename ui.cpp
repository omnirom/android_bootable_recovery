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

#include "ui.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <functional>
#include <string>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/properties.h>
#include <android-base/strings.h>
#include <cutils/android_reboot.h>
#include <minui/minui.h>

#include "common.h"
#include "roots.h"
#include "device.h"

static constexpr int UI_WAIT_KEY_TIMEOUT_SEC = 120;
static constexpr const char* BRIGHTNESS_FILE = "/sys/class/leds/lcd-backlight/brightness";
static constexpr const char* MAX_BRIGHTNESS_FILE = "/sys/class/leds/lcd-backlight/max_brightness";

RecoveryUI::RecoveryUI()
    : locale_(""),
      rtl_locale_(false),
      brightness_normal_(50),
      brightness_dimmed_(25),
      key_queue_len(0),
      key_last_down(-1),
      key_long_press(false),
      key_down_count(0),
      enable_reboot(true),
      consecutive_power_keys(0),
      last_key(-1),
      has_power_key(false),
      has_up_key(false),
      has_down_key(false),
      screensaver_state_(ScreensaverState::DISABLED) {
  pthread_mutex_init(&key_queue_mutex, nullptr);
  pthread_cond_init(&key_queue_cond, nullptr);
  memset(key_pressed, 0, sizeof(key_pressed));
}

void RecoveryUI::OnKeyDetected(int key_code) {
    if (key_code == KEY_POWER) {
        has_power_key = true;
    } else if (key_code == KEY_DOWN || key_code == KEY_VOLUMEDOWN) {
        has_down_key = true;
    } else if (key_code == KEY_UP || key_code == KEY_VOLUMEUP) {
        has_up_key = true;
    }
}

// Reads input events, handles special hot keys, and adds to the key queue.
static void* InputThreadLoop(void*) {
    while (true) {
        if (!ev_wait(-1)) {
            ev_dispatch();
        }
    }
    return nullptr;
}

bool RecoveryUI::InitScreensaver() {
  // Disabled.
  if (brightness_normal_ == 0 || brightness_dimmed_ > brightness_normal_) {
    return false;
  }

  // Set the initial brightness level based on the max brightness. Note that reading the initial
  // value from BRIGHTNESS_FILE doesn't give the actual brightness value (bullhead, sailfish), so
  // we don't have a good way to query the default value.
  std::string content;
  if (!android::base::ReadFileToString(MAX_BRIGHTNESS_FILE, &content)) {
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
                                        BRIGHTNESS_FILE)) {
    PLOG(WARNING) << "Failed to set brightness";
    return false;
  }

  LOG(INFO) << "Brightness: " << brightness_normal_value_ << " (" << brightness_normal_ << "%)";
  screensaver_state_ = ScreensaverState::NORMAL;
  return true;
}

bool RecoveryUI::Init(const std::string& locale) {
  // Set up the locale info.
  SetLocale(locale);

  ev_init(std::bind(&RecoveryUI::OnInputEvent, this, std::placeholders::_1, std::placeholders::_2));

  ev_iterate_available_keys(std::bind(&RecoveryUI::OnKeyDetected, this, std::placeholders::_1));

  if (!InitScreensaver()) {
    LOG(INFO) << "Screensaver disabled";
  }

  pthread_create(&input_thread_, nullptr, InputThreadLoop, nullptr);
  return true;
}

int RecoveryUI::OnInputEvent(int fd, uint32_t epevents) {
    struct input_event ev;
    if (ev_get_input(fd, epevents, &ev) == -1) {
        return -1;
    }

    if (ev.type == EV_SYN) {
        return 0;
    } else if (ev.type == EV_REL) {
        if (ev.code == REL_Y) {
            // accumulate the up or down motion reported by
            // the trackball.  When it exceeds a threshold
            // (positive or negative), fake an up/down
            // key event.
            rel_sum += ev.value;
            if (rel_sum > 3) {
                ProcessKey(KEY_DOWN, 1);   // press down key
                ProcessKey(KEY_DOWN, 0);   // and release it
                rel_sum = 0;
            } else if (rel_sum < -3) {
                ProcessKey(KEY_UP, 1);     // press up key
                ProcessKey(KEY_UP, 0);     // and release it
                rel_sum = 0;
            }
        }
    } else {
        rel_sum = 0;
    }

    if (ev.type == EV_KEY && ev.code <= KEY_MAX) {
        ProcessKey(ev.code, ev.value);
    }

    return 0;
}

// Process a key-up or -down event.  A key is "registered" when it is
// pressed and then released, with no other keypresses or releases in
// between.  Registered keys are passed to CheckKey() to see if it
// should trigger a visibility toggle, an immediate reboot, or be
// queued to be processed next time the foreground thread wants a key
// (eg, for the menu).
//
// We also keep track of which keys are currently down so that
// CheckKey can call IsKeyPressed to see what other keys are held when
// a key is registered.
//
// updown == 1 for key down events; 0 for key up events
void RecoveryUI::ProcessKey(int key_code, int updown) {
    bool register_key = false;
    bool long_press = false;
    bool reboot_enabled;

    pthread_mutex_lock(&key_queue_mutex);
    key_pressed[key_code] = updown;
    if (updown) {
        ++key_down_count;
        key_last_down = key_code;
        key_long_press = false;
        key_timer_t* info = new key_timer_t;
        info->ui = this;
        info->key_code = key_code;
        info->count = key_down_count;
        pthread_t thread;
        pthread_create(&thread, nullptr, &RecoveryUI::time_key_helper, info);
        pthread_detach(thread);
    } else {
        if (key_last_down == key_code) {
            long_press = key_long_press;
            register_key = true;
        }
        key_last_down = -1;
    }
    reboot_enabled = enable_reboot;
    pthread_mutex_unlock(&key_queue_mutex);

    if (register_key) {
        switch (CheckKey(key_code, long_press)) {
          case RecoveryUI::IGNORE:
            break;

          case RecoveryUI::TOGGLE:
            ShowText(!IsTextVisible());
            break;

          case RecoveryUI::REBOOT:
            if (reboot_enabled) {
                reboot("reboot,");
                while (true) { pause(); }
            }
            break;

          case RecoveryUI::ENQUEUE:
            EnqueueKey(key_code);
            break;
        }
    }
}

void* RecoveryUI::time_key_helper(void* cookie) {
    key_timer_t* info = static_cast<key_timer_t*>(cookie);
    info->ui->time_key(info->key_code, info->count);
    delete info;
    return nullptr;
}

void RecoveryUI::time_key(int key_code, int count) {
    usleep(750000);  // 750 ms == "long"
    bool long_press = false;
    pthread_mutex_lock(&key_queue_mutex);
    if (key_last_down == key_code && key_down_count == count) {
        long_press = key_long_press = true;
    }
    pthread_mutex_unlock(&key_queue_mutex);
    if (long_press) KeyLongPress(key_code);
}

void RecoveryUI::EnqueueKey(int key_code) {
    pthread_mutex_lock(&key_queue_mutex);
    const int queue_max = sizeof(key_queue) / sizeof(key_queue[0]);
    if (key_queue_len < queue_max) {
        key_queue[key_queue_len++] = key_code;
        pthread_cond_signal(&key_queue_cond);
    }
    pthread_mutex_unlock(&key_queue_mutex);
}

int RecoveryUI::WaitKey() {
  pthread_mutex_lock(&key_queue_mutex);

  // Time out after UI_WAIT_KEY_TIMEOUT_SEC, unless a USB cable is
  // plugged in.
  do {
    struct timeval now;
    struct timespec timeout;
    gettimeofday(&now, nullptr);
    timeout.tv_sec = now.tv_sec;
    timeout.tv_nsec = now.tv_usec * 1000;
    timeout.tv_sec += UI_WAIT_KEY_TIMEOUT_SEC;

    int rc = 0;
    while (key_queue_len == 0 && rc != ETIMEDOUT) {
      rc = pthread_cond_timedwait(&key_queue_cond, &key_queue_mutex, &timeout);
    }

    if (screensaver_state_ != ScreensaverState::DISABLED) {
      if (rc == ETIMEDOUT) {
        // Lower the brightness level: NORMAL -> DIMMED; DIMMED -> OFF.
        if (screensaver_state_ == ScreensaverState::NORMAL) {
          if (android::base::WriteStringToFile(std::to_string(brightness_dimmed_value_),
                                               BRIGHTNESS_FILE)) {
            LOG(INFO) << "Brightness: " << brightness_dimmed_value_ << " (" << brightness_dimmed_
                      << "%)";
            screensaver_state_ = ScreensaverState::DIMMED;
          }
        } else if (screensaver_state_ == ScreensaverState::DIMMED) {
          if (android::base::WriteStringToFile("0", BRIGHTNESS_FILE)) {
            LOG(INFO) << "Brightness: 0 (off)";
            screensaver_state_ = ScreensaverState::OFF;
          }
        }
      } else if (screensaver_state_ != ScreensaverState::NORMAL) {
        // Drop the first key if it's changing from OFF to NORMAL.
        if (screensaver_state_ == ScreensaverState::OFF) {
          if (key_queue_len > 0) {
            memcpy(&key_queue[0], &key_queue[1], sizeof(int) * --key_queue_len);
          }
        }

        // Reset the brightness to normal.
        if (android::base::WriteStringToFile(std::to_string(brightness_normal_value_),
                                             BRIGHTNESS_FILE)) {
          screensaver_state_ = ScreensaverState::NORMAL;
          LOG(INFO) << "Brightness: " << brightness_normal_value_ << " (" << brightness_normal_
                    << "%)";
        }
      }
    }
  } while (IsUsbConnected() && key_queue_len == 0);

  int key = -1;
  if (key_queue_len > 0) {
    key = key_queue[0];
    memcpy(&key_queue[0], &key_queue[1], sizeof(int) * --key_queue_len);
  }
  pthread_mutex_unlock(&key_queue_mutex);
  return key;
}

bool RecoveryUI::IsUsbConnected() {
    int fd = open("/sys/class/android_usb/android0/state", O_RDONLY);
    if (fd < 0) {
        printf("failed to open /sys/class/android_usb/android0/state: %s\n",
               strerror(errno));
        return 0;
    }

    char buf;
    // USB is connected if android_usb state is CONNECTED or CONFIGURED.
    int connected = (TEMP_FAILURE_RETRY(read(fd, &buf, 1)) == 1) && (buf == 'C');
    if (close(fd) < 0) {
        printf("failed to close /sys/class/android_usb/android0/state: %s\n",
               strerror(errno));
    }
    return connected;
}

bool RecoveryUI::IsKeyPressed(int key) {
    pthread_mutex_lock(&key_queue_mutex);
    int pressed = key_pressed[key];
    pthread_mutex_unlock(&key_queue_mutex);
    return pressed;
}

bool RecoveryUI::IsLongPress() {
    pthread_mutex_lock(&key_queue_mutex);
    bool result = key_long_press;
    pthread_mutex_unlock(&key_queue_mutex);
    return result;
}

bool RecoveryUI::HasThreeButtons() {
    return has_power_key && has_up_key && has_down_key;
}

void RecoveryUI::FlushKeys() {
    pthread_mutex_lock(&key_queue_mutex);
    key_queue_len = 0;
    pthread_mutex_unlock(&key_queue_mutex);
}

RecoveryUI::KeyAction RecoveryUI::CheckKey(int key, bool is_long_press) {
    pthread_mutex_lock(&key_queue_mutex);
    key_long_press = false;
    pthread_mutex_unlock(&key_queue_mutex);

    // If we have power and volume up keys, that chord is the signal to toggle the text display.
    if (HasThreeButtons()) {
        if (key == KEY_VOLUMEUP && IsKeyPressed(KEY_POWER)) {
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
        pthread_mutex_lock(&key_queue_mutex);
        bool reboot_enabled = enable_reboot;
        pthread_mutex_unlock(&key_queue_mutex);

        if (reboot_enabled) {
            ++consecutive_power_keys;
            if (consecutive_power_keys >= 7) {
                return REBOOT;
            }
        }
    } else {
        consecutive_power_keys = 0;
    }

    last_key = key;
    return (IsTextVisible() || screensaver_state_ == ScreensaverState::OFF) ? ENQUEUE : IGNORE;
}

void RecoveryUI::KeyLongPress(int) {
}

void RecoveryUI::SetEnableReboot(bool enabled) {
    pthread_mutex_lock(&key_queue_mutex);
    enable_reboot = enabled;
    pthread_mutex_unlock(&key_queue_mutex);
}

void RecoveryUI::SetLocale(const std::string& new_locale) {
  this->locale_ = new_locale;
  this->rtl_locale_ = false;

  if (!new_locale.empty()) {
    size_t underscore = new_locale.find('_');
    // lang has the language prefix prior to '_', or full string if '_' doesn't exist.
    std::string lang = new_locale.substr(0, underscore);

    // A bit cheesy: keep an explicit list of supported RTL languages.
    if (lang == "ar" ||  // Arabic
        lang == "fa" ||  // Persian (Farsi)
        lang == "he" ||  // Hebrew (new language code)
        lang == "iw" ||  // Hebrew (old language code)
        lang == "ur") {  // Urdu
      rtl_locale_ = true;
    }
  }
}
