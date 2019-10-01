/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "recovery_utils/thermalutil.h"

#include <dirent.h>
#include <stdio.h>

#include <algorithm>
#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>

static constexpr auto THERMAL_PREFIX = "/sys/class/thermal/";

static int thermal_filter(const dirent* de) {
  if (android::base::StartsWith(de->d_name, "thermal_zone")) {
    return 1;
  }
  return 0;
}

static std::vector<std::string> InitThermalPaths() {
  dirent** namelist;
  int n = scandir(THERMAL_PREFIX, &namelist, thermal_filter, alphasort);
  if (n == -1) {
    PLOG(ERROR) << "Failed to scandir " << THERMAL_PREFIX;
    return {};
  }
  if (n == 0) {
    LOG(ERROR) << "Failed to find CPU thermal info in " << THERMAL_PREFIX;
    return {};
  }

  std::vector<std::string> thermal_paths;
  while (n--) {
    thermal_paths.push_back(THERMAL_PREFIX + std::string(namelist[n]->d_name) + "/temp");
    free(namelist[n]);
  }
  free(namelist);
  return thermal_paths;
}

int GetMaxValueFromThermalZone() {
  static std::vector<std::string> thermal_paths = InitThermalPaths();
  int max_temperature = -1;
  for (const auto& path : thermal_paths) {
    std::string content;
    if (!android::base::ReadFileToString(path, &content)) {
      PLOG(WARNING) << "Failed to read " << path;
      continue;
    }

    int temperature;
    if (!android::base::ParseInt(android::base::Trim(content), &temperature)) {
      LOG(WARNING) << "Failed to parse integer in " << content;
      continue;
    }
    max_temperature = std::max(temperature, max_temperature);
  }
  LOG(INFO) << "current maximum temperature: " << max_temperature;
  return max_temperature;
}
