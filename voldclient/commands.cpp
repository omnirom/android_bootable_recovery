/*
 * Copyright (c) 2013 The CyanogenMod Project
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

#include "voldclient.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "Volume.h"

#include "common.h"

int vold_update_volumes() {

    const char *cmd[2] = {"volume", "list"};
    return vold_command(2, cmd, 1);
}

int vold_mount_volume(const char* path, bool wait) {

    const char *cmd[3] = { "volume", "mount", path };
    int state = vold_get_volume_state(path);

    if (state == Volume::State_Mounted) {
        LOGI("Volume %s already mounted\n", path);
        return 0;
    }

    if (state != Volume::State_Idle) {
        LOGI("Volume %s is not idle, current state is %d\n", path, state);
        return -1;
    }

    if (access(path, R_OK) != 0) {
        mkdir(path, 0000);
        chown(path, 1000, 1000);
    }
    return vold_command(3, cmd, wait);
}

int vold_mount_auto_volume(const char* label, bool wait) {
    char path[80];
    sprintf(path, "/storage/%s", label);
    const char *cmd[3] = { "volume", "mount", label };
    int state = vold_get_volume_state(path);

    if (state == Volume::State_Mounted) {
        LOGI("Volume %s already mounted\n", path);
        return 0;
    }

    if (state != Volume::State_Idle) {
        LOGI("Volume %s is not idle, current state is %d\n", path, state);
        return -1;
    }

    if (access(path, R_OK) != 0) {
        mkdir(path, 0000);
        chown(path, 1000, 1000);
    }
    return vold_command(3, cmd, wait);
}

int vold_unmount_volume(const char* path, bool force, bool wait) {

    const char *cmd[4] = { "volume", "unmount", path, "force" };
    int state = vold_get_volume_state(path);

    if (state <= Volume::State_Idle) {
        LOGI("Volume %s is not mounted\n", path);
        return 0;
    }

    if (state != Volume::State_Mounted) {
        LOGI("Volume %s cannot be unmounted in state %d\n", path, state);
        return -1;
    }

    return vold_command(force ? 4: 3, cmd, wait);
}

int vold_unmount_auto_volume(const char* label, bool force, bool wait) {

    char path[80];
    sprintf(path, "/storage/%s", label);
    const char *cmd[4] = { "volume", "unmount", label, "force" };
    int state = vold_get_volume_state(path);

    if (state <= Volume::State_Idle) {
        LOGI("Volume %s is not mounted\n", path);
        return 0;
    }

    if (state != Volume::State_Mounted) {
        LOGI("Volume %s cannot be unmounted in state %d\n", path, state);
        return -1;
    }

    return vold_command(force ? 4: 3, cmd, wait);
}

int vold_share_volume(const char* path) {

    const char *cmd[4] = { "volume", "share", path, "ums" };
    int state = vold_get_volume_state(path);

    if (state == Volume::State_Mounted)
        vold_unmount_volume(path, 0, 1);

    return vold_command(4, cmd, 1);
}

int vold_unshare_volume(const char* path, bool mount) {

    const char *cmd[4] = { "volume", "unshare", path, "ums" };
    int state = vold_get_volume_state(path);
    int ret = 0;

    if (state != Volume::State_Shared) {
        LOGE("Volume %s is not shared - state=%d\n", path, state);
        return 0;
    }

    ret = vold_command(4, cmd, 1);

    if (mount)
        vold_mount_volume(path, 1);

    return ret;
}

int vold_format_volume(const char* path, bool wait) {

    const char* cmd[3] = { "volume", "format", path };
    return vold_command(3, cmd, wait);
}

const char* volume_state_to_string(int state) {
    if (state == Volume::State_Init)
        return "Initializing";
    else if (state == Volume::State_NoMedia)
        return "No-Media";
    else if (state == Volume::State_Idle)
        return "Idle-Unmounted";
    else if (state == Volume::State_Pending)
        return "Pending";
    else if (state == Volume::State_Mounted)
        return "Mounted";
    else if (state == Volume::State_Unmounting)
        return "Unmounting";
    else if (state == Volume::State_Checking)
        return "Checking";
    else if (state == Volume::State_Formatting)
        return "Formatting";
    else if (state == Volume::State_Shared)
        return "Shared-Unmounted";
    else if (state == Volume::State_SharedMnt)
        return "Shared-Mounted";
    else
        return "Unknown-Error";
}
