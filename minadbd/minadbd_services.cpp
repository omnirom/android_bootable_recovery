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

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

#include "adb.h"
#include "adb_unique_fd.h"
#include "fdevent.h"
#include "fuse_adb_provider.h"
#include "fuse_sideload.h"
#include "services.h"
#include "sysdeps.h"

static void sideload_host_service(unique_fd sfd, const std::string& args) {
  int64_t file_size;
  int block_size;
  if ((sscanf(args.c_str(), "%" SCNd64 ":%d", &file_size, &block_size) != 2) || file_size <= 0 ||
      block_size <= 0) {
    printf("bad sideload-host arguments: %s\n", args.c_str());
    exit(1);
  }

  printf("sideload-host file size %" PRId64 " block size %d\n", file_size, block_size);

  auto adb_data_reader =
      std::make_unique<FuseAdbDataProvider>(std::move(sfd), file_size, block_size);
  int result = run_fuse_sideload(std::move(adb_data_reader));

  printf("sideload_host finished\n");
  exit(result == 0 ? 0 : 1);
}

unique_fd daemon_service_to_fd(std::string_view name, atransport* /* transport */) {
  if (name.starts_with("sideload:")) {
    // This exit status causes recovery to print a special error message saying to use a newer adb
    // (that supports sideload-host).
    exit(3);
  } else if (name.starts_with("sideload-host:")) {
    std::string arg(name.substr(strlen("sideload-host:")));
    return create_service_thread("sideload-host",
                                 std::bind(sideload_host_service, std::placeholders::_1, arg));
  }
  return unique_fd{};
}
