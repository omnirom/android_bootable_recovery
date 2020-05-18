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

#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/strings.h>
#include <android-base/unique_fd.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <linux/if.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "recovery_ui/device.h"
#include "recovery_ui/ethernet_ui.h"

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

const std::string EthernetDevice::interface = "eth0";

EthernetDevice::EthernetDevice(EthernetRecoveryUI* ui)
    : Device(ui), ctl_sock_(socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0)) {
  if (ctl_sock_ < 0) {
    PLOG(ERROR) << "Failed to open socket";
  }
}

void EthernetDevice::PreRecovery() {
  SetInterfaceFlags(0, IFF_UP);
  SetTitleIPv6LinkLocalAddress(false);
}

void EthernetDevice::PreFastboot() {
  android::base::SetProperty("fastbootd.protocol", "tcp");

  if (SetInterfaceFlags(IFF_UP, 0) < 0) {
    LOG(ERROR) << "Failed to bring up interface";
    return;
  }

  SetTitleIPv6LinkLocalAddress(true);
}

int EthernetDevice::SetInterfaceFlags(const unsigned set, const unsigned clr) {
  struct ifreq ifr;

  if (ctl_sock_ < 0) {
    return -1;
  }

  memset(&ifr, 0, sizeof(struct ifreq));
  strncpy(ifr.ifr_name, interface.c_str(), IFNAMSIZ);
  ifr.ifr_name[IFNAMSIZ - 1] = 0;

  if (ioctl(ctl_sock_, SIOCGIFFLAGS, &ifr) < 0) {
    PLOG(ERROR) << "Failed to get interface active flags";
    return -1;
  }
  ifr.ifr_flags = (ifr.ifr_flags & (~clr)) | set;

  if (ioctl(ctl_sock_, SIOCSIFFLAGS, &ifr) < 0) {
    PLOG(ERROR) << "Failed to set interface active flags";
    return -1;
  }

  return 0;
}

void EthernetDevice::SetTitleIPv6LinkLocalAddress(const bool interface_up) {
  auto recovery_ui = reinterpret_cast<EthernetRecoveryUI*>(GetUI());
  if (!interface_up) {
    recovery_ui->SetIPv6LinkLocalAddress();
    return;
  }

  struct ifaddrs* ifaddr;
  if (getifaddrs(&ifaddr) == -1) {
    PLOG(ERROR) << "Failed to get interface addresses";
    recovery_ui->SetIPv6LinkLocalAddress();
    return;
  }

  std::unique_ptr<struct ifaddrs, decltype(&freeifaddrs)> guard{ ifaddr, freeifaddrs };
  for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr->sa_family != AF_INET6 || interface != ifa->ifa_name) {
      continue;
    }

    auto current_addr = reinterpret_cast<struct sockaddr_in6*>(ifa->ifa_addr);
    if (!IN6_IS_ADDR_LINKLOCAL(&(current_addr->sin6_addr))) {
      continue;
    }

    char addrstr[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, reinterpret_cast<const void*>(&current_addr->sin6_addr), addrstr,
              INET6_ADDRSTRLEN);
    LOG(INFO) << "Our IPv6 link-local address is " << addrstr;
    recovery_ui->SetIPv6LinkLocalAddress(addrstr);
    return;
  }

  recovery_ui->SetIPv6LinkLocalAddress();
}

// -----------------------------------------------------------------------------------------
Device* make_device() {
  return new EthernetDevice(new EthernetRecoveryUI);
}
