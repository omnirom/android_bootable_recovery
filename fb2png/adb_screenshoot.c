/*
 *   -- http://android-fb2png.googlecode.com/svn/trunk/adb_screenshoot.c --
 *
 *   Copyright 2011, Kyan He <kyan.ql.he@gmail.com>
 *
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>

#include "fb.h"
#include "log.h"

#define DEFAULT_SAVE_PATH "fbdump.png"

/* defined in $T/system/core/adb/framebuffer_service.c */
#define DDMS_RAWIMAGE_VERSION 1
struct fbinfo {
    unsigned int version;
    unsigned int bpp;
    unsigned int size;
    unsigned int width;
    unsigned int height;
    unsigned int red_offset;
    unsigned int red_length;
    unsigned int blue_offset;
    unsigned int blue_length;
    unsigned int green_offset;
    unsigned int green_length;
    unsigned int alpha_offset;
    unsigned int alpha_length;
} __attribute__((packed));

static int remote_socket(const char *host, int port)
{
    struct sockaddr_in sa;
    struct hostent *hp;
    int s;

    if(!(hp = gethostbyname(host))){ return -1; }

    memset(&sa, 0, sizeof(sa));
    sa.sin_port = htons(port);
    sa.sin_family = hp->h_addrtype;
    memcpy((void*) &sa.sin_addr, (void*) hp->h_addr, hp->h_length);

    if((s = socket(hp->h_addrtype, SOCK_STREAM, 0)) < 0) {
        return -1;
    }

    if(connect(s, (struct sockaddr*) &sa, sizeof(sa)) != 0){
        close(s);
        return -1;
    }

    return s;
}

char *target = "usb";
static int adb_fd;

/**
 * Write command through adb protocol.
 * Return
 *      Bytes have been wrote.
 */
static int adb_write(const char *cmd)
{
    char buf[1024];
    int sz;

    /* Construct command. */
    sz = sprintf(buf, "%04x%s", strlen(cmd), cmd);

    write(adb_fd, buf, sz);

#if 0
    D("<< %s", buf);
#endif
    return sz;
}

/**
 * Read data through adb protocol.
 * Return
 *      Bytes have been read.
 */
static int adb_read(char *buf, int sz)
{
    sz = read(adb_fd, buf, sz);
    if (sz < 0) {
        E("Fail to read from adb socket, %s", strerror(errno));
    }
    buf[sz] = '\0';
#if 0
    D(">> %d", sz);
#endif
    return sz;
}

static int get_fb_from_adb(struct fb *fb)
{
    char buf[1024];
    const struct fbinfo* fbinfo;

    /* Init socket */
    adb_fd = remote_socket("localhost", 5037);
    if (adb_fd < 0) {
        E("Fail to create socket, %s", strerror(errno));
    }

    adb_write("host:transport-");
    adb_read(buf, 1024);

    adb_write("framebuffer:");
    adb_read(buf, 1024);

    /* Parse FB header. */
    adb_read(buf, sizeof(struct fbinfo));
    fbinfo = (struct fbinfo*) buf;

    if (fbinfo->version != DDMS_RAWIMAGE_VERSION) {
        E("unspport adb version");
    }

    /* Assemble struct fb */
    memcpy(fb, &fbinfo->bpp, sizeof(struct fbinfo) - 4);
    fb_dump(fb);

    fb->data = malloc(fb->size);
    if (!fb->data) return -1;

    /* Read out the whole framebuffer */
    int bytes_read = 0;
    while (bytes_read < fb->size) {
        bytes_read += adb_read(fb->data + bytes_read, fb->size - bytes_read);
    }

    return 0;
}

int fb2png(const char* path)
{
    struct fb fb;

    if (get_fb_from_adb(&fb)) {
        D("cannot get framebuffer.");
        return -1;
    }

    return fb_save_png(&fb, path);
}

int main(int argc, char *argv[])
{
    char fn[128];

    if (argc == 2) {
        //if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
        if (argv[1][0] == '-') {
            printf(
                "Usage: fb2png [path/to/output.png]\n"
                "    The default output path is ./fbdump.png\n"
                );
            exit(0);
        } else {
            sprintf(fn, "%s", argv[1]);
        }
    } else {
        sprintf(fn, "%s", DEFAULT_SAVE_PATH);
    }

    return fb2png(fn);
}
