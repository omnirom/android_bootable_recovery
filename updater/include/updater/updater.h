/*
 * Copyright (C) 2009 The Android Open Source Project
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
#include <stdio.h>

#include <memory>
#include <string>

#include <ziparchive/zip_archive.h>

#include "edify/expr.h"
#include "otautil/error_code.h"
#include "otautil/sysutil.h"

struct selabel_handle;

class Updater {
 public:
  ~Updater();

  // Memory-maps the OTA package and opens it as a zip file. Also sets up the command pipe and
  // selabel handle. TODO(xunchang) implement a run time environment class and move sehandle there.
  bool Init(int fd, const std::string& package_filename, bool is_retry,
            struct selabel_handle* sehandle);

  // Parses and evaluates the updater-script in the OTA package. Reports the error code if the
  // evaluation fails.
  bool RunUpdate();

  // Writes the message to command pipe, adds a new line in the end.
  void WriteToCommandPipe(const std::string& message, bool flush = false) const;

  // Sends over the message to recovery to print it on the screen.
  void UiPrint(const std::string& message) const;

  ZipArchiveHandle package_handle() const {
    return package_handle_;
  }
  struct selabel_handle* sehandle() const {
    return sehandle_;
  }
  std::string result() const {
    return result_;
  }

  uint8_t* GetMappedPackageAddress() const {
    return mapped_package_.addr;
  }

 private:
  friend class UpdaterTestBase;
  friend class UpdaterTest;
  // Where in the package we expect to find the edify script to execute.
  // (Note it's "updateR-script", not the older "update-script".)
  static constexpr const char* SCRIPT_NAME = "META-INF/com/google/android/updater-script";

  // Reads the entry |name| in the zip archive and put the result in |content|.
  bool ReadEntryToString(ZipArchiveHandle za, const std::string& entry_name, std::string* content);

  // Parses the error code embedded in state->errmsg; and reports the error code and cause code.
  void ParseAndReportErrorCode(State* state);

  MemMapping mapped_package_;
  ZipArchiveHandle package_handle_{ nullptr };
  std::string updater_script_;

  bool is_retry_{ false };
  std::unique_ptr<FILE, decltype(&fclose)> cmd_pipe_{ nullptr, fclose };
  struct selabel_handle* sehandle_{ nullptr };

  std::string result_;
};
