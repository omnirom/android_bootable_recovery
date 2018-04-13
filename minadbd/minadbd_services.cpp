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

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <string>
#include <thread>

#include "adb.h"
#include "fdevent.h"
#include "fuse_adb_provider.h"
#include "sysdeps.h"

static void sideload_host_service(int sfd, const std::string& args) {
    int file_size;
    int block_size;
    if (sscanf(args.c_str(), "%d:%d", &file_size, &block_size) != 2) {
        printf("bad sideload-host arguments: %s\n", args.c_str());
        exit(1);
    }

    printf("sideload-host file size %d block size %d\n", file_size, block_size);

    int result = run_adb_fuse(sfd, file_size, block_size);

    printf("sideload_host finished\n");
    exit(result == 0 ? 0 : 1);
}

static int create_service_thread(void (*func)(int, const std::string&), const std::string& args) {
    int s[2];
    if (adb_socketpair(s)) {
        printf("cannot create service socket pair\n");
        return -1;
    }

    std::thread([s, func, args]() { func(s[1], args); }).detach();

    VLOG(SERVICES) << "service thread started, " << s[0] << ":" << s[1];
    return s[0];
}

int service_to_fd(const char* name, atransport* /* transport */) {
  int ret = -1;

  if (!strncmp(name, "sideload:", 9)) {
    // this exit status causes recovery to print a special error
    // message saying to use a newer adb (that supports
    // sideload-host).
    exit(3);
  } else if (!strncmp(name, "sideload-host:", 14)) {
    std::string arg(name + 14);
    ret = create_service_thread(sideload_host_service, arg);
  }
  if (ret >= 0) {
    close_on_exec(ret);
  }
  return ret;
}
