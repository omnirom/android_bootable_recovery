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

#include "private/commands.h"

#include <string>

#include <android-base/logging.h>

Command::Type Command::ParseType(const std::string& type_str) {
  if (type_str == "zero") {
    return Type::ZERO;
  } else if (type_str == "new") {
    return Type::NEW;
  } else if (type_str == "erase") {
    return Type::ERASE;
  } else if (type_str == "move") {
    return Type::MOVE;
  } else if (type_str == "bsdiff") {
    return Type::BSDIFF;
  } else if (type_str == "imgdiff") {
    return Type::IMGDIFF;
  } else if (type_str == "stash") {
    return Type::STASH;
  } else if (type_str == "free") {
    return Type::FREE;
  }
  LOG(ERROR) << "Invalid type: " << type_str;
  return Type::LAST;
};
