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

#ifndef MTDUTILS_MOUNTS_H_
#define MTDUTILS_MOUNTS_H_

typedef struct {
 const char *device;
 const char *mount_point;
 const char *filesystem;
 const char *flags;
} MountedVolume;

int scan_mounted_volumes(void);

const MountedVolume *find_mounted_volume_by_device(const char *device);

const MountedVolume *
find_mounted_volume_by_mount_point(const char *mount_point);

const char *
get_filesystem_by_mounted_volume(const MountedVolume *vol);

const char *
get_device_by_mounted_volume(const MountedVolume *vol);

int unmount_mounted_volume(const MountedVolume *volume);

int remount_read_only(const MountedVolume* volume);

#endif  // MTDUTILS_MOUNTS_H_
