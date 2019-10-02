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

#include <map>
#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/strings.h>
#include <gtest/gtest.h>

#include "recovery_utils/parse_install_logs.h"

TEST(ParseInstallLogsTest, EmptyFile) {
  TemporaryFile last_install;

  auto metrics = ParseLastInstall(last_install.path);
  ASSERT_TRUE(metrics.empty());
}

TEST(ParseInstallLogsTest, SideloadSmoke) {
  TemporaryFile last_install;
  ASSERT_TRUE(android::base::WriteStringToFile("/cache/recovery/ota.zip\n0\n", last_install.path));
  auto metrics = ParseLastInstall(last_install.path);
  ASSERT_EQ(metrics.end(), metrics.find("ota_sideload"));

  ASSERT_TRUE(android::base::WriteStringToFile("/sideload/package.zip\n0\n", last_install.path));
  metrics = ParseLastInstall(last_install.path);
  ASSERT_NE(metrics.end(), metrics.find("ota_sideload"));
}

TEST(ParseInstallLogsTest, ParseRecoveryUpdateMetrics) {
  std::vector<std::string> lines = {
    "/sideload/package.zip",
    "0",
    "time_total: 300",
    "uncrypt_time: 40",
    "source_build: 4973410",
    "bytes_written_system: " + std::to_string(1200 * 1024 * 1024),
    "bytes_stashed_system: " + std::to_string(300 * 1024 * 1024),
    "bytes_written_vendor: " + std::to_string(40 * 1024 * 1024),
    "bytes_stashed_vendor: " + std::to_string(50 * 1024 * 1024),
    "temperature_start: 37000",
    "temperature_end: 38000",
    "temperature_max: 39000",
    "error: 22",
    "cause: 55",
  };

  auto metrics = ParseRecoveryUpdateMetrics(lines);

  std::map<std::string, int64_t> expected_result = {
    { "ota_time_total", 300 },         { "ota_uncrypt_time", 40 },
    { "ota_source_version", 4973410 }, { "ota_written_in_MiBs", 1240 },
    { "ota_stashed_in_MiBs", 350 },    { "ota_temperature_start", 37000 },
    { "ota_temperature_end", 38000 },  { "ota_temperature_max", 39000 },
    { "ota_non_ab_error_code", 22 },   { "ota_non_ab_cause_code", 55 },
  };

  ASSERT_EQ(expected_result, metrics);
}
