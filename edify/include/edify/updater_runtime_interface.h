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

#pragma once

#include <string>
#include <string_view>
#include <vector>

// This class serves as the base to updater runtime. It wraps the runtime dependent functions; and
// updates on device and host simulations can have different implementations. e.g. block devices
// during host simulation merely a temporary file. With this class, the caller side in registered
// updater's functions will stay the same for both update and simulation.
class UpdaterRuntimeInterface {
 public:
  virtual ~UpdaterRuntimeInterface() = default;

  // Returns true if it's a runtime instance for simulation.
  virtual bool IsSimulator() const = 0;

  // Returns the value of system property |key|. If the property doesn't exist, returns
  // |default_value|.
  virtual std::string GetProperty(const std::string_view key,
                                  const std::string_view default_value) const = 0;

  // Given the name of the block device, returns |name| for updates on the device; or the file path
  // to the fake block device for simulations.
  virtual std::string FindBlockDeviceName(const std::string_view name) const = 0;

  // Mounts the |location| on |mount_point|. Returns 0 on success.
  virtual int Mount(const std::string_view location, const std::string_view mount_point,
                    const std::string_view fs_type, const std::string_view mount_options) = 0;

  // Returns true if |mount_point| is mounted.
  virtual bool IsMounted(const std::string_view mount_point) const = 0;

  // Unmounts the |mount_point|. Returns a pair of results with the first value indicating
  // if the |mount_point| is mounted, and the second value indicating the result of umount(2).
  virtual std::pair<bool, int> Unmount(const std::string_view mount_point) = 0;

  // Reads |filename| and puts its value to |content|.
  virtual bool ReadFileToString(const std::string_view filename, std::string* content) const = 0;

  // Updates the content of |filename| with |content|.
  virtual bool WriteStringToFile(const std::string_view content,
                                 const std::string_view filename) const = 0;

  // Wipes the first |len| bytes of block device in |filename|.
  virtual int WipeBlockDevice(const std::string_view filename, size_t len) const = 0;

  // Starts a child process and runs the program with |args|. Uses vfork(2) if |is_vfork| is true.
  virtual int RunProgram(const std::vector<std::string>& args, bool is_vfork) const = 0;

  // Runs tune2fs with arguments |args|.
  virtual int Tune2Fs(const std::vector<std::string>& args) const = 0;

  // Dynamic partition related functions.
  virtual bool MapPartitionOnDeviceMapper(const std::string& partition_name, std::string* path) = 0;
  virtual bool UnmapPartitionOnDeviceMapper(const std::string& partition_name) = 0;
  virtual bool UpdateDynamicPartitions(const std::string_view op_list_value) = 0;

  // On devices supports A/B, add current slot suffix to arg. Otherwise, return |arg| as is.
  virtual std::string AddSlotSuffix(const std::string_view arg) const = 0;
};
