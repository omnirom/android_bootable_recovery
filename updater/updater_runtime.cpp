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
#include <selinux/label.h>
#include <tune2fs.h>

#include "otautil/mounts.h"
#include "otautil/sysutil.h"

std::string UpdaterRuntime::GetProperty(const std::string_view key,
                                        const std::string_view default_value) const {
  return android::base::GetProperty(std::string(key), std::string(default_value));
}

std::string UpdaterRuntime::FindBlockDeviceName(const std::string_view name) const {
  return std::string(name);
}

int UpdaterRuntime::Mount(const std::string_view location, const std::string_view mount_point,
                          const std::string_view fs_type, const std::string_view mount_options) {
  std::string mount_point_string(mount_point);
  char* secontext = nullptr;
  if (sehandle_) {
    selabel_lookup(sehandle_, &secontext, mount_point_string.c_str(), 0755);
    setfscreatecon(secontext);
  }

  mkdir(mount_point_string.c_str(), 0755);

  if (secontext) {
    freecon(secontext);
    setfscreatecon(nullptr);
  }

  return mount(std::string(location).c_str(), mount_point_string.c_str(),
               std::string(fs_type).c_str(), MS_NOATIME | MS_NODEV | MS_NODIRATIME,
               std::string(mount_options).c_str());
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
