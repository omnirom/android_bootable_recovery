/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agree to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <android-base/logging.h>
#include <gtest/gtest.h>

#include "recovery_utils/battery_utils.h"

TEST(BatteryInfoTest, GetBatteryInfo) {
  auto info = GetBatteryInfo();
  // 0 <= capacity <= 100
  ASSERT_LE(0, info.capacity);
  ASSERT_LE(info.capacity, 100);
}
