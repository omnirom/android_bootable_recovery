/*
 * Copyright (C) 2012 The Android Open Source Project
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

#include "install/adb_install.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <android-base/logging.h>
#include <android-base/properties.h>

#include "fuse_sideload.h"
#include "install/install.h"
#include "recovery_ui/ui.h"

static bool SetUsbConfig(const std::string& state) {
  android::base::SetProperty("sys.usb.config", state);
  return android::base::WaitForProperty("sys.usb.state", state);
}

int apply_from_adb(bool* wipe_cache, RecoveryUI* ui) {
  // Save the usb state to restore after the sideload operation.
  std::string usb_state = android::base::GetProperty("sys.usb.state", "none");
  // Clean up state and stop adbd.
  if (usb_state != "none" && !SetUsbConfig("none")) {
    LOG(ERROR) << "Failed to clear USB config";
    return INSTALL_ERROR;
  }

  ui->Print(
      "\n\nNow send the package you want to apply\n"
      "to the device with \"adb sideload <filename>\"...\n");

  pid_t child;
  if ((child = fork()) == 0) {
    execl("/system/bin/recovery", "recovery", "--adbd", nullptr);
    _exit(EXIT_FAILURE);
  }

  if (!SetUsbConfig("sideload")) {
    LOG(ERROR) << "Failed to set usb config to sideload";
    return INSTALL_ERROR;
  }

  // How long (in seconds) we wait for the host to start sending us a package, before timing out.
  static constexpr int ADB_INSTALL_TIMEOUT = 300;

  // FUSE_SIDELOAD_HOST_PATHNAME will start to exist once the host connects and starts serving a
  // package. Poll for its appearance. (Note that inotify doesn't work with FUSE.)
  int result = INSTALL_ERROR;
  int status;
  bool waited = false;
  for (int i = 0; i < ADB_INSTALL_TIMEOUT; ++i) {
    if (waitpid(child, &status, WNOHANG) != 0) {
      result = INSTALL_ERROR;
      waited = true;
      break;
    }

    struct stat st;
    if (stat(FUSE_SIDELOAD_HOST_PATHNAME, &st) != 0) {
      if (errno == ENOENT && i < ADB_INSTALL_TIMEOUT - 1) {
        sleep(1);
        continue;
      } else {
        ui->Print("\nTimed out waiting for package.\n\n");
        result = INSTALL_ERROR;
        kill(child, SIGKILL);
        break;
      }
    }
    result = install_package(FUSE_SIDELOAD_HOST_PATHNAME, wipe_cache, false, 0, ui);
    break;
  }

  if (!waited) {
    // Calling stat() on this magic filename signals the minadbd subprocess to shut down.
    struct stat st;
    stat(FUSE_SIDELOAD_HOST_EXIT_PATHNAME, &st);

    // TODO: there should be a way to cancel waiting for a package (by pushing some button combo on
    // the device). For now you just have to 'adb sideload' a file that's not a valid package, like
    // "/dev/null".
    waitpid(child, &status, 0);
  }

  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    if (WEXITSTATUS(status) == 3) {
      ui->Print("\nYou need adb 1.0.32 or newer to sideload\nto this device.\n\n");
    } else if (!WIFSIGNALED(status)) {
      ui->Print("\n(adbd status %d)\n", WEXITSTATUS(status));
    }
  }

  // Clean up before switching to the older state, for example setting the state
  // to none sets sys/class/android_usb/android0/enable to 0.
  if (!SetUsbConfig("none")) {
    LOG(ERROR) << "Failed to clear USB config";
  }

  if (usb_state != "none") {
    if (!SetUsbConfig(usb_state)) {
      LOG(ERROR) << "Failed to set USB config to " << usb_state;
    }
  }

  return result;
}
