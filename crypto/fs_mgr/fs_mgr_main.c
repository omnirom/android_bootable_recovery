/*
 * Copyright (C) 2012 The Android Open Source Project
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
#include <string.h>
#include <stdlib.h>
#include <libgen.h>
#include "fs_mgr_priv.h"

char *me = "";

static void usage(void)
{
    ERROR("%s: usage: %s <-a | -n mnt_point blk_dev | -u> <fstab_file>\n", me, me);
    exit(1);
}

/* Parse the command line.  If an error is encountered, print an error message
 * and exit the program, do not return to the caller.
 * Return the number of argv[] entries consumed.
 */
static void parse_options(int argc, char *argv[], int *a_flag, int *u_flag, int *n_flag,
                     char **n_name, char **n_blk_dev)
{
    me = basename(strdup(argv[0]));

    if (argc <= 1) {
        usage();
    }

    if (!strcmp(argv[1], "-a")) {
        if (argc != 3) {
            usage();
        }
        *a_flag = 1;
    }
    if (!strcmp(argv[1], "-n")) {
        if (argc != 5) {
            usage();
        }
        *n_flag = 1;
        *n_name = argv[2];
        *n_blk_dev = argv[3];
    }
    if (!strcmp(argv[1], "-u")) {
        if (argc != 3) {
            usage();
        }
        *u_flag = 1;
    }

    /* If no flag is specified, it's an error */
    if (!(*a_flag | *n_flag | *u_flag)) {
        usage();
    }

    /* If more than one flag is specified, it's an error */
    if ((*a_flag + *n_flag + *u_flag) > 1) {
        usage();
    }

    return;
}

int main(int argc, char *argv[])
{
    int a_flag=0;
    int u_flag=0;
    int n_flag=0;
    char *n_name;
    char *n_blk_dev;
    char *fstab;

    klog_init();
    klog_set_level(6);

    parse_options(argc, argv, &a_flag, &u_flag, &n_flag, &n_name, &n_blk_dev);

    /* The name of the fstab file is last, after the option */
    fstab = argv[argc - 1];

    if (a_flag) {
        return fs_mgr_mount_all(fstab);
    } else if (n_flag) {
        return fs_mgr_do_mount(fstab, n_name, n_blk_dev, 0);
    } else if (u_flag) {
        return fs_mgr_unmount_all(fstab);
    } else {
        ERROR("%s: Internal error, unknown option\n", me);
        exit(1);
    }

    /* Should not get here */
    exit(1);
}

