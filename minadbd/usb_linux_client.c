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
#include <unistd.h>
#include <string.h>

#include <linux/usb/ch9.h>
#include <linux/usb/functionfs.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>

#include "sysdeps.h"

#define   TRACE_TAG  TRACE_USB
#include "adb.h"

#define MAX_PACKET_SIZE_FS	64
#define MAX_PACKET_SIZE_HS	512

#define cpu_to_le16(x)  htole16(x)
#define cpu_to_le32(x)  htole32(x)

struct usb_handle
{
    int fd;
    adb_cond_t notify;
    adb_mutex_t lock;

    int (*write)(usb_handle *h, const void *data, int len);
    int (*read)(usb_handle *h, void *data, int len);
    void (*kick)(usb_handle *h);

    int control;
    int bulk_out; /* "out" from the host's perspective => source for adbd */
    int bulk_in;  /* "in" from the host's perspective => sink for adbd */
};

static const struct {
    struct usb_functionfs_descs_head header;
    struct {
        struct usb_interface_descriptor intf;
        struct usb_endpoint_descriptor_no_audio source;
        struct usb_endpoint_descriptor_no_audio sink;
    } __attribute__((packed)) fs_descs, hs_descs;
} __attribute__((packed)) descriptors = {
    .header = {
        .magic = cpu_to_le32(FUNCTIONFS_DESCRIPTORS_MAGIC),
        .length = cpu_to_le32(sizeof(descriptors)),
        .fs_count = 3,
        .hs_count = 3,
    },
    .fs_descs = {
        .intf = {
            .bLength = sizeof(descriptors.fs_descs.intf),
            .bDescriptorType = USB_DT_INTERFACE,
            .bInterfaceNumber = 0,
            .bNumEndpoints = 2,
            .bInterfaceClass = ADB_CLASS,
            .bInterfaceSubClass = ADB_SUBCLASS,
            .bInterfaceProtocol = ADB_PROTOCOL,
            .iInterface = 1, /* first string from the provided table */
        },
        .source = {
            .bLength = sizeof(descriptors.fs_descs.source),
            .bDescriptorType = USB_DT_ENDPOINT,
            .bEndpointAddress = 1 | USB_DIR_OUT,
            .bmAttributes = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize = MAX_PACKET_SIZE_FS,
        },
        .sink = {
            .bLength = sizeof(descriptors.fs_descs.sink),
            .bDescriptorType = USB_DT_ENDPOINT,
            .bEndpointAddress = 2 | USB_DIR_IN,
            .bmAttributes = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize = MAX_PACKET_SIZE_FS,
        },
    },
    .hs_descs = {
        .intf = {
            .bLength = sizeof(descriptors.hs_descs.intf),
            .bDescriptorType = USB_DT_INTERFACE,
            .bInterfaceNumber = 0,
            .bNumEndpoints = 2,
            .bInterfaceClass = ADB_CLASS,
            .bInterfaceSubClass = ADB_SUBCLASS,
            .bInterfaceProtocol = ADB_PROTOCOL,
            .iInterface = 1, /* first string from the provided table */
        },
        .source = {
            .bLength = sizeof(descriptors.hs_descs.source),
            .bDescriptorType = USB_DT_ENDPOINT,
            .bEndpointAddress = 1 | USB_DIR_OUT,
            .bmAttributes = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize = MAX_PACKET_SIZE_HS,
        },
        .sink = {
            .bLength = sizeof(descriptors.hs_descs.sink),
            .bDescriptorType = USB_DT_ENDPOINT,
            .bEndpointAddress = 2 | USB_DIR_IN,
            .bmAttributes = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize = MAX_PACKET_SIZE_HS,
        },
    },
};

#define STR_INTERFACE_ "ADB Interface"

static const struct {
    struct usb_functionfs_strings_head header;
    struct {
        __le16 code;
        const char str1[sizeof(STR_INTERFACE_)];
    } __attribute__((packed)) lang0;
} __attribute__((packed)) strings = {
    .header = {
        .magic = cpu_to_le32(FUNCTIONFS_STRINGS_MAGIC),
        .length = cpu_to_le32(sizeof(strings)),
        .str_count = cpu_to_le32(1),
        .lang_count = cpu_to_le32(1),
    },
    .lang0 = {
        cpu_to_le16(0x0409), /* en-us */
        STR_INTERFACE_,
    },
};

void usb_cleanup()
{
    // nothing to do here
}

static void *usb_adb_open_thread(void *x)
{
    struct usb_handle *usb = (struct usb_handle *)x;
    int fd;

    while (1) {
        // wait until the USB device needs opening
        adb_mutex_lock(&usb->lock);
        while (usb->fd != -1)
            adb_cond_wait(&usb->notify, &usb->lock);
        adb_mutex_unlock(&usb->lock);

        D("[ usb_thread - opening device ]\n");
        do {
            /* XXX use inotify? */
            fd = unix_open("/dev/android_adb", O_RDWR);
            if (fd < 0) {
                // to support older kernels
                fd = unix_open("/dev/android", O_RDWR);
                fprintf(stderr, "usb_adb_open_thread: %d\n", fd );
            }
            if (fd < 0) {
                adb_sleep_ms(1000);
            }
        } while (fd < 0);
        D("[ opening device succeeded ]\n");

        close_on_exec(fd);
        usb->fd = fd;

        D("[ usb_thread - registering device ]\n");
        register_usb_transport(usb, 0, 1);
    }

    // never gets here
    return 0;
}

static int usb_adb_write(usb_handle *h, const void *data, int len)
{
    int n;

    D("about to write (fd=%d, len=%d)\n", h->fd, len);
    n = adb_write(h->fd, data, len);
    if(n != len) {
        D("ERROR: fd = %d, n = %d, errno = %d (%s)\n",
            h->fd, n, errno, strerror(errno));
        return -1;
    }
    D("[ done fd=%d ]\n", h->fd);
    return 0;
}

static int usb_adb_read(usb_handle *h, void *data, int len)
{
    int n;

    D("about to read (fd=%d, len=%d)\n", h->fd, len);
    n = adb_read(h->fd, data, len);
    if(n != len) {
        D("ERROR: fd = %d, n = %d, errno = %d (%s)\n",
            h->fd, n, errno, strerror(errno));
        return -1;
    }
    D("[ done fd=%d ]\n", h->fd);
    return 0;
}

static void usb_adb_kick(usb_handle *h)
{
    D("usb_kick\n");
    adb_mutex_lock(&h->lock);
    adb_close(h->fd);
    h->fd = -1;

    // notify usb_adb_open_thread that we are disconnected
    adb_cond_signal(&h->notify);
    adb_mutex_unlock(&h->lock);
}

static void usb_adb_init()
{
    usb_handle *h;
    adb_thread_t tid;
    int fd;

    h = calloc(1, sizeof(usb_handle));

    h->write = usb_adb_write;
    h->read = usb_adb_read;
    h->kick = usb_adb_kick;
    h->fd = -1;

    adb_cond_init(&h->notify, 0);
    adb_mutex_init(&h->lock, 0);

    fprintf(stderr, "Starting to open usb_init()\n");
    // Open the file /dev/android_adb_enable to trigger 
    // the enabling of the adb USB function in the kernel.
    // We never touch this file again - just leave it open
    // indefinitely so the kernel will know when we are running
    // and when we are not.
    fd = unix_open("/dev/android_adb_enable", O_RDWR);
    fprintf(stderr, "unix_open to open usb_init(): %d\n", fd);
    if (fd < 0) {
       D("failed to open /dev/android_adb_enable\n");
    } else {
        close_on_exec(fd);
    }

    D("[ usb_init - starting thread ]\n");
    if(adb_thread_create(&tid, usb_adb_open_thread, h)){
        fatal_errno("cannot create usb thread");
        fprintf(stderr, "cannot create the usb thread()\n");
    }
}


static void init_functionfs(struct usb_handle *h)
{
    ssize_t ret;

    D("OPENING %s\n", USB_FFS_ADB_EP0);
    h->control = adb_open(USB_FFS_ADB_EP0, O_RDWR);
    if (h->control < 0) {
        D("[ %s: cannot open control endpoint: errno=%d]\n", USB_FFS_ADB_EP0, errno);
        goto err;
    }

    ret = adb_write(h->control, &descriptors, sizeof(descriptors));
    if (ret < 0) {
        D("[ %s: write descriptors failed: errno=%d ]\n", USB_FFS_ADB_EP0, errno);
        goto err;
    }

    ret = adb_write(h->control, &strings, sizeof(strings));
    if (ret < 0) {
        D("[ %s: writing strings failed: errno=%d]\n", USB_FFS_ADB_EP0, errno);
        goto err;
    }

    h->bulk_out = adb_open(USB_FFS_ADB_OUT, O_RDWR);
    if (h->bulk_out < 0) {
        D("[ %s: cannot open bulk-out ep: errno=%d ]\n", USB_FFS_ADB_OUT, errno);
        goto err;
    }

    h->bulk_in = adb_open(USB_FFS_ADB_IN, O_RDWR);
    if (h->bulk_in < 0) {
        D("[ %s: cannot open bulk-in ep: errno=%d ]\n", USB_FFS_ADB_IN, errno);
        goto err;
    }

    return;

err:
    if (h->bulk_in > 0) {
        adb_close(h->bulk_in);
        h->bulk_in = -1;
    }
    if (h->bulk_out > 0) {
        adb_close(h->bulk_out);
        h->bulk_out = -1;
    }
    if (h->control > 0) {
        adb_close(h->control);
        h->control = -1;
    }
    return;
}

static void *usb_ffs_open_thread(void *x)
{
    struct usb_handle *usb = (struct usb_handle *)x;

    while (1) {
        // wait until the USB device needs opening
        adb_mutex_lock(&usb->lock);
        while (usb->control != -1)
            adb_cond_wait(&usb->notify, &usb->lock);
        adb_mutex_unlock(&usb->lock);

        while (1) {
            init_functionfs(usb);

            if (usb->control >= 0)
                break;

            adb_sleep_ms(1000);
        }

        D("[ usb_thread - registering device ]\n");
        register_usb_transport(usb, 0, 1);
    }

    // never gets here
    return 0;
}

static int bulk_write(int bulk_in, const char *buf, size_t length)
{
    size_t count = 0;
    int ret;

    do {
        ret = adb_write(bulk_in, buf + count, length - count);
        if (ret < 0) {
            if (errno != EINTR)
                return ret;
        } else {
            count += ret;
        }
    } while (count < length);

    D("[ bulk_write done fd=%d ]\n", bulk_in);
    return count;
}

static int usb_ffs_write(usb_handle *h, const void *data, int len)
{
    int n;

    D("about to write (fd=%d, len=%d)\n", h->bulk_in, len);
    n = bulk_write(h->bulk_in, data, len);
    if (n != len) {
        D("ERROR: fd = %d, n = %d, errno = %d (%s)\n",
            h->bulk_in, n, errno, strerror(errno));
        return -1;
    }
    D("[ done fd=%d ]\n", h->bulk_in);
    return 0;
}

static int bulk_read(int bulk_out, char *buf, size_t length)
{
    size_t count = 0;
    int ret;

    do {
        ret = adb_read(bulk_out, buf + count, length - count);
        if (ret < 0) {
            if (errno != EINTR) {
                D("[ bulk_read failed fd=%d length=%zu count=%zu ]\n",
                                           bulk_out, length, count);
                return ret;
            }
        } else {
            count += ret;
        }
    } while (count < length);

    return count;
}

static int usb_ffs_read(usb_handle *h, void *data, int len)
{
    int n;

    D("about to read (fd=%d, len=%d)\n", h->bulk_out, len);
    n = bulk_read(h->bulk_out, data, len);
    if (n != len) {
        D("ERROR: fd = %d, n = %d, errno = %d (%s)\n",
            h->bulk_out, n, errno, strerror(errno));
        return -1;
    }
    D("[ done fd=%d ]\n", h->bulk_out);
    return 0;
}

static void usb_ffs_kick(usb_handle *h)
{
    int err;

    err = ioctl(h->bulk_in, FUNCTIONFS_CLEAR_HALT);
    if (err < 0)
        D("[ kick: source (fd=%d) clear halt failed (%d) ]", h->bulk_in, errno);

    err = ioctl(h->bulk_out, FUNCTIONFS_CLEAR_HALT);
    if (err < 0)
        D("[ kick: sink (fd=%d) clear halt failed (%d) ]", h->bulk_out, errno);

    adb_mutex_lock(&h->lock);
    adb_close(h->control);
    adb_close(h->bulk_out);
    adb_close(h->bulk_in);
    h->control = h->bulk_out = h->bulk_in = -1;

    // notify usb_ffs_open_thread that we are disconnected
    adb_cond_signal(&h->notify);
    adb_mutex_unlock(&h->lock);
}

static void usb_ffs_init()
{
    usb_handle *h;
    adb_thread_t tid;

    D("[ usb_init - using FunctionFS ]\n");

    h = calloc(1, sizeof(usb_handle));

    h->write = usb_ffs_write;
    h->read = usb_ffs_read;
    h->kick = usb_ffs_kick;

    h->control  = -1;
    h->bulk_out = -1;
    h->bulk_out = -1;

    adb_cond_init(&h->notify, 0);
    adb_mutex_init(&h->lock, 0);

    D("[ usb_init - starting thread ]\n");
    if (adb_thread_create(&tid, usb_ffs_open_thread, h)){
        fatal_errno("[ cannot create usb thread ]\n");
    }
}

void usb_init()
{
    if (access(USB_FFS_ADB_EP0, F_OK) == 0)
        usb_ffs_init();
    else
        usb_adb_init();
}

int usb_write(usb_handle *h, const void *data, int len)
{
    return h->write(h, data, len);
}

int usb_read(usb_handle *h, void *data, int len)
{
    return h->read(h, data, len);
}
int usb_close(usb_handle *h)
{
    // nothing to do here
    return 0;
}

void usb_kick(usb_handle *h)
{
    h->kick(h);
}
