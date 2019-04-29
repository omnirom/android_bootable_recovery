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
#include <vector>

#include "install/package.h"
#include "recovery_ui/device.h"

// Wipes the current A/B device, with a secure wipe of all the partitions in RECOVERY_WIPE.
bool WipeAbDevice(Device* device, size_t wipe_package_size);

// Reads the "recovery.wipe" entry in the zip archive returns a list of partitions to wipe.
std::vector<std::string> GetWipePartitionList(Package* wipe_package);
