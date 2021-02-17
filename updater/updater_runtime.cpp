/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "updater/updater_runtime.h"

#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/strings.h>
#include <android-base/unique_fd.h>
#include <ext4_utils/wipe.h>
#include <fs_mgr.h>
#include <selinux/label.h>
#include <tune2fs.h>

#include "mounts.h"
#include "otautil/sysutil.h"

std::string UpdaterRuntime::GetProperty(const std::string_view key,
                                        const std::string_view default_value) const {
  return android::base::GetProperty(std::string(key), std::string(default_value));
}

std::string UpdaterRuntime::FindBlockDeviceName(const std::string_view name) const {
  return std::string(name);
}

static bool setMountFlag(const std::string& flag, unsigned* mount_flags) {
  static constexpr std::pair<const char*, unsigned> mount_flags_list[] = {
    { "noatime", MS_NOATIME },
    { "noexec", MS_NOEXEC },
    { "nosuid", MS_NOSUID },
    { "nodev", MS_NODEV },
    { "nodiratime", MS_NODIRATIME },
    { "ro", MS_RDONLY },
    { "rw", 0 },
    { "remount", MS_REMOUNT },
    { "bind", MS_BIND },
    { "rec", MS_REC },
    { "unbindable", MS_UNBINDABLE },
    { "private", MS_PRIVATE },
    { "slave", MS_SLAVE },
    { "shared", MS_SHARED },
    { "defaults", 0 },
  };

  for (const auto& [name, value] : mount_flags_list) {
    if (flag == name) {
      *mount_flags |= value;
      return true;
    }
  }
  return false;
}

static bool parseMountFlags(const std::string& flags, unsigned* mount_flags,
                            std::string* fs_options) {
  bool is_flag_set = false;
  std::vector<std::string> flag_list;
  for (const auto& flag : android::base::Split(flags, ",")) {
    if (!setMountFlag(flag, mount_flags)) {
      // Unknown flag, so it must be a filesystem specific option.
      flag_list.push_back(flag);
    } else {
      is_flag_set = true;
    }
  }
  *fs_options = android::base::Join(flag_list, ',');
  return is_flag_set;
}

int UpdaterRuntime::Mount(const std::string_view location, const std::string_view mount_point,
                          const std::string_view fs_type, const std::string_view mount_options) {
  std::string mount_point_string(mount_point);
  std::string mount_options_string(mount_options);
  char* secontext = nullptr;
  unsigned mount_flags = 0;
  std::string fs_options;

  if (sehandle_) {
    selabel_lookup(sehandle_, &secontext, mount_point_string.c_str(), 0755);
    setfscreatecon(secontext);
  }

  mkdir(mount_point_string.c_str(), 0755);

  if (secontext) {
    freecon(secontext);
    setfscreatecon(nullptr);
  }

  if (!parseMountFlags(mount_options_string, &mount_flags, &fs_options)) {
    // Fall back to default
    mount_flags = MS_NOATIME | MS_NODEV | MS_NODIRATIME;
  }

  return mount(std::string(location).c_str(), mount_point_string.c_str(),
               std::string(fs_type).c_str(), mount_flags, fs_options.c_str());
}

bool UpdaterRuntime::IsMounted(const std::string_view mount_point) const {
  scan_mounted_volumes();
  MountedVolume* vol = find_mounted_volume_by_mount_point(std::string(mount_point).c_str());
  return vol != nullptr;
}

std::pair<bool, int> UpdaterRuntime::Unmount(const std::string_view mount_point) {
  scan_mounted_volumes();
  MountedVolume* vol = find_mounted_volume_by_mount_point(std::string(mount_point).c_str());
  if (vol == nullptr) {
    return { false, -1 };
  }

  int ret = unmount_mounted_volume(vol);
  return { true, ret };
}

bool UpdaterRuntime::ReadFileToString(const std::string_view filename, std::string* content) const {
  return android::base::ReadFileToString(std::string(filename), content);
}

bool UpdaterRuntime::WriteStringToFile(const std::string_view content,
                                       const std::string_view filename) const {
  return android::base::WriteStringToFile(std::string(content), std::string(filename));
}

int UpdaterRuntime::WipeBlockDevice(const std::string_view filename, size_t len) const {
  android::base::unique_fd fd(open(std::string(filename).c_str(), O_WRONLY));
  if (fd == -1) {
    PLOG(ERROR) << "Failed to open " << filename;
    return false;
  }
  // The wipe_block_device function in ext4_utils returns 0 on success and 1 for failure.
  return wipe_block_device(fd, len);
}

int UpdaterRuntime::RunProgram(const std::vector<std::string>& args, bool is_vfork) const {
  CHECK(!args.empty());
  auto argv = StringVectorToNullTerminatedArray(args);
  LOG(INFO) << "about to run program [" << args[0] << "] with " << argv.size() << " args";

  pid_t child = is_vfork ? vfork() : fork();
  if (child == 0) {
    execv(argv[0], argv.data());
    PLOG(ERROR) << "run_program: execv failed";
    _exit(EXIT_FAILURE);
  }

  int status;
  waitpid(child, &status, 0);
  if (WIFEXITED(status)) {
    if (WEXITSTATUS(status) != 0) {
      LOG(ERROR) << "run_program: child exited with status " << WEXITSTATUS(status);
    }
  } else if (WIFSIGNALED(status)) {
    LOG(ERROR) << "run_program: child terminated by signal " << WTERMSIG(status);
  }

  return status;
}

int UpdaterRuntime::Tune2Fs(const std::vector<std::string>& args) const {
  auto tune2fs_args = StringVectorToNullTerminatedArray(args);
  // tune2fs changes the filesystem parameters on an ext2 filesystem; it returns 0 on success.
  return tune2fs_main(tune2fs_args.size() - 1, tune2fs_args.data());
}

std::string UpdaterRuntime::AddSlotSuffix(const std::string_view arg) const {
  return std::string(arg) + fs_mgr_get_slot_suffix();
}
