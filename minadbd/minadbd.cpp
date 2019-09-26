/*
 * Copyright (C) 2015 The Android Open Source Project
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

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#include <android-base/logging.h>
#include <android-base/parseint.h>

#include "adb.h"
#include "adb_auth.h"
#include "transport.h"

#include "minadbd/types.h"
#include "minadbd_services.h"

using namespace std::string_literals;

int main(int argc, char** argv) {
  android::base::InitLogging(argv, &android::base::StderrLogger);
  // TODO(xunchang) implement a command parser
  if ((argc != 3 && argc != 4) || argv[1] != "--socket_fd"s ||
      (argc == 4 && argv[3] != "--rescue"s)) {
    LOG(ERROR) << "minadbd has invalid arguments, argc: " << argc;
    exit(kMinadbdArgumentsParsingError);
  }

  int socket_fd;
  if (!android::base::ParseInt(argv[2], &socket_fd)) {
    LOG(ERROR) << "Failed to parse int in " << argv[2];
    exit(kMinadbdArgumentsParsingError);
  }
  if (fcntl(socket_fd, F_GETFD, 0) == -1) {
    PLOG(ERROR) << "Failed to get minadbd socket";
    exit(kMinadbdSocketIOError);
  }
  SetMinadbdSocketFd(socket_fd);

  if (argc == 4) {
    SetMinadbdRescueMode(true);
    adb_device_banner = "rescue";
  } else {
    adb_device_banner = "sideload";
  }

  signal(SIGPIPE, SIG_IGN);

  // We can't require authentication for sideloading. http://b/22025550.
  auth_required = false;

  init_transport_registration();
  usb_init();

  VLOG(ADB) << "Event loop starting";
  fdevent_loop();

  return 0;
}
