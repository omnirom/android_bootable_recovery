/*
 * Copyright (C) 2019 The Android Open Source Project
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

// The message between recovery and minadbd is 8 bytes in size unless the length is explicitly
// specified. Both the command and status has the format |prefix(4 bytes) + encoded enum(4 bytes)|.
constexpr size_t kMinadbdMessageSize = 8;
constexpr char const kMinadbdCommandPrefix[] = "COMD";
constexpr char const kMinadbdStatusPrefix[] = "STAT";

enum MinadbdErrorCode : int {
  kMinadbdSuccess = 0,
  kMinadbdArgumentsParsingError = 1,
  kMinadbdSocketIOError = 2,
  kMinadbdMessageFormatError = 3,
  kMinadbdAdbVersionError = 4,
  kMinadbdHostCommandArgumentError = 5,
  kMinadbdFuseStartError = 6,
  kMinadbdUnsupportedCommandError = 7,
  kMinadbdCommandExecutionError = 8,
  kMinadbdErrorUnknown = 9,
  kMinadbdHostSocketIOError = 10,
};

enum class MinadbdCommandStatus : uint32_t {
  kSuccess = 0,
  kFailure = 1,
};

enum class MinadbdCommand : uint32_t {
  kInstall = 0,
  kUiPrint = 1,
  kRebootAndroid = 2,
  kRebootBootloader = 3,
  kRebootFastboot = 4,
  kRebootRecovery = 5,
  kRebootRescue = 6,
  kWipeCache = 7,
  kWipeData = 8,
  kNoOp = 9,

  // Last but invalid command.
  kError,
};

static_assert(kMinadbdMessageSize == sizeof(kMinadbdCommandPrefix) - 1 + sizeof(MinadbdCommand));
static_assert(kMinadbdMessageSize ==
              sizeof(kMinadbdStatusPrefix) - 1 + sizeof(MinadbdCommandStatus));
