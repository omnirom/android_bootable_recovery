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

#include "updater/simulator_runtime.h"

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

#include "otautil/mounts.h"
#include "otautil/sysutil.h"

std::string SimulatorRuntime::GetProperty(const std::string_view key,
                                          const std::string_view default_value) const {
  return source_->GetProperty(key, default_value);
}

int SimulatorRuntime::Mount(const std::string_view location, const std::string_view mount_point,
                            const std::string_view /* fs_type */,
                            const std::string_view /* mount_options */) {
  if (auto mounted_location = mounted_partitions_.find(mount_point);
      mounted_location != mounted_partitions_.end() && mounted_location->second != location) {
    LOG(ERROR) << mount_point << " has been mounted at " << mounted_location->second;
    return -1;
  }

  mounted_partitions_.emplace(mount_point, location);
  return 0;
}

bool SimulatorRuntime::IsMounted(const std::string_view mount_point) const {
  return mounted_partitions_.find(mount_point) != mounted_partitions_.end();
}

std::pair<bool, int> SimulatorRuntime::Unmount(const std::string_view mount_point) {
  if (!IsMounted(mount_point)) {
    return { false, -1 };
  }

  mounted_partitions_.erase(std::string(mount_point));
  return { true, 0 };
}

std::string SimulatorRuntime::FindBlockDeviceName(const std::string_view name) const {
  return source_->FindBlockDeviceName(name);
}

// TODO(xunchang) implement the utility functions in simulator.
int SimulatorRuntime::RunProgram(const std::vector<std::string>& args, bool /* is_vfork */) const {
  LOG(INFO) << "Running program with args " << android::base::Join(args, " ");
  return 0;
}

int SimulatorRuntime::Tune2Fs(const std::vector<std::string>& args) const {
  LOG(INFO) << "Running Tune2Fs with args " << android::base::Join(args, " ");
  return 0;
}

int SimulatorRuntime::WipeBlockDevice(const std::string_view filename, size_t /* len */) const {
  LOG(INFO) << "SKip wiping block device " << filename;
  return 0;
}

bool SimulatorRuntime::ReadFileToString(const std::string_view filename,
                                        std::string* /* content */) const {
  LOG(INFO) << "SKip reading filename " << filename;
  return true;
}

bool SimulatorRuntime::WriteStringToFile(const std::string_view content,
                                         const std::string_view filename) const {
  LOG(INFO) << "SKip writing " << content.size() << " bytes to file " << filename;
  return true;
}
