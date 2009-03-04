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

#include <sys/stat.h>
#include "roots.h"
#include "common.h"

#define CANARY_FILE "/system/build.prop"
#define CANARY_FILE_ROOT_PATH "SYSTEM:build.prop"

int
file_exists(const char *path)
{
    struct stat st;
    int ret;
    ret = stat(path, &st);
    if (ret == 0) {
        return S_ISREG(st.st_mode);
    }
    return 0;
}

int
test_roots()
{
    int ret;

    /* Make sure that /system isn't mounted yet.
     */
    if (file_exists(CANARY_FILE)) return -__LINE__;
    if (is_root_path_mounted(CANARY_FILE_ROOT_PATH)) return -__LINE__;

    /* Try to mount the root.
     */
    ret = ensure_root_path_mounted(CANARY_FILE_ROOT_PATH);
    if (ret < 0) return -__LINE__;

    /* Make sure we can see the file now and that we know the root is mounted.
     */
    if (!file_exists(CANARY_FILE)) return -__LINE__;
    if (!is_root_path_mounted(CANARY_FILE_ROOT_PATH)) return -__LINE__;

    /* Make sure that the root path corresponds to the regular path.
     */
    struct stat st1, st2;
    char buf[128];
    const char *path = translate_root_path(CANARY_FILE_ROOT_PATH,
            buf, sizeof(buf));
    if (path == NULL) return -__LINE__;
    ret = stat(CANARY_FILE, &st1);
    if (ret != 0) return -__LINE__;
    ret = stat(path, &st2);
    if (ret != 0) return -__LINE__;
    if (st1.st_dev != st2.st_dev || st1.st_ino != st2.st_ino) return -__LINE__;

    /* Try to unmount the root.
     */
    ret = ensure_root_path_unmounted(CANARY_FILE_ROOT_PATH);
    if (ret < 0) return -__LINE__;

    /* Make sure that we can't see the file anymore and that
     * we don't think the root is mounted.
     */
    if (file_exists(CANARY_FILE)) return -__LINE__;
    if (is_root_path_mounted(CANARY_FILE_ROOT_PATH)) return -__LINE__;

    return 0;
}
