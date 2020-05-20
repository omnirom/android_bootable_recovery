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

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "edify/updater_runtime_interface.h"
#include "updater/build_info.h"

class SimulatorRuntime : public UpdaterRuntimeInterface {
 public:
  explicit SimulatorRuntime(BuildInfo* source) : source_(source) {}

  bool IsSimulator() const override {
    return true;
  }

  std::string GetProperty(const std::string_view key,
                          const std::string_view default_value) const override;

  int Mount(const std::string_view location, const std::string_view mount_point,
            const std::string_view fs_type, const std::string_view mount_options) override;
  bool IsMounted(const std::string_view mount_point) const override;
  std::pair<bool, int> Unmount(const std::string_view mount_point) override;

  bool ReadFileToString(const std::string_view filename, std::string* content) const override;
  bool WriteStringToFile(const std::string_view content,
                         const std::string_view filename) const override;

  int WipeBlockDevice(const std::string_view filename, size_t len) const override;
  int RunProgram(const std::vector<std::string>& args, bool is_vfork) const override;
  int Tune2Fs(const std::vector<std::string>& args) const override;

  bool MapPartitionOnDeviceMapper(const std::string& partition_name, std::string* path) override;
  bool UnmapPartitionOnDeviceMapper(const std::string& partition_name) override;
  bool UpdateDynamicPartitions(const std::string_view op_list_value) override;
  std::string AddSlotSuffix(const std::string_view arg) const override;

 private:
  std::string FindBlockDeviceName(const std::string_view name) const override;

  BuildInfo* source_;
  std::map<std::string, std::string, std::less<>> mounted_partitions_;
};
