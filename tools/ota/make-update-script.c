/*
 * Copyright (C) 2008 The Android Open Source Project
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

#include "private/android_filesystem_config.h"

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

/*
 * Recursively walk the directory tree at <sysdir>/<subdir>, writing
 * script commands to set permissions and create symlinks.
 * Assume the contents already have the specified default permissions,
 * so only output commands if they need to be changed from the defaults.
 *
 * Note that permissions are set by fs_config(), which uses a lookup table of
 * Android permissions.  They are not drawn from the build host filesystem.
 */
static void walk_files(
        const char *sysdir, const char *subdir,
        unsigned default_uid, unsigned default_gid,
        unsigned default_dir_mode, unsigned default_file_mode) {
    const char *sep = strcmp(subdir, "") ? "/" : "";

    char fn[PATH_MAX];
    unsigned dir_uid = 0, dir_gid = 0, dir_mode = 0;
    snprintf(fn, PATH_MAX, "system%s%s", sep, subdir);
    fs_config(fn, 1, &dir_uid, &dir_gid, &dir_mode);

    snprintf(fn, PATH_MAX, "%s%s%s", sysdir, sep, subdir);
    DIR *dir = opendir(fn);
    if (dir == NULL) {
        perror(fn);
        exit(1);
    }

    /*
     * We can use "set_perm" and "set_perm_recursive" to set file permissions
     * (owner, group, and file mode) for individual files and entire subtrees.
     * We want to use set_perm_recursive efficiently to avoid setting the
     * permissions of every single file in the system image individually.
     *
     * What we do is recursively set our entire subtree to the permissions
     * used by the first file we encounter, and then use "set_perm" to adjust
     * the permissions of subsequent files which don't match the first one.
     * This is bad if the first file is an outlier, but it generally works.
     * Subdirectories can do the same thing recursively if they're different.
     */

    int is_first = 1;
    const struct dirent *e;
    while ((e = readdir(dir))) {
        // Skip over "." and ".." entries
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;

        if (e->d_type == DT_LNK) {  // Symlink

            // Symlinks don't really have permissions, so this is orthogonal.
            snprintf(fn, PATH_MAX, "%s/%s%s%s", sysdir, subdir, sep, e->d_name);
            int len = readlink(fn, fn, PATH_MAX - 1);
            if (len <= 0) {
                perror(fn);
                exit(1);
            }
            fn[len] = '\0';
            printf("symlink %s SYSTEM:%s%s%s\n", fn, subdir, sep, e->d_name);

        } else if (e->d_type == DT_DIR) {  // Subdirectory

            // Use the parent directory as the model for default permissions.
            // We haven't seen a file, so just make up some file defaults.
            if (is_first && (
                    dir_mode != default_dir_mode ||
                    dir_uid != default_uid || dir_gid != default_gid)) {
                default_uid = dir_uid;
                default_gid = dir_gid;
                default_dir_mode = dir_mode;
                default_file_mode = dir_mode & default_file_mode & 0666;
                printf("set_perm_recursive %d %d 0%o 0%o SYSTEM:%s\n",
                         default_uid, default_gid,
                         default_dir_mode, default_file_mode,
                         subdir);
            }

            is_first = 0;

            // Recursively handle the subdirectory.
            // Note, the recursive call handles the directory's own permissions.
            snprintf(fn, PATH_MAX, "%s%s%s", subdir, sep, e->d_name);
            walk_files(sysdir, fn,
                    default_uid, default_gid,
                    default_dir_mode, default_file_mode);

        } else {  // Ordinary file

            // Get the file's desired permissions.
            unsigned file_uid = 0, file_gid = 0, file_mode = 0;
            snprintf(fn, PATH_MAX, "system/%s%s%s", subdir, sep, e->d_name);
            fs_config(fn, 0, &file_uid, &file_gid, &file_mode);

            // If this is the first file, its mode gets to become the default.
            if (is_first && (
                    dir_mode != default_dir_mode ||
                    file_mode != default_file_mode ||
                    dir_uid != default_uid || file_uid != default_uid ||
                    dir_gid != default_gid || file_gid != default_gid)) {
                default_uid = dir_uid;
                default_gid = dir_gid;
                default_dir_mode = dir_mode;
                default_file_mode = file_mode;
                printf("set_perm_recursive %d %d 0%o 0%o SYSTEM:%s\n",
                         default_uid, default_gid,
                         default_dir_mode, default_file_mode,
                         subdir);
            }

            is_first = 0;

            // Otherwise, override this file if it doesn't match the defaults.
            if (file_mode != default_file_mode ||
                file_uid != default_uid || file_gid != default_gid) {
                printf("set_perm %d %d 0%o SYSTEM:%s%s%s\n",
                         file_uid, file_gid, file_mode,
                         subdir, sep, e->d_name);
            }

        }
    }

    // Set the directory's permissions directly, if they never got set.
    if (dir_mode != default_dir_mode ||
        dir_uid != default_uid || dir_gid != default_gid) {
        printf("set_perm %d %d 0%o SYSTEM:%s\n",
                dir_uid, dir_gid, dir_mode, subdir);
    }

    closedir(dir);
}

/*
 * Generate the update script (in "Amend", see commands/recovery/commands.c)
 * for the complete-reinstall OTA update packages the build system makes.
 *
 * The generated script makes a variety of sanity checks about the device,
 * erases and reinstalls system files, and sets file permissions appropriately.
 */
int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s systemdir android-info.txt >update-script\n",
                argv[0]);
        return 2;
    }

    // ensure basic recovery script language compatibility
    printf("assert compatible_with(\"0.2\") == \"true\"\n");

    // if known, make sure the device name is correct
    const char *device = getenv("TARGET_DEVICE");
    if (device != NULL) {
        printf("assert getprop(\"ro.product.device\") == \"%s\" || "
                "getprop(\"ro.build.product\") == \"%s\"\n", device, device);
    }

    // scan android-info.txt to enforce compatibility with the target system
    FILE *fp = fopen(argv[2], "r");
    if (fp == NULL) {
        perror(argv[2]);
        return 1;
    }

    // The lines we're looking for look like:
    //     version-bootloader=x.yy.zzzz|x.yy.zzzz|...
    // or:
    //     require version-bootloader=x.yy.zzzz|x.yy.zzzz|...
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        const char *name = strtok(line, "="), *value = strtok(NULL, "|\n");
        if (value != NULL &&
            (!strcmp(name, "version-bootloader") ||
             !strcmp(name, "require version-bootloader"))) {
            printf("assert getprop(\"ro.bootloader\") == \"%s\"", value);

            while ((value = strtok(NULL, "|\n")) != NULL) {
              printf(" || getprop(\"ro.bootloader\") == \"%s\"", value);
            }
            printf("\n");
        }
        // We also used to check version-baseband, but we update radio.img
        // ourselves, so there's no need.
    }

    // erase the boot sector first, so if the update gets interrupted,
    // the system will reboot into the recovery partition and start over.
    printf("format BOOT:\n");

    // write the radio image (actually just loads it into RAM for now)
    printf("show_progress 0.1 0\n");
    printf("write_radio_image PACKAGE:radio.img\n");

    // erase and reinstall the system image
    printf("show_progress 0.5 0\n");
    printf("format SYSTEM:\n");
    printf("copy_dir PACKAGE:system SYSTEM:\n");

    // walk the files in the system image, set their permissions, etc.
    // use -1 for default values to force permissions to be set explicitly.
    walk_files(argv[1], "", -1, -1, -1, -1);

    // as the last step, write the boot sector.
    printf("show_progress 0.2 0\n");
    printf("write_raw_image PACKAGE:boot.img BOOT:\n");

    // after the end of the script, the radio will be written to cache
    // leave some space in the progress bar for this operation
    printf("show_progress 0.2 10\n");
    return 0;
}
