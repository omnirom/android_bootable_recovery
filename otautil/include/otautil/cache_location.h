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

#ifndef _OTAUTIL_OTAUTIL_CACHE_LOCATION_H_
#define _OTAUTIL_OTAUTIL_CACHE_LOCATION_H_

#include <string>

#include "android-base/macros.h"

// A singleton class to maintain the update related locations. The locations should be only set
// once at the start of the program.
class CacheLocation {
 public:
  static CacheLocation& location();

  // getter and setter functions.
  std::string cache_temp_source() const {
    return cache_temp_source_;
  }
  void set_cache_temp_source(const std::string& temp_source) {
    cache_temp_source_ = temp_source;
  }

  std::string last_command_file() const {
    return last_command_file_;
  }
  void set_last_command_file(const std::string& last_command) {
    last_command_file_ = last_command;
  }

  std::string stash_directory_base() const {
    return stash_directory_base_;
  }
  void set_stash_directory_base(const std::string& base) {
    stash_directory_base_ = base;
  }

 private:
  CacheLocation();
  DISALLOW_COPY_AND_ASSIGN(CacheLocation);

  // When there isn't enough room on the target filesystem to hold the patched version of the file,
  // we copy the original here and delete it to free up space.  If the expected source file doesn't
  // exist, or is corrupted, we look to see if the cached file contains the bits we want and use it
  // as the source instead.  The default location for the cached source is "/cache/saved.file".
  std::string cache_temp_source_;

  // Location to save the last command that stashes blocks.
  std::string last_command_file_;

  // The base directory to write stashes during update.
  std::string stash_directory_base_;
};

#endif  // _OTAUTIL_OTAUTIL_CACHE_LOCATION_H_
