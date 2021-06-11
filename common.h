/*
 * Copyright (C) 2007 The Android Open Source Project
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

#ifndef RECOVERY_COMMON_H
#define RECOVERY_COMMON_H

#include <stdio.h>
#include <stdarg.h>
#include <string>

#ifdef __cplusplus
extern "C" {
#endif

#define LOGE(...) fprintf(stdout, "E:" __VA_ARGS__)
#define LOGW(...) fprintf(stdout, "W:" __VA_ARGS__)
#define LOGI(...) fprintf(stdout, "I:" __VA_ARGS__)

#if 0
#define LOGV(...) fprintf(stdout, "V:" __VA_ARGS__)
#define LOGD(...) fprintf(stdout, "D:" __VA_ARGS__)
#else
#define LOGV(...) do {} while (0)
#define LOGD(...) do {} while (0)
#endif

#define STRINGIFY(x) #x
#define EXPAND(x) STRINGIFY(x)

// Not using the command-line defined macro here because this header could be included by
// device-specific recovery libraries. We static assert the value consistency in recovery.cpp.
//static constexpr int kRecoveryApiVersion = 3;

class RecoveryUI;

extern RecoveryUI* ui;
extern bool modified_flash;
//typedef struct fstab_rec Volume;

// The current stage, e.g. "1/2".
extern std::string stage;

// The reason argument provided in "--reason=".
extern const char* reason;

// fopen a file, mounting volumes and making parent dirs as necessary.
FILE* fopen_path(const char *path, const char *mode);

void ui_print(const char* format, ...);

//static bool is_ro_debuggable();

#ifdef __cplusplus
}
#endif

bool reboot(const std::string& command);

#endif  // RECOVERY_COMMON_H
