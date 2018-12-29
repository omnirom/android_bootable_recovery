/*
 * Copyright (C) 2008 The Android Open Source Project
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <poll.h>

#include <cutils/properties.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>

#include "android/os/IVold.h"

#include <binder/IServiceManager.h>
#include <binder/Status.h>

#include <private/android_filesystem_config.h>

static void usage();

static android::sp<android::IBinder> getServiceAggressive() {
    static char prop_value[PROPERTY_VALUE_MAX];
    property_get("init.svc.sys_vold", prop_value, "error");
    if (strncmp(prop_value, "running", strlen("running")) != 0) {
        printf("vdc_pie: vold is not running, init.svc.sys_vold=%s\n", prop_value);
        exit(EINVAL);
    }

    android::sp<android::IBinder> res;
    auto sm = android::defaultServiceManager();
    auto name = android::String16("vold");
    for (int i = 0; i < 5000; i++) {
        res = sm->checkService(name);
        if (res) {
            printf("vdc_pie: Got vold, waited %d ms\n", (i * 10));
            break;
        }
        usleep(10000); // 10ms
    }
    return res;
}

static int checkStatus(android::binder::Status status) {
    if (status.isOk()) return 0;
    std::string ret = status.toString8().string();
#ifdef TWRP_INCLUDE_LOGCAT
    printf("vdc_pie: Decryption failed, vold service returned: %s,"
		" see logcat for details\n", ret.c_str());
#else
	printf("vdc_pie: Decryption failed, vold service returned: %s\n", ret.c_str());
#endif
    return -1;
}

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);

    if (args.size() > 0 && args[0] == "--wait") {
        // Just ignore the --wait flag
        args.erase(args.begin());
    }

    if (args.size() < 2) {
        usage();
        exit(5);
    }
    android::sp<android::IBinder> binder = getServiceAggressive();
    if (!binder) {
        printf("vdc_pie: Failed to obtain vold Binder\n");
        exit(EINVAL);
    }
    auto vold = android::interface_cast<android::os::IVold>(binder);

    if (args[0] == "cryptfs" && args[1] == "checkpw" && args.size() == 3) {
        return checkStatus(vold->fdeCheckPassword(args[2]));
    } else {
        usage();
        exit(EINVAL);
    }
    return 0;
}

static void usage() {
    printf("vdc_pie: Usage: vold_pie cryptfs checkpw <password>\n");
}
