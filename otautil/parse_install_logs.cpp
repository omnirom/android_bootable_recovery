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

#include "otautil/parse_install_logs.h"

#include <unistd.h>

#include <optional>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/properties.h>
#include <android-base/strings.h>

constexpr const char* OTA_SIDELOAD_METRICS = "ota_sideload";

// Here is an example of lines in last_install:
// ...
// time_total: 101
// bytes_written_vendor: 51074
// bytes_stashed_vendor: 200
std::map<std::string, int64_t> ParseRecoveryUpdateMetrics(const std::vector<std::string>& lines) {
  constexpr unsigned int kMiB = 1024 * 1024;
  std::optional<int64_t> bytes_written_in_mib;
  std::optional<int64_t> bytes_stashed_in_mib;
  std::map<std::string, int64_t> metrics;
  for (const auto& line : lines) {
    size_t num_index = line.find(':');
    if (num_index == std::string::npos) {
      LOG(WARNING) << "Skip parsing " << line;
      continue;
    }

    std::string num_string = android::base::Trim(line.substr(num_index + 1));
    int64_t parsed_num;
    if (!android::base::ParseInt(num_string, &parsed_num)) {
      LOG(ERROR) << "Failed to parse numbers in " << line;
      continue;
    }

    if (android::base::StartsWith(line, "bytes_written")) {
      bytes_written_in_mib = bytes_written_in_mib.value_or(0) + parsed_num / kMiB;
    } else if (android::base::StartsWith(line, "bytes_stashed")) {
      bytes_stashed_in_mib = bytes_stashed_in_mib.value_or(0) + parsed_num / kMiB;
    } else if (android::base::StartsWith(line, "time")) {
      metrics.emplace("ota_time_total", parsed_num);
    } else if (android::base::StartsWith(line, "uncrypt_time")) {
      metrics.emplace("ota_uncrypt_time", parsed_num);
    } else if (android::base::StartsWith(line, "source_build")) {
      metrics.emplace("ota_source_version", parsed_num);
    } else if (android::base::StartsWith(line, "temperature_start")) {
      metrics.emplace("ota_temperature_start", parsed_num);
    } else if (android::base::StartsWith(line, "temperature_end")) {
      metrics.emplace("ota_temperature_end", parsed_num);
    } else if (android::base::StartsWith(line, "temperature_max")) {
      metrics.emplace("ota_temperature_max", parsed_num);
    } else if (android::base::StartsWith(line, "error")) {
      metrics.emplace("ota_non_ab_error_code", parsed_num);
    } else if (android::base::StartsWith(line, "cause")) {
      metrics.emplace("ota_non_ab_cause_code", parsed_num);
    }
  }

  if (bytes_written_in_mib) {
    metrics.emplace("ota_written_in_MiBs", bytes_written_in_mib.value());
  }
  if (bytes_stashed_in_mib) {
    metrics.emplace("ota_stashed_in_MiBs", bytes_stashed_in_mib.value());
  }

  return metrics;
}

std::map<std::string, int64_t> ParseLastInstall(const std::string& file_name) {
  if (access(file_name.c_str(), F_OK) != 0) {
    return {};
  }

  std::string content;
  if (!android::base::ReadFileToString(file_name, &content)) {
    PLOG(ERROR) << "Failed to read " << file_name;
    return {};
  }

  if (content.empty()) {
    LOG(INFO) << "Empty last_install file";
    return {};
  }

  std::vector<std::string> lines = android::base::Split(content, "\n");
  auto metrics = ParseRecoveryUpdateMetrics(lines);

  // LAST_INSTALL starts with "/sideload/package.zip" after a sideload.
  if (android::base::Trim(lines[0]) == "/sideload/package.zip") {
    int type = (android::base::GetProperty("ro.build.type", "") == "user") ? 1 : 0;
    metrics.emplace(OTA_SIDELOAD_METRICS, type);
  }

  return metrics;
}
