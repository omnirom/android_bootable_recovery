/*
 * Copyright (C) 2016 The Android Open Source Project
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

#define LOG_TAG "recovery-refresh"

//
// Strictly to deal with reboot into system after OTA, then
// reboot while in system before boot complete landing us back
// into recovery to continue with any mitigations with retained
// log history. This simply refreshes the pmsg files from
// the last pmsg file contents.
//
// Usage:
//    recovery-refresh [--force-rotate|--rotate]
//
//    All file content representing in the recovery/ directory stored in
//    /sys/fs/pstore/pmsg-ramoops-0 in logger format that reside in the
//    LOG_ID_SYSTEM buffer at ANDROID_LOG_INFO priority or higher is
//    refreshed into /dev/pmsg0. This ensures that an unexpected reboot
//    before recovery-persist is run will still contain the associated
//    pmsg Android Logger content.
//
//    --force-rotate  recovery/last_kmsg and recovery.last_log files are
//                    rotated with .<number> suffixes upwards.
//    --rotate        rotated only if rocovery/last_msg or recovery/last_log
//                    exist, otherwise perform 1:1 refresh.
//

#include <string.h>

#include <string>

#include <android/log.h> /* Android Log Priority Tags */
#include <log/logger.h> /* Android Log packet format */
#include <private/android_logger.h> /* private pmsg functions */

static const char LAST_KMSG_FILE[] = "recovery/last_kmsg";
static const char LAST_LOG_FILE[] = "recovery/last_log";

static ssize_t logbasename(
        log_id_t /* logId */,
        char /* prio */,
        const char *filename,
        const char * /* buf */, size_t len,
        void *arg) {
    if (strstr(LAST_KMSG_FILE, filename) ||
            strstr(LAST_LOG_FILE, filename)) {
        bool *doRotate = reinterpret_cast<bool *>(arg);
        *doRotate = true;
    }
    return len;
}

static ssize_t logrotate(
        log_id_t logId,
        char prio,
        const char *filename,
        const char *buf, size_t len,
        void *arg) {
    bool *doRotate = reinterpret_cast<bool *>(arg);
    if (!*doRotate) {
        return __android_log_pmsg_file_write(logId, prio, filename, buf, len);
    }

    std::string name(filename);
    size_t dot = name.find_last_of(".");
    std::string sub = name.substr(0, dot);

    if (!strstr(LAST_KMSG_FILE, sub.c_str()) &&
                !strstr(LAST_LOG_FILE, sub.c_str())) {
        return __android_log_pmsg_file_write(logId, prio, filename, buf, len);
    }

    // filename rotation
    if (dot == std::string::npos) {
        name += ".1";
    } else {
        std::string number = name.substr(dot + 1);
        if (!isdigit(number.data()[0])) {
            name += ".1";
        } else {
            unsigned long long i = std::stoull(number);
            name = sub + "." + std::to_string(i + 1);
        }
    }

    return __android_log_pmsg_file_write(logId, prio, name.c_str(), buf, len);
}

int main(int argc, char **argv) {
    static const char filter[] = "recovery/";
    static const char force_rotate_flag[] = "--force-rotate";
    static const char rotate_flag[] = "--rotate";
    ssize_t ret;
    bool doRotate = false;

    // Take last pmsg contents and rewrite it to the current pmsg session.
    if ((argc <= 1) || !argv[1] ||
            (((doRotate = strcmp(argv[1], rotate_flag))) &&
                strcmp(argv[1], force_rotate_flag))) {
        doRotate = false;
    } else if (!doRotate) {
        // Do we need to rotate?
        __android_log_pmsg_file_read(
            LOG_ID_SYSTEM, ANDROID_LOG_INFO, filter,
            logbasename, &doRotate);
    }

    // Take action to refresh pmsg contents
    ret = __android_log_pmsg_file_read(
        LOG_ID_SYSTEM, ANDROID_LOG_INFO, filter,
        logrotate, &doRotate);

    return (ret < 0) ? ret : 0;
}
