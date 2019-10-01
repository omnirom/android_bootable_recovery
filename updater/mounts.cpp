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

#include "mounts.h"

#include <errno.h>
#include <fcntl.h>
#include <mntent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>

#include <string>
#include <vector>

#include <android-base/logging.h>

struct MountedVolume {
  std::string device;
  std::string mount_point;
  std::string filesystem;
  std::string flags;
};

static std::vector<MountedVolume*> g_mounts_state;

bool scan_mounted_volumes() {
  for (size_t i = 0; i < g_mounts_state.size(); ++i) {
    delete g_mounts_state[i];
  }
  g_mounts_state.clear();

  // Open and read mount table entries.
  FILE* fp = setmntent("/proc/mounts", "re");
  if (fp == NULL) {
    return false;
  }
  mntent* e;
  while ((e = getmntent(fp)) != NULL) {
    MountedVolume* v = new MountedVolume;
    v->device = e->mnt_fsname;
    v->mount_point = e->mnt_dir;
    v->filesystem = e->mnt_type;
    v->flags = e->mnt_opts;
    g_mounts_state.push_back(v);
  }
  endmntent(fp);
  return true;
}

MountedVolume* find_mounted_volume_by_mount_point(const char* mount_point) {
  for (size_t i = 0; i < g_mounts_state.size(); ++i) {
    if (g_mounts_state[i]->mount_point == mount_point) return g_mounts_state[i];
  }
  return nullptr;
}

int unmount_mounted_volume(MountedVolume* volume) {
  // Intentionally pass the empty string to umount if the caller tries to unmount a volume they
  // already unmounted using this function.
  std::string mount_point = volume->mount_point;
  volume->mount_point.clear();
  int result = umount(mount_point.c_str());
  if (result == -1) {
    PLOG(WARNING) << "Failed to umount " << mount_point;
  }
  return result;
}
