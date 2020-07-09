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

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include "cutils/properties.h"

#include "common.h"
#ifdef USE_OLD_VERIFIER
#include "verifier24/verifier.h"
#include "ui.h"
#elif USE_28_VERIFIER
#include "verifier28/verifier.h"
#include "verifier28/adb_install.h"
#include "verifier28/ui.h"
#include "verifier28/fuse_sideload.h"
#else
#include "install/install.h"
#endif

static void set_usb_driver(bool enabled) {
  char configfs[PROPERTY_VALUE_MAX];
  property_get("sys.usb.configfs", configfs, "false");
  if (strcmp(configfs, "false") == 0 || strcmp(configfs, "0") == 0)
    return;

  int fd = open("/sys/class/android_usb/android0/enable", O_WRONLY);
  if (fd < 0) {
/*  These error messages show when built in older Android branches (e.g. Gingerbread)
    It's not a critical error so we're disabling the error messages.
    ui->Print("failed to open driver control: %s\n", strerror(errno));
*/
    printf("failed to open driver control: %s\n", strerror(errno));
    return;
  }

  if (TEMP_FAILURE_RETRY(write(fd, enabled ? "1" : "0", 1)) == -1) {
/*
    ui->Print("failed to set driver control: %s\n", strerror(errno));
*/
    printf("failed to set driver control: %s\n", strerror(errno));
  }
  if (close(fd) < 0) {
/*
    ui->Print("failed to close driver control: %s\n", strerror(errno));
*/
    printf("failed to close driver control: %s\n", strerror(errno));
  }
}

// On Android 8.0 for some reason init can't seem to completely stop adbd
// so we have to kill it too if it doesn't die on its own.
static void kill_adbd() {
  DIR* dir = opendir("/proc");
  if (dir) {
    struct dirent* de = 0;

    while ((de = readdir(dir)) != 0) {
      if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
        continue;

      int pid = -1;
      int ret = sscanf(de->d_name, "%d", &pid);

      if (ret == 1) {
        char cmdpath[PATH_MAX];
        sprintf(cmdpath, "/proc/%d/cmdline", pid);

        FILE* file = fopen(cmdpath, "r");
        size_t task_size = PATH_MAX;
        char task[PATH_MAX];
        char* p = task;
        if (getline(&p, &task_size, file) > 0) {
          if (strstr(task, "adbd") != 0) {
            printf("adbd pid %d found, sending kill.\n", pid);
            kill(pid, SIGINT);
            usleep(5000);
            kill(pid, SIGKILL);
          }
        }
        fclose(file);
      }
    }
    closedir(dir);
  }
}

static void stop_adbd() {
  printf("Stopping adbd...\n");
  property_set("ctl.stop", "adbd");
  usleep(5000);
  kill_adbd();
  set_usb_driver(false);
}

static bool is_ro_debuggable() {
  char value[PROPERTY_VALUE_MAX+1];
  return (property_get("ro.debuggable", value, NULL) == 1 && value[0] == '1');
}

static void maybe_restart_adbd() {
  if (is_ro_debuggable()) {
    printf("Restarting adbd...\n");
    set_usb_driver(true);
    property_set("ctl.start", "adbd");
  }
}

// How long (in seconds) we wait for the host to start sending us a
// package, before timing out.
#define ADB_INSTALL_TIMEOUT 300

int
apply_from_adb(const char* install_file, pid_t* child_pid) {

  stop_adbd();
  set_usb_driver(true);
/*
int apply_from_adb(RecoveryUI* ui, bool* wipe_cache, const char* install_file) {
  modified_flash = true;

  stop_adbd(ui);
  set_usb_driver(ui, true);

  ui->Print("\n\nNow send the package you want to apply\n"
            "to the device with \"adb sideload <filename>\"...\n");
*/
  pid_t child;
  if ((child = fork()) == 0) {
    execl("/sbin/recovery", "recovery", "--adbd", install_file, NULL);
    _exit(-1);
  }

  *child_pid = child;
  // caller can now kill the child thread from another thread

  // FUSE_SIDELOAD_HOST_PATHNAME will start to exist once the host
  // connects and starts serving a package.  Poll for its
  // appearance.  (Note that inotify doesn't work with FUSE.)
  int result = INSTALL_ERROR;
  int status;
  bool waited = false;
  struct stat st;
  for (int i = 0; i < ADB_INSTALL_TIMEOUT; ++i) {
    if (waitpid(child, &status, WNOHANG) != 0) {
      result = -1;
      waited = true;
      break;
    }

    if (stat(FUSE_SIDELOAD_HOST_PATHNAME, &st) != 0) {
      if (errno == ENOENT && i < ADB_INSTALL_TIMEOUT-1) {
        sleep(1);
        continue;
      } else {
        printf("\nTimed out waiting for package: %s\n\n", strerror(errno));
        result = -1;
        kill(child, SIGKILL);
        break;
      }
    }
    // Install is handled elsewhere in TWRP
    //install_package(FUSE_SIDELOAD_HOST_PATHNAME, wipe_cache, install_file, false);
    return 0;
  }

  // if we got here, something failed
  *child_pid = 0;

  if (!waited) {
    // Calling stat() on this magic filename signals the minadbd
    // subprocess to shut down.
    stat(FUSE_SIDELOAD_HOST_EXIT_PATHNAME, &st);

    // TODO(dougz): there should be a way to cancel waiting for a
    // package (by pushing some button combo on the device).  For now
    // you just have to 'adb sideload' a file that's not a valid
    // package, like "/dev/null".
    waitpid(child, &status, 0);
  }

  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    if (WEXITSTATUS(status) == 3) {
      printf("\nYou need adb 1.0.32 or newer to sideload\nto this device.\n\n");
      result = -2;
    } else if (!WIFSIGNALED(status)) {
      printf("adbd status %d\n", WEXITSTATUS(status));
    }
  }

  set_usb_driver(false);
  maybe_restart_adbd();

  return result;
}
