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
#include <android/binder_manager.h>
#include <health-shim/shim.h>
#include <healthhalutils/HealthHalUtils.h>

BatteryInfo GetBatteryInfo() {
  using android::hardware::health::V2_0::get_health_service;
  using HidlHealth = android::hardware::health::V2_0::IHealth;
  using aidl::android::hardware::health::BatteryStatus;
  using aidl::android::hardware::health::HealthShim;
  using aidl::android::hardware::health::IHealth;
  using aidl::android::hardware::health::toString;
  using std::string_literals::operator""s;

  auto service_name = IHealth::descriptor + "/default"s;
  std::shared_ptr<IHealth> health;
  if (AServiceManager_isDeclared(service_name.c_str())) {
    ndk::SpAIBinder binder(AServiceManager_waitForService(service_name.c_str()));
    health = IHealth::fromBinder(binder);
  }
  if (health == nullptr) {
    LOG(INFO) << "Unable to get AIDL health service, trying HIDL...";
    android::sp<HidlHealth> hidl_health = get_health_service();
    if (hidl_health != nullptr) {
      health = ndk::SharedRefBase::make<HealthShim>(hidl_health);
    }
  }
  if (health == nullptr) {
    LOG(WARNING) << "No health implementation is found; assuming defaults";
  }

  int wait_second = 0;
  while (true) {
    auto charge_status = BatteryStatus::UNKNOWN;
    if (health != nullptr) {
      auto res = health->getChargeStatus(&charge_status);
      if (!res.isOk()) {
        LOG(WARNING) << "Unable to call getChargeStatus: " << res.getDescription();
        charge_status = BatteryStatus::UNKNOWN;
      }
    }

    // Treat unknown status as on charger. See hardware/interfaces/health/aidl/BatteryStatus.aidl
    // for the meaning of the return values.
    bool charging = (charge_status != BatteryStatus::DISCHARGING &&
                     charge_status != BatteryStatus::NOT_CHARGING);

    int32_t capacity = INT32_MIN;
    if (health != nullptr) {
      auto res = health->getCapacity(&capacity);
      if (!res.isOk()) {
        LOG(WARNING) << "Unable to call getCapacity: " << res.getDescription();
        capacity = INT32_MIN;
      }
    }

    LOG(INFO) << "charge_status " << toString(charge_status) << ", charging " << charging
              << ", capacity " << capacity;

    constexpr int BATTERY_READ_TIMEOUT_IN_SEC = 10;
    // At startup, the battery drivers in devices like N5X/N6P take some time to load
    // the battery profile. Before the load finishes, it reports value 50 as a fake
    // capacity. BATTERY_READ_TIMEOUT_IN_SEC is set that the battery drivers are expected
    // to finish loading the battery profile earlier than 10 seconds after kernel startup.
    if (capacity == 50) {
      if (wait_second < BATTERY_READ_TIMEOUT_IN_SEC) {
        LOG(INFO) << "Battery capacity == 50, waiting "
                  << (BATTERY_READ_TIMEOUT_IN_SEC - wait_second)
                  << " seconds to ensure this is not a fake value...";
        sleep(1);
        wait_second++;
        continue;
      }
    }
    // If we can't read battery percentage, it may be a device without battery. In this
    // situation, use 100 as a fake battery percentage.
    if (capacity == INT32_MIN) {
      LOG(WARNING) << "Using fake battery capacity 100.";
      capacity = 100;
    }

    LOG(INFO) << "GetBatteryInfo() reporting charging " << charging << ", capacity " << capacity;
    return BatteryInfo{ charging, capacity };
  }
}
