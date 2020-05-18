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

#ifndef RECOVERY_ETHERNET_UI_H
#define RECOVERY_ETHERNET_UI_H

#include "screen_ui.h"

class EthernetRecoveryUI : public ScreenRecoveryUI {
 public:
  EthernetRecoveryUI() {}
  void SetTitle(const std::vector<std::string>& lines) override;

  // For EthernetDevice
  void SetIPv6LinkLocalAddress(const std::string& address = "");

 private:
  std::string address_;
};

#endif  // RECOVERY_ETHERNET_UI_H
