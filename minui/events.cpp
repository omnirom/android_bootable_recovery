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

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>

#include <functional>
#include <memory>

#include <android-base/unique_fd.h>

#include "minui/minui.h"

constexpr const char* INPUT_DEV_DIR = "/dev/input";

constexpr size_t MAX_DEVICES = 16;
constexpr size_t MAX_MISC_FDS = 16;

constexpr size_t BITS_PER_LONG = sizeof(unsigned long) * 8;
constexpr size_t BITS_TO_LONGS(size_t bits) {
  return ((bits + BITS_PER_LONG - 1) / BITS_PER_LONG);
}

struct FdInfo {
  android::base::unique_fd fd;
  ev_callback cb;
};

static bool g_allow_touch_inputs = true;
static ev_callback g_saved_input_cb;
static android::base::unique_fd g_epoll_fd;
static epoll_event g_polled_events[MAX_DEVICES + MAX_MISC_FDS];
static int g_polled_events_count;

static FdInfo ev_fdinfo[MAX_DEVICES + MAX_MISC_FDS];

static size_t g_ev_count = 0;
static size_t g_ev_dev_count = 0;
static size_t g_ev_misc_count = 0;

static bool test_bit(size_t bit, unsigned long* array) { // NOLINT
  return (array[bit / BITS_PER_LONG] & (1UL << (bit % BITS_PER_LONG))) != 0;
}

static bool should_add_input_device(int fd, bool allow_touch_inputs) {
  // Use unsigned long to match ioctl's parameter type.
  unsigned long ev_bits[BITS_TO_LONGS(EV_MAX)];  // NOLINT

  // Read the evbits of the input device.
  if (ioctl(fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) == -1) {
    return false;
  }

  // We assume that only EV_KEY, EV_REL, and EV_SW event types are ever needed. EV_ABS is also
  // allowed if allow_touch_inputs is set.
  if (!test_bit(EV_KEY, ev_bits) && !test_bit(EV_REL, ev_bits) && !test_bit(EV_SW, ev_bits)) {
    if (!allow_touch_inputs || !test_bit(EV_ABS, ev_bits)) {
      return false;
    }
  }

  return true;
}

static int inotify_cb(int fd, __unused uint32_t epevents) {
  if (g_saved_input_cb == nullptr) return -1;

  // The inotify will put one or several complete events.
  // Should not read part of one event.
  int event_len_int;
  int ret = ioctl(fd, FIONREAD, &event_len_int);
  if (ret != 0) return -1;
  if (event_len_int < 0) return -1;
  size_t event_len = event_len_int;

  std::unique_ptr<DIR, decltype(&closedir)> dir(opendir(INPUT_DEV_DIR), closedir);
  if (!dir) {
    return -1;
  }

  std::vector<int8_t> buf(event_len);

  ssize_t r = TEMP_FAILURE_RETRY(read(fd, buf.data(), event_len));
  if (r != event_len) {
    return -1;
  }

  size_t offset = 0;
  while (offset < event_len) {
    struct inotify_event* pevent = reinterpret_cast<struct inotify_event*>(buf.data() + offset);
    if (offset + sizeof(inotify_event) + pevent->len > event_len) {
      // The pevent->len is too large and buffer will over flow.
      // In general, should not happen, just make more stable.
      return -1;
    }
    offset += sizeof(inotify_event) + pevent->len;

    pevent->name[pevent->len] = '\0';
    if (strncmp(pevent->name, "event", 5)) {
      continue;
    }

    android::base::unique_fd dfd(openat(dirfd(dir.get()), pevent->name, O_RDONLY));
    if (dfd == -1) {
      break;
    }

    if (!should_add_input_device(dfd, g_allow_touch_inputs)) {
      continue;
    }

    // Only add, we assume the user will not plug out and plug in USB device again and again :)
    ev_add_fd(std::move(dfd), g_saved_input_cb);
  }

  return 0;
}

int ev_init(ev_callback input_cb, bool allow_touch_inputs) {
  g_epoll_fd.reset();

  android::base::unique_fd epoll_fd(epoll_create1(EPOLL_CLOEXEC));
  if (epoll_fd == -1) {
    return -1;
  }

  android::base::unique_fd inotify_fd(inotify_init1(IN_CLOEXEC));
  if (inotify_fd.get() == -1) {
    return -1;
  }

  if (inotify_add_watch(inotify_fd, INPUT_DEV_DIR, IN_CREATE) < 0) {
    return -1;
  }

  std::unique_ptr<DIR, decltype(&closedir)> dir(opendir(INPUT_DEV_DIR), closedir);
  if (!dir) {
    return -1;
  }

  bool epoll_ctl_failed = false;
  dirent* de;
  while ((de = readdir(dir.get())) != nullptr) {
    if (strncmp(de->d_name, "event", 5)) continue;
    android::base::unique_fd fd(openat(dirfd(dir.get()), de->d_name, O_RDONLY | O_CLOEXEC));
    if (fd == -1) continue;

    if (!should_add_input_device(fd, allow_touch_inputs)) {
      continue;
    }

    epoll_event ev;
    ev.events = EPOLLIN | EPOLLWAKEUP;
    ev.data.ptr = &ev_fdinfo[g_ev_count];
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1) {
      epoll_ctl_failed = true;
      continue;
    }

    ev_fdinfo[g_ev_count].fd.reset(fd.release());
    ev_fdinfo[g_ev_count].cb = input_cb;
    g_ev_count++;
    g_ev_dev_count++;
    if (g_ev_dev_count == MAX_DEVICES) break;
  }

  if (epoll_ctl_failed && !g_ev_count) {
    return -1;
  }

  g_epoll_fd.reset(epoll_fd.release());

  g_saved_input_cb = input_cb;
  g_allow_touch_inputs = allow_touch_inputs;
  ev_add_fd(std::move(inotify_fd), inotify_cb);

  return 0;
}

int ev_get_epollfd(void) {
  return g_epoll_fd.get();
}

int ev_add_fd(android::base::unique_fd&& fd, ev_callback cb) {
  if (g_ev_misc_count == MAX_MISC_FDS || cb == nullptr) {
    return -1;
  }

  epoll_event ev;
  ev.events = EPOLLIN | EPOLLWAKEUP;
  ev.data.ptr = static_cast<void*>(&ev_fdinfo[g_ev_count]);
  int ret = epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, fd, &ev);
  if (!ret) {
    ev_fdinfo[g_ev_count].fd.reset(fd.release());
    ev_fdinfo[g_ev_count].cb = std::move(cb);
    g_ev_count++;
    g_ev_misc_count++;
  }

  return ret;
}

void ev_exit(void) {
  while (g_ev_count > 0) {
    ev_fdinfo[--g_ev_count].fd.reset();
  }
  g_ev_misc_count = 0;
  g_ev_dev_count = 0;
  g_saved_input_cb = nullptr;
  g_epoll_fd.reset();
}

int ev_wait(int timeout) {
  g_polled_events_count = epoll_wait(g_epoll_fd, g_polled_events, g_ev_count, timeout);
  if (g_polled_events_count <= 0) {
    return -1;
  }
  return 0;
}

void ev_dispatch(void) {
  for (int n = 0; n < g_polled_events_count; n++) {
    FdInfo* fdi = static_cast<FdInfo*>(g_polled_events[n].data.ptr);
    const ev_callback& cb = fdi->cb;
    if (cb) {
      cb(fdi->fd, g_polled_events[n].events);
    }
  }
}

int ev_get_input(int fd, uint32_t epevents, input_event* ev) {
  if (epevents & EPOLLIN) {
    ssize_t r = TEMP_FAILURE_RETRY(read(fd, ev, sizeof(*ev)));
    if (r == sizeof(*ev)) {
      return 0;
    }
  }
  if (epevents & EPOLLHUP) {
    // Delete this watch
    epoll_ctl(g_epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
  }
  return -1;
}

int ev_sync_key_state(const ev_set_key_callback& set_key_cb) {
  // Use unsigned long to match ioctl's parameter type.
  unsigned long ev_bits[BITS_TO_LONGS(EV_MAX)];    // NOLINT
  unsigned long key_bits[BITS_TO_LONGS(KEY_MAX)];  // NOLINT

  for (size_t i = 0; i < g_ev_dev_count; ++i) {
    memset(ev_bits, 0, sizeof(ev_bits));
    memset(key_bits, 0, sizeof(key_bits));

    if (ioctl(ev_fdinfo[i].fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) == -1) {
      continue;
    }
    if (!test_bit(EV_KEY, ev_bits)) {
      continue;
    }
    if (ioctl(ev_fdinfo[i].fd, EVIOCGKEY(sizeof(key_bits)), key_bits) == -1) {
      continue;
    }

    for (int code = 0; code <= KEY_MAX; code++) {
      if (test_bit(code, key_bits)) {
        set_key_cb(code, 1);
      }
    }
  }

  return 0;
}

void ev_iterate_available_keys(const std::function<void(int)>& f) {
  // Use unsigned long to match ioctl's parameter type.
  unsigned long ev_bits[BITS_TO_LONGS(EV_MAX)];    // NOLINT
  unsigned long key_bits[BITS_TO_LONGS(KEY_MAX)];  // NOLINT

  for (size_t i = 0; i < g_ev_dev_count; ++i) {
    memset(ev_bits, 0, sizeof(ev_bits));
    memset(key_bits, 0, sizeof(key_bits));

    // Does this device even have keys?
    if (ioctl(ev_fdinfo[i].fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) == -1) {
      continue;
    }
    if (!test_bit(EV_KEY, ev_bits)) {
      continue;
    }

    if (ioctl(ev_fdinfo[i].fd, EVIOCGBIT(EV_KEY, KEY_MAX), key_bits) == -1) {
      continue;
    }

    for (int key_code = 0; key_code <= KEY_MAX; ++key_code) {
      if (test_bit(key_code, key_bits)) {
        f(key_code);
      }
    }
  }
}

void ev_iterate_touch_inputs(const std::function<void(int)>& action) {
  for (size_t i = 0; i < g_ev_dev_count; ++i) {
    // Use unsigned long to match ioctl's parameter type.
    unsigned long ev_bits[BITS_TO_LONGS(EV_MAX)] = {};  // NOLINT
    if (ioctl(ev_fdinfo[i].fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) == -1) {
      continue;
    }
    if (!test_bit(EV_ABS, ev_bits)) {
      continue;
    }

    unsigned long key_bits[BITS_TO_LONGS(KEY_MAX)] = {};  // NOLINT
    if (ioctl(ev_fdinfo[i].fd, EVIOCGBIT(EV_ABS, KEY_MAX), key_bits) == -1) {
      continue;
    }

    for (int key_code = 0; key_code <= KEY_MAX; ++key_code) {
      if (test_bit(key_code, key_bits)) {
        action(key_code);
      }
    }
  }
}
