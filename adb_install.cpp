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

#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <fcntl.h>
#include <stdio.h>

#include "ui.h"
#include "cutils/properties.h"
#include "adb_install.h"
extern "C" {
#include "minadbd/fuse_adb_provider.h"
#include "fuse_sideload.h"
}

static RecoveryUI* ui = NULL;

void
set_usb_driver(bool enabled) {
    int fd = open("/sys/class/android_usb/android0/enable", O_WRONLY);
    if (fd < 0) {
/* These error messages show when built in older Android branches (e.g. Gingerbread)
   It's not a critical error so we're disabling the error messages.
        ui->Print("failed to open driver control: %s\n", strerror(errno));
*/
		printf("failed to open driver control: %s\n", strerror(errno));
        return;
    }
    if (write(fd, enabled ? "1" : "0", 1) < 0) {
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

static void
stop_adbd() {
    property_set("ctl.stop", "adbd");
    set_usb_driver(false);
}


void
maybe_restart_adbd() {
    char value[PROPERTY_VALUE_MAX+1];
    int len = property_get("ro.debuggable", value, NULL);
    if (len == 1 && value[0] == '1') {
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
    int result;
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
            printf("status %d\n", WEXITSTATUS(status));
        }
    }

    set_usb_driver(false);
    maybe_restart_adbd();

    return result;
}
