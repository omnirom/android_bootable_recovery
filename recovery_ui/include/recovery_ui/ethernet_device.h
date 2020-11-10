/*
 * Copyright (C) 2020 The Android Open Source Project
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

#ifndef _ETHERNET_RECOVERY_DEVICE_H
#define _ETHERNET_RECOVERY_DEVICE_H

#include "device.h"

#include <android-base/unique_fd.h>

// Forward declaration to avoid including "ethernet_ui.h".
class EthernetRecoveryUI;

class EthernetDevice : public Device {
 public:
  explicit EthernetDevice(EthernetRecoveryUI* ui);

  void PreRecovery() override;
  void PreFastboot() override;

 private:
  int SetInterfaceFlags(const unsigned set, const unsigned clr);
  void SetTitleIPv6LinkLocalAddress(const bool interface_up);

  android::base::unique_fd ctl_sock_;
  static const std::string interface;
};

#endif  // _ETHERNET_RECOVERY_DEVICE_H
