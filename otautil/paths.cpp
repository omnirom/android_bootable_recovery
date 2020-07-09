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

#include "otautil/paths.h"

constexpr const char kDefaultCacheLogDirectory[] = "/cache/recovery";
constexpr const char kDefaultCacheTempSource[] = "/cache/saved.file";
constexpr const char kDefaultLastCommandFile[] = "/cache/recovery/last_command";
constexpr const char kDefaultResourceDirectory[] = "/res/images";
constexpr const char kDefaultStashDirectoryBase[] = "/cache/recovery";
constexpr const char kDefaultTemporaryInstallFile[] = "/tmp/last_install";
constexpr const char kDefaultTemporaryLogFile[] = "/tmp/recovery.log";
constexpr const char kDefaultTemporaryUpdateBinary[] = "/tmp/update-binary";

Paths& Paths::Get() {
  static Paths paths;
  return paths;
}

Paths::Paths()
    : cache_log_directory_(kDefaultCacheLogDirectory),
      cache_temp_source_(kDefaultCacheTempSource),
      last_command_file_(kDefaultLastCommandFile),
      resource_dir_(kDefaultResourceDirectory),
      stash_directory_base_(kDefaultStashDirectoryBase),
      temporary_install_file_(kDefaultTemporaryInstallFile),
      temporary_log_file_(kDefaultTemporaryLogFile),
      temporary_update_binary_(kDefaultTemporaryUpdateBinary) {}
