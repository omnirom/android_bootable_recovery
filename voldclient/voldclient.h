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
#ifndef _VOLD_CLIENT_H
#define _VOLD_CLIENT_H

#include <sys/types.h>
#include <linux/kdev_t.h>

int vold_mount_volume(const char* path, bool wait);
int vold_mount_auto_volume(const char* label, bool wait);
int vold_unmount_volume(const char* path, bool force, bool wait);
int vold_unmount_auto_volume(const char* label, bool force, bool wait);

int vold_share_volume(const char* path);
int vold_unshare_volume(const char* path, bool remount);

int vold_format_volume(const char* path, bool wait);

int vold_is_volume_available(const char* path);
int vold_get_volume_state(const char* path);

int vold_update_volumes();
int vold_get_num_volumes();
void vold_mount_all();
void vold_unmount_all();

struct vold_callbacks {
    int (*state_changed)(char* label, char* path, int state);
    int (*disk_added)(char* label, char* path);
    int (*disk_removed)(char* label, char* path);
};

void vold_client_start(struct vold_callbacks* callbacks, int automount);
void vold_set_automount(int enabled);
int vold_command(int len, const char** command, int wait);

const char* volume_state_to_string(int state);

#endif

