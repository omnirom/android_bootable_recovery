/*
 * Copyright (C) 2007 The Android Open Source Project
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

#include "minadbd_services.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <thread>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/memory.h>
#include <android-base/parseint.h>
#include <android-base/properties.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>

#include "adb.h"
#include "adb_unique_fd.h"
#include "adb_utils.h"
#include "fuse_adb_provider.h"
#include "fuse_sideload.h"
#include "minadbd/types.h"
#include "recovery_utils/battery_utils.h"
#include "services.h"
#include "sysdeps.h"

static int minadbd_socket = -1;
static bool rescue_mode = false;
static std::string sideload_mount_point = FUSE_SIDELOAD_HOST_MOUNTPOINT;

void SetMinadbdSocketFd(int socket_fd) {
  minadbd_socket = socket_fd;
}

void SetMinadbdRescueMode(bool rescue) {
  rescue_mode = rescue;
}

void SetSideloadMountPoint(const std::string& path) {
  sideload_mount_point = path;
}

static bool WriteCommandToFd(MinadbdCommand cmd, int fd) {
  char message[kMinadbdMessageSize];
  memcpy(message, kMinadbdCommandPrefix, strlen(kMinadbdStatusPrefix));
  android::base::put_unaligned(message + strlen(kMinadbdStatusPrefix), cmd);

  if (!android::base::WriteFully(fd, message, kMinadbdMessageSize)) {
    PLOG(ERROR) << "Failed to write message " << message;
    return false;
  }
  return true;
}

// Blocks and reads the command status from |fd|. Returns false if the received message has a
// format error.
static bool WaitForCommandStatus(int fd, MinadbdCommandStatus* status) {
  char buffer[kMinadbdMessageSize];
  if (!android::base::ReadFully(fd, buffer, kMinadbdMessageSize)) {
    PLOG(ERROR) << "Failed to response status from socket";
    exit(kMinadbdSocketIOError);
  }

  std::string message(buffer, buffer + kMinadbdMessageSize);
  if (!android::base::StartsWith(message, kMinadbdStatusPrefix)) {
    LOG(ERROR) << "Failed to parse status in " << message;
    return false;
  }

  *status = android::base::get_unaligned<MinadbdCommandStatus>(
      message.substr(strlen(kMinadbdStatusPrefix)).c_str());
  return true;
}

static MinadbdErrorCode RunAdbFuseSideload(int sfd, const std::string& args,
                                           MinadbdCommandStatus* status) {
  auto pieces = android::base::Split(args, ":");
  int64_t file_size;
  int block_size;
  if (pieces.size() != 2 || !android::base::ParseInt(pieces[0], &file_size) || file_size <= 0 ||
      !android::base::ParseInt(pieces[1], &block_size) || block_size <= 0) {
    LOG(ERROR) << "bad sideload-host arguments: " << args;
    return kMinadbdHostCommandArgumentError;
  }

  LOG(INFO) << "sideload-host file size " << file_size << ", block size " << block_size;

  if (!WriteCommandToFd(MinadbdCommand::kInstall, minadbd_socket)) {
    return kMinadbdSocketIOError;
  }

  auto adb_data_reader = std::make_unique<FuseAdbDataProvider>(sfd, file_size, block_size);
  if (int result = run_fuse_sideload(std::move(adb_data_reader), sideload_mount_point.c_str());
      result != 0) {
    LOG(ERROR) << "Failed to start fuse";
    return kMinadbdFuseStartError;
  }

  if (!WaitForCommandStatus(minadbd_socket, status)) {
    return kMinadbdMessageFormatError;
  }

  // Signal host-side adb to stop. For sideload mode, we always send kMinadbdServicesExitSuccess
  // (i.e. "DONEDONE") regardless of the install result. For rescue mode, we send failure message on
  // install error.
  if (!rescue_mode || *status == MinadbdCommandStatus::kSuccess) {
    if (!android::base::WriteFully(sfd, kMinadbdServicesExitSuccess,
                                   strlen(kMinadbdServicesExitSuccess))) {
      return kMinadbdHostSocketIOError;
    }
  } else {
    if (!android::base::WriteFully(sfd, kMinadbdServicesExitFailure,
                                   strlen(kMinadbdServicesExitFailure))) {
      return kMinadbdHostSocketIOError;
    }
  }

  return kMinadbdSuccess;
}

// Sideload service always exits after serving an install command.
static void SideloadHostService(unique_fd sfd, const std::string& args) {
  MinadbdCommandStatus status;
  exit(RunAdbFuseSideload(sfd.get(), args, &status));
}

// Rescue service waits for the next command after an install command.
static void RescueInstallHostService(unique_fd sfd, const std::string& args) {
  MinadbdCommandStatus status;
  if (auto result = RunAdbFuseSideload(sfd.get(), args, &status); result != kMinadbdSuccess) {
    exit(result);
  }
}

// Answers the query on a given property |prop|, by writing the result to the given |sfd|. The
// result will be newline-terminated, so nonexistent or nonallowed query will be answered with "\n".
// If given an empty string, dumps all the supported properties (analogous to `adb shell getprop`)
// in lines, e.g. "[prop]: [value]".
static void RescueGetpropHostService(unique_fd sfd, const std::string& prop) {
  constexpr const char* kRescueBatteryLevelProp = "rescue.battery_level";
  static const std::set<std::string> kGetpropAllowedProps = {
    // clang-format off
    kRescueBatteryLevelProp,
    "ro.build.date.utc",
    "ro.build.fingerprint",
    "ro.build.flavor",
    "ro.build.id",
    "ro.build.product",
    "ro.build.tags",
    "ro.build.version.incremental",
    "ro.product.device",
    "ro.product.vendor.device",
    // clang-format on
  };

  auto query_prop = [](const std::string& key) {
    if (key == kRescueBatteryLevelProp) {
      auto battery_info = GetBatteryInfo();
      return std::to_string(battery_info.capacity);
    }
    return android::base::GetProperty(key, "");
  };

  std::string result;
  if (prop.empty()) {
    for (const auto& key : kGetpropAllowedProps) {
      auto value = query_prop(key);
      if (value.empty()) {
        continue;
      }
      result += "[" + key + "]: [" + value + "]\n";
    }
  } else if (kGetpropAllowedProps.find(prop) != kGetpropAllowedProps.end()) {
    result = query_prop(prop) + "\n";
  }
  if (result.empty()) {
    result = "\n";
  }
  if (!android::base::WriteFully(sfd, result.data(), result.size())) {
    exit(kMinadbdHostSocketIOError);
  }

  // Send heartbeat signal to keep the rescue service alive.
  if (!WriteCommandToFd(MinadbdCommand::kNoOp, minadbd_socket)) {
    exit(kMinadbdSocketIOError);
  }
  if (MinadbdCommandStatus status; !WaitForCommandStatus(minadbd_socket, &status)) {
    exit(kMinadbdMessageFormatError);
  }
}

// Reboots into the given target. We don't reboot directly from minadbd, but going through recovery
// instead. This allows recovery to finish all the pending works (clear BCB, save logs etc) before
// the reboot.
static void RebootHostService(unique_fd /* sfd */, const std::string& target) {
  MinadbdCommand command;
  if (target == "bootloader") {
    command = MinadbdCommand::kRebootBootloader;
  } else if (target == "rescue") {
    command = MinadbdCommand::kRebootRescue;
  } else if (target == "recovery") {
    command = MinadbdCommand::kRebootRecovery;
  } else if (target == "fastboot") {
    command = MinadbdCommand::kRebootFastboot;
  } else {
    command = MinadbdCommand::kRebootAndroid;
  }
  if (!WriteCommandToFd(command, minadbd_socket)) {
    exit(kMinadbdSocketIOError);
  }
  MinadbdCommandStatus status;
  if (!WaitForCommandStatus(minadbd_socket, &status)) {
    exit(kMinadbdMessageFormatError);
  }
}

static void WipeDeviceService(unique_fd fd, const std::string& args) {
  auto pieces = android::base::Split(args, ":");
  if (pieces.size() != 2 || pieces[0] != "userdata") {
    LOG(ERROR) << "Failed to parse wipe device command arguments " << args;
    exit(kMinadbdHostCommandArgumentError);
  }

  size_t message_size;
  if (!android::base::ParseUint(pieces[1], &message_size) ||
      message_size < strlen(kMinadbdServicesExitSuccess)) {
    LOG(ERROR) << "Failed to parse wipe device message size in " << args;
    exit(kMinadbdHostCommandArgumentError);
  }

  WriteCommandToFd(MinadbdCommand::kWipeData, minadbd_socket);
  MinadbdCommandStatus status;
  if (!WaitForCommandStatus(minadbd_socket, &status)) {
    exit(kMinadbdMessageFormatError);
  }

  std::string response = (status == MinadbdCommandStatus::kSuccess) ? kMinadbdServicesExitSuccess
                                                                    : kMinadbdServicesExitFailure;
  response += std::string(message_size - response.size(), '\0');
  if (!android::base::WriteFully(fd, response.c_str(), response.size())) {
    exit(kMinadbdHostSocketIOError);
  }
}

asocket* daemon_service_to_socket(std::string_view) {
  return nullptr;
}

unique_fd daemon_service_to_fd(std::string_view name, atransport* /* transport */) {
  // Common services that are supported both in sideload and rescue modes.
  if (android::base::ConsumePrefix(&name, "reboot:")) {
    // "reboot:<target>", where target must be one of the following.
    std::string args(name);
    if (args.empty() || args == "bootloader" || args == "rescue" || args == "recovery" ||
        args == "fastboot") {
      return create_service_thread("reboot",
                                   std::bind(RebootHostService, std::placeholders::_1, args));
    }
    return unique_fd{};
  }

  // Rescue-specific services.
  if (rescue_mode) {
    if (android::base::ConsumePrefix(&name, "rescue-install:")) {
      // rescue-install:<file-size>:<block-size>
      std::string args(name);
      return create_service_thread(
          "rescue-install", std::bind(RescueInstallHostService, std::placeholders::_1, args));
    } else if (android::base::ConsumePrefix(&name, "rescue-getprop:")) {
      // rescue-getprop:<prop>
      std::string args(name);
      return create_service_thread(
          "rescue-getprop", std::bind(RescueGetpropHostService, std::placeholders::_1, args));
    } else if (android::base::ConsumePrefix(&name, "rescue-wipe:")) {
      // rescue-wipe:target:<message-size>
      std::string args(name);
      return create_service_thread("rescue-wipe",
                                   std::bind(WipeDeviceService, std::placeholders::_1, args));
    }

    return unique_fd{};
  }

  // Sideload-specific services.
  if (name.starts_with("sideload:")) {
    // This exit status causes recovery to print a special error message saying to use a newer adb
    // (that supports sideload-host).
    exit(kMinadbdAdbVersionError);
  } else if (android::base::ConsumePrefix(&name, "sideload-host:")) {
    // sideload-host:<file-size>:<block-size>
    std::string args(name);
    return create_service_thread("sideload-host",
                                 std::bind(SideloadHostService, std::placeholders::_1, args));
  }
  return unique_fd{};
}
