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

#include "otautil/cache_location.h"

constexpr const char kDefaultCacheTempSource[] = "/cache/saved.file";
constexpr const char kDefaultLastCommandFile[] = "/cache/recovery/last_command";
constexpr const char kDefaultStashDirectoryBase[] = "/cache/recovery";

CacheLocation& CacheLocation::location() {
  static CacheLocation cache_location;
  return cache_location;
}

CacheLocation::CacheLocation()
    : cache_temp_source_(kDefaultCacheTempSource),
      last_command_file_(kDefaultLastCommandFile),
      stash_directory_base_(kDefaultStashDirectoryBase) {}
