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
#include <string>
#include <string_view>
#include <thread>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/memory.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>

#include "adb.h"
#include "adb_unique_fd.h"
#include "fdevent.h"
#include "fuse_adb_provider.h"
#include "fuse_sideload.h"
#include "minadbd_types.h"
#include "services.h"
#include "sysdeps.h"

static int minadbd_socket = -1;
void SetMinadbdSocketFd(int socket_fd) {
  minadbd_socket = socket_fd;
}

static bool WriteCommandToFd(MinadbdCommands cmd, int fd) {
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

static void sideload_host_service(unique_fd sfd, const std::string& args) {
  int64_t file_size;
  int block_size;
  if ((sscanf(args.c_str(), "%" SCNd64 ":%d", &file_size, &block_size) != 2) || file_size <= 0 ||
      block_size <= 0) {
    LOG(ERROR) << "bad sideload-host arguments: " << args;
    exit(kMinadbdPackageSizeError);
  }

  LOG(INFO) << "sideload-host file size " << file_size << ", block size " << block_size;

  if (!WriteCommandToFd(MinadbdCommands::kInstall, minadbd_socket)) {
    exit(kMinadbdSocketIOError);
  }

  auto adb_data_reader = std::make_unique<FuseAdbDataProvider>(sfd, file_size, block_size);
  if (int result = run_fuse_sideload(std::move(adb_data_reader)); result != 0) {
    LOG(ERROR) << "Failed to start fuse";
    exit(kMinadbdFuseStartError);
  }

  MinadbdCommandStatus status;
  if (!WaitForCommandStatus(minadbd_socket, &status)) {
    exit(kMinadbdMessageFormatError);
  }
  LOG(INFO) << "Got command status: " << static_cast<unsigned int>(status);

  LOG(INFO) << "sideload_host finished";
  exit(kMinadbdSuccess);
}

unique_fd daemon_service_to_fd(std::string_view name, atransport* /* transport */) {
  if (name.starts_with("sideload:")) {
    // This exit status causes recovery to print a special error message saying to use a newer adb
    // (that supports sideload-host).
    exit(kMinadbdAdbVersionError);
  } else if (name.starts_with("sideload-host:")) {
    std::string arg(name.substr(strlen("sideload-host:")));
    return create_service_thread("sideload-host",
                                 std::bind(sideload_host_service, std::placeholders::_1, arg));
  }
  return unique_fd{};
}
