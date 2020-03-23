/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include <stdint.h>

#include <map>
#include <string>
#include <vector>

constexpr const char* LAST_INSTALL_FILE = "/data/misc/recovery/last_install";
constexpr const char* LAST_INSTALL_FILE_IN_CACHE = "/cache/recovery/last_install";

// Parses the metrics of update applied under recovery mode in |lines|, and returns a map with
// "name: value".
std::map<std::string, int64_t> ParseRecoveryUpdateMetrics(const std::vector<std::string>& lines);
// Parses the sideload history and update metrics in the last_install file. Returns a map with
// entries as "metrics_name: value". If no such file exists, returns an empty map.
std::map<std::string, int64_t> ParseLastInstall(const std::string& file_name);
