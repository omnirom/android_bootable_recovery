/*
 * Copyright (C) 2016 The Android Open Source Project
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

#ifndef BOOTLOADER_MESSAGE_WRITER_H
#define BOOTLOADER_MESSAGE_WRITER_H

#ifdef __cplusplus
#include <string>
#include <vector>

bool clear_bootloader_message(std::string* err);

bool write_bootloader_message(const std::vector<std::string>& options, std::string* err);

#else
#include <stdbool.h>

// C Interface.
bool write_bootloader_message(const char* options);
#endif

#endif  // BOOTLOADER_MESSAGE_WRITER_H
