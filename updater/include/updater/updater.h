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
#include <string_view>

#include <ziparchive/zip_archive.h>

#include "edify/expr.h"
#include "edify/updater_interface.h"
#include "otautil/error_code.h"
#include "otautil/sysutil.h"

class Updater : public UpdaterInterface {
 public:
  explicit Updater(std::unique_ptr<UpdaterRuntimeInterface> run_time)
      : runtime_(std::move(run_time)) {}

  ~Updater() override;

  // Memory-maps the OTA package and opens it as a zip file. Also sets up the command pipe and
  // UpdaterRuntime.
  bool Init(int fd, const std::string_view package_filename, bool is_retry);

  // Parses and evaluates the updater-script in the OTA package. Reports the error code if the
  // evaluation fails.
  bool RunUpdate();

  // Writes the message to command pipe, adds a new line in the end.
  void WriteToCommandPipe(const std::string_view message, bool flush = false) const override;

  // Sends over the message to recovery to print it on the screen.
  void UiPrint(const std::string_view message) const override;

  std::string FindBlockDeviceName(const std::string_view name) const override;

  UpdaterRuntimeInterface* GetRuntime() const override {
    return runtime_.get();
  }
  ZipArchiveHandle GetPackageHandle() const override {
    return package_handle_;
  }
  std::string GetResult() const override {
    return result_;
  }
  uint8_t* GetMappedPackageAddress() const override {
    return mapped_package_.addr;
  }
  size_t GetMappedPackageLength() const override {
    return mapped_package_.length;
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

  std::unique_ptr<UpdaterRuntimeInterface> runtime_;

  MemMapping mapped_package_;
  ZipArchiveHandle package_handle_{ nullptr };
  std::string updater_script_;

  bool is_retry_{ false };
  std::unique_ptr<FILE, decltype(&fclose)> cmd_pipe_{ nullptr, fclose };

  std::string result_;
  std::vector<std::string> skipped_functions_;
};
