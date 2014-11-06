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

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/poll.h>

#include <linux/input.h>

#include "minui.h"

#define MAX_DEVICES 16
#define MAX_MISC_FDS 16

#define BITS_PER_LONG (sizeof(unsigned long) * 8)
#define BITS_TO_LONGS(x) (((x) + BITS_PER_LONG - 1) / BITS_PER_LONG)

#define test_bit(bit, array) \
    ((array)[(bit)/BITS_PER_LONG] & (1 << ((bit) % BITS_PER_LONG)))

struct fd_info {
    ev_callback cb;
    void *data;
};

static struct pollfd ev_fds[MAX_DEVICES + MAX_MISC_FDS];
static struct fd_info ev_fdinfo[MAX_DEVICES + MAX_MISC_FDS];

static unsigned ev_count = 0;
static unsigned ev_dev_count = 0;
static unsigned ev_misc_count = 0;

int ev_init(ev_callback input_cb, void *data)
{
    DIR *dir;
    struct dirent *de;
    int fd;

    dir = opendir("/dev/input");
    if(dir != 0) {
        while((de = readdir(dir))) {
            unsigned long ev_bits[BITS_TO_LONGS(EV_MAX)];

//            fprintf(stderr,"/dev/input/%s\n", de->d_name);
            if(strncmp(de->d_name,"event",5)) continue;
            fd = openat(dirfd(dir), de->d_name, O_RDONLY);
            if(fd < 0) continue;

            /* read the evbits of the input device */
            if (ioctl(fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) < 0) {
                close(fd);
                continue;
            }

            /* TODO: add ability to specify event masks. For now, just assume
             * that only EV_KEY and EV_REL event types are ever needed. */
            if (!test_bit(EV_KEY, ev_bits) && !test_bit(EV_REL, ev_bits)) {
                close(fd);
                continue;
            }

            ev_fds[ev_count].fd = fd;
            ev_fds[ev_count].events = POLLIN;
            ev_fdinfo[ev_count].cb = input_cb;
            ev_fdinfo[ev_count].data = data;
            ev_count++;
            ev_dev_count++;
            if(ev_dev_count == MAX_DEVICES) break;
        }
    }

    return 0;
}

int ev_add_fd(int fd, ev_callback cb, void *data)
{
    if (ev_misc_count == MAX_MISC_FDS || cb == NULL)
        return -1;

    ev_fds[ev_count].fd = fd;
    ev_fds[ev_count].events = POLLIN;
    ev_fdinfo[ev_count].cb = cb;
    ev_fdinfo[ev_count].data = data;
    ev_count++;
    ev_misc_count++;
    return 0;
}

void ev_exit(void)
{
    while (ev_count > 0) {
        close(ev_fds[--ev_count].fd);
    }
    ev_misc_count = 0;
    ev_dev_count = 0;
}

int ev_wait(int timeout)
{
    int r;

    r = poll(ev_fds, ev_count, timeout);
    if (r <= 0)
        return -1;
    return 0;
}

void ev_dispatch(void)
{
    unsigned n;
    int ret;

    for (n = 0; n < ev_count; n++) {
        ev_callback cb = ev_fdinfo[n].cb;
        if (cb && (ev_fds[n].revents & ev_fds[n].events))
            cb(ev_fds[n].fd, ev_fds[n].revents, ev_fdinfo[n].data);
    }
}

int ev_get_input(int fd, short revents, struct input_event *ev)
{
    int r;

    if (revents & POLLIN) {
        r = read(fd, ev, sizeof(*ev));
        if (r == sizeof(*ev))
            return 0;
    }
    return -1;
}

int ev_sync_key_state(ev_set_key_callback set_key_cb, void *data)
{
    unsigned long key_bits[BITS_TO_LONGS(KEY_MAX)];
    unsigned long ev_bits[BITS_TO_LONGS(EV_MAX)];
    unsigned i;
    int ret;

    for (i = 0; i < ev_dev_count; i++) {
        int code;

        memset(key_bits, 0, sizeof(key_bits));
        memset(ev_bits, 0, sizeof(ev_bits));

        ret = ioctl(ev_fds[i].fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits);
        if (ret < 0 || !test_bit(EV_KEY, ev_bits))
            continue;

        ret = ioctl(ev_fds[i].fd, EVIOCGKEY(sizeof(key_bits)), key_bits);
        if (ret < 0)
            continue;

        for (code = 0; code <= KEY_MAX; code++) {
            if (test_bit(code, key_bits))
                set_key_cb(code, 1, data);
        }
    }

    return 0;
}
