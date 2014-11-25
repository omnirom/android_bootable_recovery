/*
 * Copyright (C) 2013 The CyanogenMod Project
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

#ifndef _RECOVERY_CMDS_H
#define _RECOVERY_CMDS_H

#include <stdio.h>
#include <string.h>

int minizip_main(int argc, char **argv);
int make_ext4fs_main(int argc, char **argv);
int reboot_main(int argc, char **argv);
int poweroff_main(int argc, char **argv);
int setprop_main(int argc, char **argv);
int getprop_main(int argc, char **argv);
int fsck_msdos_main(int argc, char **argv);
int newfs_msdos_main(int argc, char **argv);
int vdc_main(int argc, char **argv);
int pigz_main(int argc, char **argv);
int sdcard_main(int argc, char **argv);
int start_main(int argc, char **argv);
int stop_main(int argc, char **argv);
#ifdef USE_F2FS
int make_f2fs_main(int argc, char **argv);
int fsck_f2fs_main(int argc, char **argv);
int fibmap_main(int argc, char **argv);
#endif

int busybox_driver(int argc, char **argv);

struct recovery_cmd {
    const char *name;
    int (*main_func)(int argc, char **argv);
};

static const struct recovery_cmd recovery_cmds[] = {
    { "minizip",        minizip_main },
    { "make_ext4fs",    make_ext4fs_main },
    { "reboot",         reboot_main },
    { "poweroff",       reboot_main },
    { "setprop",        setprop_main },
    { "getprop",        getprop_main },
    { "fsck_msdos",     fsck_msdos_main },
    { "newfs_msdos",    newfs_msdos_main },
    { "vdc",            vdc_main },
    { "pigz",           pigz_main },
    { "sdcard",         sdcard_main },
    { "start",          start_main },
    { "stop",           stop_main },
#ifdef USE_F2FS
    { "mkfs.f2fs",      make_f2fs_main },
    { "fsck.f2fs",      fsck_f2fs_main },
    { "fibmap.f2fs",    fibmap_main },
#endif
    { NULL, NULL },
};

struct recovery_cmd get_command(char* command) {
    int i;

    for (i = 0; recovery_cmds[i].name; i++) {
        if (strcmp(command, recovery_cmds[i].name) == 0)
            break;
    }

    return recovery_cmds[i];
}
#endif
