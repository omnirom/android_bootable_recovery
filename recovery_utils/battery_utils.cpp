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

#include "recovery_utils/battery_utils.h"

#include <stdint.h>
#include <unistd.h>

#include <android-base/logging.h>
#include <healthhalutils/HealthHalUtils.h>

BatteryInfo GetBatteryInfo() {
  using android::hardware::health::V1_0::BatteryStatus;
  using android::hardware::health::V2_0::get_health_service;
  using android::hardware::health::V2_0::IHealth;
  using android::hardware::health::V2_0::Result;
  using android::hardware::health::V2_0::toString;

  android::sp<IHealth> health = get_health_service();

  int wait_second = 0;
  while (true) {
    auto charge_status = BatteryStatus::UNKNOWN;

    if (health == nullptr) {
      LOG(WARNING) << "No health implementation is found; assuming defaults";
    } else {
      health
          ->getChargeStatus([&charge_status](auto res, auto out_status) {
            if (res == Result::SUCCESS) {
              charge_status = out_status;
            }
          })
          .isOk();  // should not have transport error
    }

    // Treat unknown status as on charger. See hardware/interfaces/health/1.0/types.hal for the
    // meaning of the return values.
    bool charging = (charge_status != BatteryStatus::DISCHARGING &&
                     charge_status != BatteryStatus::NOT_CHARGING);

    Result res = Result::UNKNOWN;
    int32_t capacity = INT32_MIN;
    if (health != nullptr) {
      health
          ->getCapacity([&res, &capacity](auto out_res, auto out_capacity) {
            res = out_res;
            capacity = out_capacity;
          })
          .isOk();  // should not have transport error
    }

    LOG(INFO) << "charge_status " << toString(charge_status) << ", charging " << charging
              << ", status " << toString(res) << ", capacity " << capacity;

    constexpr int BATTERY_READ_TIMEOUT_IN_SEC = 10;
    // At startup, the battery drivers in devices like N5X/N6P take some time to load
    // the battery profile. Before the load finishes, it reports value 50 as a fake
    // capacity. BATTERY_READ_TIMEOUT_IN_SEC is set that the battery drivers are expected
    // to finish loading the battery profile earlier than 10 seconds after kernel startup.
    if (res == Result::SUCCESS && capacity == 50) {
      if (wait_second < BATTERY_READ_TIMEOUT_IN_SEC) {
        sleep(1);
        wait_second++;
        continue;
      }
    }
    // If we can't read battery percentage, it may be a device without battery. In this
    // situation, use 100 as a fake battery percentage.
    if (res != Result::SUCCESS) {
      capacity = 100;
    }

    return BatteryInfo{ charging, capacity };
  }
}
