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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <chrono>

#include <android-base/logging.h>
#include <private/android_logger.h> /* private pmsg functions */

#include "common.h"
#include "minadbd/minadbd.h"
#include "otautil/paths.h"
#include "private/recovery.h"
#include "rotate_logs.h"
#include "ui.h"

static void UiLogger(android::base::LogId /* id */, android::base::LogSeverity severity,
                     const char* /* tag */, const char* /* file */, unsigned int /* line */,
                     const char* message) {
  static constexpr char log_characters[] = "VDIWEF";
  if (severity >= android::base::ERROR && ui != nullptr) {
    ui->Print("E:%s\n", message);
  } else {
    fprintf(stdout, "%c:%s\n", log_characters[severity], message);
  }
}

static void redirect_stdio(const char* filename) {
  int pipefd[2];
  if (pipe(pipefd) == -1) {
    PLOG(ERROR) << "pipe failed";

    // Fall back to traditional logging mode without timestamps. If these fail, there's not really
    // anywhere to complain...
    freopen(filename, "a", stdout);
    setbuf(stdout, nullptr);
    freopen(filename, "a", stderr);
    setbuf(stderr, nullptr);

    return;
  }

  pid_t pid = fork();
  if (pid == -1) {
    PLOG(ERROR) << "fork failed";

    // Fall back to traditional logging mode without timestamps. If these fail, there's not really
    // anywhere to complain...
    freopen(filename, "a", stdout);
    setbuf(stdout, nullptr);
    freopen(filename, "a", stderr);
    setbuf(stderr, nullptr);

    return;
  }

  if (pid == 0) {
    /// Close the unused write end.
    close(pipefd[1]);

    auto start = std::chrono::steady_clock::now();

    // Child logger to actually write to the log file.
    FILE* log_fp = fopen(filename, "ae");
    if (log_fp == nullptr) {
      PLOG(ERROR) << "fopen \"" << filename << "\" failed";
      close(pipefd[0]);
      _exit(EXIT_FAILURE);
    }

    FILE* pipe_fp = fdopen(pipefd[0], "r");
    if (pipe_fp == nullptr) {
      PLOG(ERROR) << "fdopen failed";
      check_and_fclose(log_fp, filename);
      close(pipefd[0]);
      _exit(EXIT_FAILURE);
    }

    char* line = nullptr;
    size_t len = 0;
    while (getline(&line, &len, pipe_fp) != -1) {
      auto now = std::chrono::steady_clock::now();
      double duration =
          std::chrono::duration_cast<std::chrono::duration<double>>(now - start).count();
      if (line[0] == '\n') {
        fprintf(log_fp, "[%12.6lf]\n", duration);
      } else {
        fprintf(log_fp, "[%12.6lf] %s", duration, line);
      }
      fflush(log_fp);
    }

    PLOG(ERROR) << "getline failed";

    free(line);
    check_and_fclose(log_fp, filename);
    close(pipefd[0]);
    _exit(EXIT_FAILURE);
  } else {
    // Redirect stdout/stderr to the logger process. Close the unused read end.
    close(pipefd[0]);

    setbuf(stdout, nullptr);
    setbuf(stderr, nullptr);

    if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
      PLOG(ERROR) << "dup2 stdout failed";
    }
    if (dup2(pipefd[1], STDERR_FILENO) == -1) {
      PLOG(ERROR) << "dup2 stderr failed";
    }

    close(pipefd[1]);
  }
}

int main(int argc, char** argv) {
  // We don't have logcat yet under recovery; so we'll print error on screen and log to stdout
  // (which is redirected to recovery.log) as we used to do.
  android::base::InitLogging(argv, &UiLogger);

  // Take last pmsg contents and rewrite it to the current pmsg session.
  static constexpr const char filter[] = "recovery/";
  // Do we need to rotate?
  bool do_rotate = false;

  __android_log_pmsg_file_read(LOG_ID_SYSTEM, ANDROID_LOG_INFO, filter, logbasename, &do_rotate);
  // Take action to refresh pmsg contents
  __android_log_pmsg_file_read(LOG_ID_SYSTEM, ANDROID_LOG_INFO, filter, logrotate, &do_rotate);

  // If this binary is started with the single argument "--adbd", instead of being the normal
  // recovery binary, it turns into kind of a stripped-down version of adbd that only supports the
  // 'sideload' command.  Note this must be a real argument, not anything in the command file or
  // bootloader control block; the only way recovery should be run with this argument is when it
  // starts a copy of itself from the apply_from_adb() function.
  if (argc == 2 && strcmp(argv[1], "--adbd") == 0) {
    minadbd_main();
    return 0;
  }

  // redirect_stdio should be called only in non-sideload mode. Otherwise we may have two logger
  // instances with different timestamps.
  redirect_stdio(Paths::Get().temporary_log_file().c_str());

  return start_recovery(argc, argv);
}
