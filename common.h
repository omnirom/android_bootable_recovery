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
struct selabel_handle;

extern struct selabel_handle* sehandle;
extern RecoveryUI* ui;
extern bool has_cache;

// The current stage, e.g. "1/2".
extern std::string stage;

// The reason argument provided in "--reason=".
extern const char* reason;

void ui_print(const char* format, ...) __printflike(1, 2);

bool is_ro_debuggable();

bool SetUsbConfig(const std::string& state);
#endif  // RECOVERY_COMMON_H
