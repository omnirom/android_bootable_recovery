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

#include "DirUtil.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <limits.h>

#include <string>

#include <selinux/label.h>
#include <selinux/selinux.h>

typedef enum { DMISSING, DDIR, DILLEGAL } DirStatus;

static DirStatus
getPathDirStatus(const char *path)
{
    struct stat st;
    int err;

    err = stat(path, &st);
    if (err == 0) {
        /* Something's there; make sure it's a directory.
         */
        if (S_ISDIR(st.st_mode)) {
            return DDIR;
        }
        errno = ENOTDIR;
        return DILLEGAL;
    } else if (errno != ENOENT) {
        /* Something went wrong, or something in the path
         * is bad.  Can't do anything in this situation.
         */
        return DILLEGAL;
    }
    return DMISSING;
}

int
dirCreateHierarchy(const char *path, int mode,
        const struct utimbuf *timestamp, bool stripFileName,
        struct selabel_handle *sehnd)
{
    DirStatus ds;

    /* Check for an empty string before we bother
     * making any syscalls.
     */
    if (path[0] == '\0') {
        errno = ENOENT;
        return -1;
    }
    // Allocate a path that we can modify; stick a slash on
    // the end to make things easier.
    std::string cpath = path;
    if (stripFileName) {
        // Strip everything after the last slash.
        size_t pos = cpath.rfind('/');
        if (pos == std::string::npos) {
            errno = ENOENT;
            return -1;
        }
        cpath.resize(pos + 1);
    } else {
        // Make sure that the path ends in a slash.
        cpath.push_back('/');
    }

    /* See if it already exists.
     */
    ds = getPathDirStatus(cpath.c_str());
    if (ds == DDIR) {
        return 0;
    } else if (ds == DILLEGAL) {
        return -1;
    }

    /* Walk up the path from the root and make each level.
     * If a directory already exists, no big deal.
     */
    const char *path_start = &cpath[0];
    char *p = &cpath[0];
    while (*p != '\0') {
        /* Skip any slashes, watching out for the end of the string.
         */
        while (*p != '\0' && *p == '/') {
            p++;
        }
        if (*p == '\0') {
            break;
        }

        /* Find the end of the next path component.
         * We know that we'll see a slash before the NUL,
         * because we added it, above.
         */
        while (*p != '/') {
            p++;
        }
        *p = '\0';

        /* Check this part of the path and make a new directory
         * if necessary.
         */
        ds = getPathDirStatus(path_start);
        if (ds == DILLEGAL) {
            /* Could happen if some other process/thread is
             * messing with the filesystem.
             */
            return -1;
        } else if (ds == DMISSING) {
            int err;

            char *secontext = NULL;

            if (sehnd) {
                selabel_lookup(sehnd, &secontext, path_start, mode);
                setfscreatecon(secontext);
            }

            err = mkdir(path_start, mode);

            if (secontext) {
                freecon(secontext);
                setfscreatecon(NULL);
            }

            if (err != 0) {
                return -1;
            }
            if (timestamp != NULL && utime(path_start, timestamp)) {
                return -1;
            }
        }
        // else, this directory already exists.

        // Repair the path and continue.
        *p = '/';
    }
    return 0;
}

int
dirUnlinkHierarchy(const char *path)
{
    struct stat st;
    DIR *dir;
    struct dirent *de;
    int fail = 0;

    /* is it a file or directory? */
    if (lstat(path, &st) < 0) {
        return -1;
    }

    /* a file, so unlink it */
    if (!S_ISDIR(st.st_mode)) {
        return unlink(path);
    }

    /* a directory, so open handle */
    dir = opendir(path);
    if (dir == NULL) {
        return -1;
    }

    /* recurse over components */
    errno = 0;
    while ((de = readdir(dir)) != NULL) {
        //TODO: don't blow the stack
        char dn[PATH_MAX];
        if (!strcmp(de->d_name, "..") || !strcmp(de->d_name, ".")) {
            continue;
        }
        snprintf(dn, sizeof(dn), "%s/%s", path, de->d_name);
        if (dirUnlinkHierarchy(dn) < 0) {
            fail = 1;
            break;
        }
        errno = 0;
    }
    /* in case readdir or unlink_recursive failed */
    if (fail || errno < 0) {
        int save = errno;
        closedir(dir);
        errno = save;
        return -1;
    }

    /* close directory handle */
    if (closedir(dir) < 0) {
        return -1;
    }

    /* delete target directory */
    return rmdir(path);
}
