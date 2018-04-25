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

#include <stdarg.h>
#include <stdio.h>

#include <string>

// Not using the command-line defined macro here because this header could be included by
// device-specific recovery libraries. We static assert the value consistency in recovery.cpp.
static constexpr int kRecoveryApiVersion = 3;

class RecoveryUI;

extern RecoveryUI* ui;
extern bool modified_flash;

// The current stage, e.g. "1/2".
extern std::string stage;

// The reason argument provided in "--reason=".
extern const char* reason;

// fopen(3)'s the given file, by mounting volumes and making parent dirs as necessary. Returns the
// file pointer, or nullptr on error.
FILE* fopen_path(const std::string& path, const char* mode);

// In turn fflush(3)'s, fsync(3)'s and fclose(3)'s the given stream.
void check_and_fclose(FILE* fp, const std::string& name);

void ui_print(const char* format, ...) __printflike(1, 2);

bool is_ro_debuggable();

bool reboot(const std::string& command);

#endif  // RECOVERY_COMMON_H
