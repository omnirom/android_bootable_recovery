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

struct BatteryInfo {
  // Whether the device is on charger. Note that the value will be `true` if the battery status is
  // unknown (BATTERY_STATUS_UNKNOWN).
  bool charging;

  // The remaining battery capacity percentage (i.e. between 0 and 100). See getCapacity in
  // hardware/interfaces/health/2.0/IHealth.hal. Returns 100 in case it fails to read a value from
  // the health HAL.
  int32_t capacity;
};

// Returns the battery status for OTA installation purpose.
BatteryInfo GetBatteryInfo();
