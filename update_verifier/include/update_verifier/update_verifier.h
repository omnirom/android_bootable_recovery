/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include <string>

int update_verifier(int argc, char** argv);

// Returns true to indicate a passing verification (or the error should be ignored); Otherwise
// returns false on fatal errors, where we should reject the current boot and trigger a fallback.
// This function tries to process the care_map.txt as protobuf message; and falls back to use the
// plain text format if the parse failed.
//
// Note that update_verifier should be backward compatible to not reject care_map.txt from old
// releases, which could otherwise fail to boot into the new release. For example, we've changed
// the care_map format between N and O. An O update_verifier would fail to work with N care_map.txt.
// This could be a result of sideloading an O OTA while the device having a pending N update.
bool verify_image(const std::string& care_map_name);
