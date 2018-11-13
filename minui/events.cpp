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

#include <bits/epoll_event.h> /* bionic workaround */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <functional>

#include "minui/minui.h"

#define MAX_DEVICES 16
#define MAX_MISC_FDS 16

#define BITS_PER_LONG (sizeof(unsigned long) * 8)
#define BITS_TO_LONGS(x) (((x) + BITS_PER_LONG - 1) / BITS_PER_LONG)

struct fd_info {
  int fd;
  ev_callback cb;
#ifdef TW_USE_MINUI_WITH_DATA
  void* data;
#endif
};

static int g_epoll_fd;
static epoll_event polledevents[MAX_DEVICES + MAX_MISC_FDS];
static int npolledevents;

static fd_info ev_fdinfo[MAX_DEVICES + MAX_MISC_FDS];

static unsigned ev_count = 0;
static unsigned ev_dev_count = 0;
static unsigned ev_misc_count = 0;

static bool test_bit(size_t bit, unsigned long* array) { // NOLINT
    return (array[bit/BITS_PER_LONG] & (1UL << (bit % BITS_PER_LONG))) != 0;
}

#ifdef TW_USE_MINUI_WITH_OPTIONAL_TOUCH_EVENTS
int ev_init(ev_callback input_cb, bool allow_touch_inputs) {
#else
#ifdef TW_USE_MINUI_WITH_DATA
int ev_init(ev_callback input_cb, void* data) {
#else
int ev_init(ev_callback input_cb) {
#endif
  bool allow_touch_inputs = false;
#endif

  g_epoll_fd = epoll_create(MAX_DEVICES + MAX_MISC_FDS);
  if (g_epoll_fd == -1) {
    return -1;
  }

  bool epollctlfail = false;
  DIR* dir = opendir("/dev/input");
  if (dir != nullptr) {
    dirent* de;
    while ((de = readdir(dir))) {
      if (strncmp(de->d_name, "event", 5)) continue;
      int fd = openat(dirfd(dir), de->d_name, O_RDONLY);
      if (fd == -1) continue;

      // Use unsigned long to match ioctl's parameter type.
      unsigned long ev_bits[BITS_TO_LONGS(EV_MAX)];  // NOLINT

      // Read the evbits of the input device.
      if (ioctl(fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) == -1) {
        close(fd);
        continue;
      }

      // We assume that only EV_KEY, EV_REL, and EV_SW event types are ever needed. EV_ABS is also
      // allowed if allow_touch_inputs is set.
      if (!test_bit(EV_KEY, ev_bits) && !test_bit(EV_REL, ev_bits) && !test_bit(EV_SW, ev_bits)) {
        if (!allow_touch_inputs || !test_bit(EV_ABS, ev_bits)) {
          close(fd);
          continue;
        }
      }

      epoll_event ev;
      ev.events = EPOLLIN | EPOLLWAKEUP;
      ev.data.ptr = &ev_fdinfo[ev_count];
      if (epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        close(fd);
        epollctlfail = true;
        continue;
      }

      ev_fdinfo[ev_count].fd = fd;
      ev_fdinfo[ev_count].cb = std::move(input_cb);
#ifdef TW_USE_MINUI_WITH_DATA
      ev_fdinfo[ev_count].data = data;
#endif
      ev_count++;
      ev_dev_count++;
      if (ev_dev_count == MAX_DEVICES) break;
    }

    closedir(dir);
  }

  if (epollctlfail && !ev_count) {
    close(g_epoll_fd);
    g_epoll_fd = -1;
    return -1;
  }

  return 0;
}

int ev_get_epollfd(void) {
    return g_epoll_fd;
}

#ifdef TW_USE_MINUI_WITH_DATA
int ev_add_fd(int fd, ev_callback cb, void* data) {
#else
int ev_add_fd(int fd, ev_callback cb) {
#endif
  if (ev_misc_count == MAX_MISC_FDS || cb == NULL) {
    return -1;
  }

  epoll_event ev;
  ev.events = EPOLLIN | EPOLLWAKEUP;
  ev.data.ptr = static_cast<void*>(&ev_fdinfo[ev_count]);
  int ret = epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, fd, &ev);
  if (!ret) {
    ev_fdinfo[ev_count].fd = fd;
    ev_fdinfo[ev_count].cb = std::move(cb);
#ifdef TW_USE_MINUI_WITH_DATA
    ev_fdinfo[ev_count].data = data;
#endif
    ev_count++;
    ev_misc_count++;
  }

  return ret;
}

void ev_exit(void) {
    while (ev_count > 0) {
        close(ev_fdinfo[--ev_count].fd);
    }
    ev_misc_count = 0;
    ev_dev_count = 0;
    close(g_epoll_fd);
}

int ev_wait(int timeout) {
    npolledevents = epoll_wait(g_epoll_fd, polledevents, ev_count, timeout);
    if (npolledevents <= 0) {
        return -1;
    }
    return 0;
}

void ev_dispatch(void) {
  for (int n = 0; n < npolledevents; n++) {
    fd_info* fdi = static_cast<fd_info*>(polledevents[n].data.ptr);
    const ev_callback& cb = fdi->cb;
    if (cb) {
#ifdef TW_USE_MINUI_WITH_DATA
      cb(fdi->fd, polledevents[n].events, fdi->data);
#else
      cb(fdi->fd, polledevents[n].events);
#endif
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
    return -1;
}

#ifdef TW_USE_MINUI_WITH_DATA
int ev_sync_key_state(ev_set_key_callback set_key_cb, void* data) {
#else
int ev_sync_key_state(const ev_set_key_callback& set_key_cb) {
#endif
  // Use unsigned long to match ioctl's parameter type.
  unsigned long ev_bits[BITS_TO_LONGS(EV_MAX)];    // NOLINT
  unsigned long key_bits[BITS_TO_LONGS(KEY_MAX)];  // NOLINT

  for (size_t i = 0; i < ev_dev_count; ++i) {
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
#ifdef TW_USE_MINUI_WITH_DATA
        set_key_cb(code, 1, data);
#else
        set_key_cb(code, 1);
#endif
      }
    }
  }

  return 0;
}

void ev_iterate_available_keys(const std::function<void(int)>& f) {
    // Use unsigned long to match ioctl's parameter type.
    unsigned long ev_bits[BITS_TO_LONGS(EV_MAX)]; // NOLINT
    unsigned long key_bits[BITS_TO_LONGS(KEY_MAX)]; // NOLINT

    for (size_t i = 0; i < ev_dev_count; ++i) {
        memset(ev_bits, 0, sizeof(ev_bits));
        memset(key_bits, 0, sizeof(key_bits));

        // Does this device even have keys?
        if (ioctl(ev_fdinfo[i].fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) == -1) {
            continue;
        }
        if (!test_bit(EV_KEY, ev_bits)) {
            continue;
        }

        int rc = ioctl(ev_fdinfo[i].fd, EVIOCGBIT(EV_KEY, KEY_MAX), key_bits);
        if (rc == -1) {
            continue;
        }

        for (int key_code = 0; key_code <= KEY_MAX; ++key_code) {
            if (test_bit(key_code, key_bits)) {
                f(key_code);
            }
        }
    }
}

#ifdef TW_USE_MINUI_WITH_OPTIONAL_TOUCH_EVENTS
void ev_iterate_touch_inputs(const std::function<void(int)>& action) {
  for (size_t i = 0; i < ev_dev_count; ++i) {
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
#endif
