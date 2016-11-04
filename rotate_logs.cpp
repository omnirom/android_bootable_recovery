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

#include "rotate_logs.h"

#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include <string>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/stringprintf.h>
#include <private/android_logger.h> /* private pmsg functions */

static const std::string LAST_KMSG_FILTER = "recovery/last_kmsg";
static const std::string LAST_LOG_FILTER = "recovery/last_log";

ssize_t logbasename(
        log_id_t /* logId */,
        char /* prio */,
        const char *filename,
        const char * /* buf */, size_t len,
        void *arg) {
    bool* doRotate  = static_cast<bool*>(arg);
    if (LAST_KMSG_FILTER.find(filename) != std::string::npos ||
            LAST_LOG_FILTER.find(filename) != std::string::npos) {
        *doRotate = true;
    }
    return len;
}

ssize_t logrotate(
        log_id_t logId,
        char prio,
        const char *filename,
        const char *buf, size_t len,
        void *arg) {
    bool* doRotate  = static_cast<bool*>(arg);
    if (!*doRotate) {
        return __android_log_pmsg_file_write(logId, prio, filename, buf, len);
    }

    std::string name(filename);
    size_t dot = name.find_last_of('.');
    std::string sub = name.substr(0, dot);

    if (LAST_KMSG_FILTER.find(sub) == std::string::npos &&
            LAST_LOG_FILTER.find(sub) == std::string::npos) {
        return __android_log_pmsg_file_write(logId, prio, filename, buf, len);
    }

    // filename rotation
    if (dot == std::string::npos) {
        name += ".1";
    } else {
        std::string number = name.substr(dot + 1);
        if (!isdigit(number[0])) {
            name += ".1";
        } else {
            size_t i;
            if (!android::base::ParseUint(number, &i)) {
                LOG(ERROR) << "failed to parse uint in " << number;
                return -1;
            }
            name = sub + "." + std::to_string(i + 1);
        }
    }

    return __android_log_pmsg_file_write(logId, prio, name.c_str(), buf, len);
}

// Rename last_log -> last_log.1 -> last_log.2 -> ... -> last_log.$max.
// Similarly rename last_kmsg -> last_kmsg.1 -> ... -> last_kmsg.$max.
// Overwrite any existing last_log.$max and last_kmsg.$max.
void rotate_logs(const char* last_log_file, const char* last_kmsg_file) {
    // Logs should only be rotated once.
    static bool rotated = false;
    if (rotated) {
        return;
    }
    rotated = true;

    for (int i = KEEP_LOG_COUNT - 1; i >= 0; --i) {
        std::string old_log = android::base::StringPrintf("%s", last_log_file);
        if (i > 0) {
          old_log += "." + std::to_string(i);
        }
        std::string new_log = android::base::StringPrintf("%s.%d", last_log_file, i+1);
        // Ignore errors if old_log doesn't exist.
        rename(old_log.c_str(), new_log.c_str());

        std::string old_kmsg = android::base::StringPrintf("%s", last_kmsg_file);
        if (i > 0) {
          old_kmsg += "." + std::to_string(i);
        }
        std::string new_kmsg = android::base::StringPrintf("%s.%d", last_kmsg_file, i+1);
        rename(old_kmsg.c_str(), new_kmsg.c_str());
    }
}
