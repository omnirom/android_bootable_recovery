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

#include <string>
#include <string_view>

class BootState {
 public:
  BootState(std::string_view reason, std::string_view stage) : reason_(reason), stage_(stage) {}

  std::string reason() const {
    return reason_;
  }
  std::string stage() const {
    return stage_;
  }

 private:
  std::string reason_;  // The reason argument provided in "--reason=".
  std::string stage_;   // The current stage, e.g. "1/2".
};
