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

#include "updater/updater.h"

#include <string.h>
#include <unistd.h>

#include <string>

#include <android-base/logging.h>
#include <android-base/strings.h>

#include "edify/updater_runtime_interface.h"

Updater::~Updater() {
  if (package_handle_) {
    CloseArchive(package_handle_);
  }
}

bool Updater::Init(int fd, const std::string_view package_filename, bool is_retry) {
  // Set up the pipe for sending commands back to the parent process.
  cmd_pipe_.reset(fdopen(fd, "wb"));
  if (!cmd_pipe_) {
    LOG(ERROR) << "Failed to open the command pipe";
    return false;
  }

  setlinebuf(cmd_pipe_.get());

  if (!mapped_package_.MapFile(std::string(package_filename))) {
    LOG(ERROR) << "failed to map package " << package_filename;
    return false;
  }
  if (int open_err = OpenArchiveFromMemory(mapped_package_.addr, mapped_package_.length,
                                           std::string(package_filename).c_str(), &package_handle_);
      open_err != 0) {
    LOG(ERROR) << "failed to open package " << package_filename << ": "
               << ErrorCodeString(open_err);
    return false;
  }
  if (!ReadEntryToString(package_handle_, SCRIPT_NAME, &updater_script_)) {
    return false;
  }

  is_retry_ = is_retry;

  return true;
}

bool Updater::RunUpdate() {
  CHECK(runtime_);

  // Parse the script.
  std::unique_ptr<Expr> root;
  int error_count = 0;
  int error = ParseString(updater_script_, &root, &error_count);
  if (error != 0 || error_count > 0) {
    LOG(ERROR) << error_count << " parse errors";
    return false;
  }

  // Evaluate the parsed script.
  State state(updater_script_, this);
  state.is_retry = is_retry_;

  bool status = Evaluate(&state, root, &result_);
  if (status) {
    fprintf(cmd_pipe_.get(), "ui_print script succeeded: result was [%s]\n", result_.c_str());
    // Even though the script doesn't abort, still log the cause code if result is empty.
    if (result_.empty() && state.cause_code != kNoCause) {
      fprintf(cmd_pipe_.get(), "log cause: %d\n", state.cause_code);
    }
    for (const auto& func : skipped_functions_) {
      LOG(WARNING) << "Skipped executing function " << func;
    }
    return true;
  }

  ParseAndReportErrorCode(&state);
  return false;
}

void Updater::WriteToCommandPipe(const std::string_view message, bool flush) const {
  fprintf(cmd_pipe_.get(), "%s\n", std::string(message).c_str());
  if (flush) {
    fflush(cmd_pipe_.get());
  }
}

void Updater::UiPrint(const std::string_view message) const {
  // "line1\nline2\n" will be split into 3 tokens: "line1", "line2" and "".
  // so skip sending empty strings to ui.
  std::vector<std::string> lines = android::base::Split(std::string(message), "\n");
  for (const auto& line : lines) {
    if (!line.empty()) {
      fprintf(cmd_pipe_.get(), "ui_print %s\n", line.c_str());
    }
  }

  // on the updater side, we need to dump the contents to stderr (which has
  // been redirected to the log file). because the recovery will only print
  // the contents to screen when processing pipe command ui_print.
  LOG(INFO) << message;
}

std::string Updater::FindBlockDeviceName(const std::string_view name) const {
  return runtime_->FindBlockDeviceName(name);
}

void Updater::ParseAndReportErrorCode(State* state) {
  CHECK(state);
  if (state->errmsg.empty()) {
    LOG(ERROR) << "script aborted (no error message)";
    fprintf(cmd_pipe_.get(), "ui_print script aborted (no error message)\n");
  } else {
    LOG(ERROR) << "script aborted: " << state->errmsg;
    const std::vector<std::string> lines = android::base::Split(state->errmsg, "\n");
    for (const std::string& line : lines) {
      // Parse the error code in abort message.
      // Example: "E30: This package is for bullhead devices."
      if (!line.empty() && line[0] == 'E') {
        if (sscanf(line.c_str(), "E%d: ", &state->error_code) != 1) {
          LOG(ERROR) << "Failed to parse error code: [" << line << "]";
        }
      }
      fprintf(cmd_pipe_.get(), "ui_print %s\n", line.c_str());
    }
  }

  // Installation has been aborted. Set the error code to kScriptExecutionFailure unless
  // a more specific code has been set in errmsg.
  if (state->error_code == kNoError) {
    state->error_code = kScriptExecutionFailure;
  }
  fprintf(cmd_pipe_.get(), "log error: %d\n", state->error_code);
  // Cause code should provide additional information about the abort.
  if (state->cause_code != kNoCause) {
    fprintf(cmd_pipe_.get(), "log cause: %d\n", state->cause_code);
    if (state->cause_code == kPatchApplicationFailure) {
      LOG(INFO) << "Patch application failed, retry update.";
      fprintf(cmd_pipe_.get(), "retry_update\n");
    } else if (state->cause_code == kEioFailure) {
      LOG(INFO) << "Update failed due to EIO, retry update.";
      fprintf(cmd_pipe_.get(), "retry_update\n");
    }
  }
}

bool Updater::ReadEntryToString(ZipArchiveHandle za, const std::string& entry_name,
                                std::string* content) {
  ZipEntry entry;
  int find_err = FindEntry(za, entry_name, &entry);
  if (find_err != 0) {
    LOG(ERROR) << "failed to find " << entry_name
               << " in the package: " << ErrorCodeString(find_err);
    return false;
  }

  content->resize(entry.uncompressed_length);
  int extract_err = ExtractToMemory(za, &entry, reinterpret_cast<uint8_t*>(&content->at(0)),
                                    entry.uncompressed_length);
  if (extract_err != 0) {
    LOG(ERROR) << "failed to read " << entry_name
               << " from package: " << ErrorCodeString(extract_err);
    return false;
  }

  return true;
}
