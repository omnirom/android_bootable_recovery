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

#ifndef _OTAUTIL_PATHS_H_
#define _OTAUTIL_PATHS_H_

#include <string>

#include <android-base/macros.h>

// A singleton class to maintain the update related paths. The paths should be only set once at the
// start of the program.
class Paths {
 public:
  static Paths& Get();

  std::string cache_log_directory() const {
    return cache_log_directory_;
  }
  void set_cache_log_directory(const std::string& log_dir) {
    cache_log_directory_ = log_dir;
  }

  std::string cache_temp_source() const {
    return cache_temp_source_;
  }
  void set_cache_temp_source(const std::string& temp_source) {
    cache_temp_source_ = temp_source;
  }

  std::string last_command_file() const {
    return last_command_file_;
  }
  void set_last_command_file(const std::string& last_command_file) {
    last_command_file_ = last_command_file;
  }

  std::string resource_dir() const {
    return resource_dir_;
  }
  void set_resource_dir(const std::string& resource_dir) {
    resource_dir_ = resource_dir;
  }

  std::string stash_directory_base() const {
    return stash_directory_base_;
  }
  void set_stash_directory_base(const std::string& base) {
    stash_directory_base_ = base;
  }

  std::string temporary_install_file() const {
    return temporary_install_file_;
  }
  void set_temporary_install_file(const std::string& install_file) {
    temporary_install_file_ = install_file;
  }

  std::string temporary_log_file() const {
    return temporary_log_file_;
  }
  void set_temporary_log_file(const std::string& log_file) {
    temporary_log_file_ = log_file;
  }

  std::string temporary_update_binary() const {
    return temporary_update_binary_;
  }
  void set_temporary_update_binary(const std::string& update_binary) {
    temporary_update_binary_ = update_binary;
  }

 private:
  Paths();
  DISALLOW_COPY_AND_ASSIGN(Paths);

  // Path to the directory that contains last_log and last_kmsg log files.
  std::string cache_log_directory_;

  // Path to the temporary source file on /cache. When there isn't enough room on the target
  // filesystem to hold the patched version of the file, we copy the original here and delete it to
  // free up space. If the expected source file doesn't exist, or is corrupted, we look to see if
  // the cached file contains the bits we want and use it as the source instead.
  std::string cache_temp_source_;

  // Path to the last command file.
  std::string last_command_file_;

  // Path to the resource dir;
  std::string resource_dir_;

  // Path to the base directory to write stashes during update.
  std::string stash_directory_base_;

  // Path to the temporary file that contains the install result.
  std::string temporary_install_file_;

  // Path to the temporary log file while under recovery.
  std::string temporary_log_file_;

  // Path to the temporary update binary while installing a non-A/B package.
  std::string temporary_update_binary_;
};

#endif  // _OTAUTIL_PATHS_H_
