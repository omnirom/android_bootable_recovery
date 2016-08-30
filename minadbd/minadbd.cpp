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

#include "minadbd.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "adb.h"
#include "adb_auth.h"
#include "transport.h"

int minadbd_main() {
    adb_device_banner = "sideload";

    signal(SIGPIPE, SIG_IGN);

    // We can't require authentication for sideloading. http://b/22025550.
    auth_required = false;

    init_transport_registration();
    usb_init();

    VLOG(ADB) << "Event loop starting";
    fdevent_loop();

    return 0;
}
