/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "fsck_unshare_blocks.h"

#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/unique_fd.h>
#include <fs_mgr/roots.h>

#include "otautil/roots.h"

static constexpr const char* SYSTEM_E2FSCK_BIN = "/system/bin/e2fsck_static";
static constexpr const char* TMP_E2FSCK_BIN = "/tmp/e2fsck.bin";

static bool copy_file(const char* source, const char* dest) {
  android::base::unique_fd source_fd(open(source, O_RDONLY));
  if (source_fd < 0) {
    PLOG(ERROR) << "open %s failed" << source;
    return false;
  }

  android::base::unique_fd dest_fd(open(dest, O_CREAT | O_WRONLY, S_IRWXU));
  if (dest_fd < 0) {
    PLOG(ERROR) << "open %s failed" << dest;
    return false;
  }

  for (;;) {
    char buf[4096];
    ssize_t rv = read(source_fd, buf, sizeof(buf));
    if (rv < 0) {
      PLOG(ERROR) << "read failed";
      return false;
    }
    if (rv == 0) {
      break;
    }
    if (write(dest_fd, buf, rv) != rv) {
      PLOG(ERROR) << "write failed";
      return false;
    }
  }
  return true;
}

static bool run_e2fsck(const std::string& partition) {
  Volume* volume = volume_for_mount_point(partition);
  if (!volume) {
    LOG(INFO) << "No fstab entry for " << partition << ", skipping.";
    return true;
  }

  LOG(INFO) << "Running e2fsck on device " << volume->blk_device;

  std::vector<std::string> args = { TMP_E2FSCK_BIN, "-p", "-E", "unshare_blocks",
                                    volume->blk_device };
  std::vector<char*> argv(args.size());
  std::transform(args.cbegin(), args.cend(), argv.begin(),
                 [](const std::string& arg) { return const_cast<char*>(arg.c_str()); });
  argv.push_back(nullptr);

  pid_t child;
  char* env[] = { nullptr };
  if (posix_spawn(&child, argv[0], nullptr, nullptr, argv.data(), env)) {
    PLOG(ERROR) << "posix_spawn failed";
    return false;
  }

  int status = 0;
  int ret = TEMP_FAILURE_RETRY(waitpid(child, &status, 0));
  if (ret < 0) {
    PLOG(ERROR) << "waitpid failed";
    return false;
  }
  if (!WIFEXITED(status)) {
    LOG(ERROR) << "e2fsck exited abnormally: " << status;
    return false;
  }
  int return_code = WEXITSTATUS(status);
  if (return_code >= 8) {
    LOG(ERROR) << "e2fsck could not unshare blocks: " << return_code;
    return false;
  }

  LOG(INFO) << "Successfully unshared blocks on " << partition;
  return true;
}

bool do_fsck_unshare_blocks() {
  // List of partitions we will try to e2fsck -E unshare_blocks.
  std::vector<std::string> partitions = { "/odm", "/oem", "/product", "/vendor" };

  // Temporarily mount system so we can copy e2fsck_static.
  auto system_root = android::fs_mgr::GetSystemRoot();
  bool mounted = ensure_path_mounted_at(system_root, "/mnt/system") != -1;
  partitions.push_back(system_root);

  if (!mounted) {
    LOG(ERROR) << "Failed to mount system image.";
    return false;
  }
  if (!copy_file(SYSTEM_E2FSCK_BIN, TMP_E2FSCK_BIN)) {
    LOG(ERROR) << "Could not copy e2fsck to /tmp.";
    return false;
  }
  if (umount("/mnt/system") < 0) {
    PLOG(ERROR) << "umount failed";
    return false;
  }

  bool ok = true;
  for (const auto& partition : partitions) {
    ok &= run_e2fsck(partition);
  }

  if (ok) {
    LOG(INFO) << "Finished running e2fsck.";
  } else {
    LOG(ERROR) << "Finished running e2fsck, but not all partitions succceeded.";
  }
  return ok;
}
