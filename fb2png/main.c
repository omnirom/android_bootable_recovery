/**
 * fb2png  Save screenshot into .png.
 *
 * Copyright (C) 2012  Kyan <kyan.ql.he@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fb2png.h"

#ifdef ANDROID
    #define DEFAULT_SAVE_PATH "/data/local/fbdump.png"
#else
    #define DEFAULT_SAVE_PATH "fbdump.png"
#endif

int main(int argc, char *argv[])
{
    char fn[PATH_MAX];
    int ret;

    if (argc == 2 && argv[1][0] != '-') {
        if (strlen(argv[1]) >= PATH_MAX) {
            printf("Output path is too long!\n");
            exit(-1);
        }
        sprintf(fn, "%s", argv[1]);
    } else if (argc == 1) {
        sprintf(fn, "%s", DEFAULT_SAVE_PATH);
    } else {
        //if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))
        printf(
            "Android Screenshooter - fb2png\n"
            "Author: Kyan He <kyan.ql.he@gmail.com>\n"
            "Modified by  Phil3759 & McKael @xda\n"
            "Base version 0.0.2 ---> v0.0.5  <2013>\n"
            "Usage: fb2png [path/to/output.png]\n"
            "    The default output path is /data/local/fbdump.png\n"
            );
        exit(0);
    }

    if (0 == (ret = fb2png(fn)))
        printf("Saved image to %s\n", fn);

    exit(ret);
}
