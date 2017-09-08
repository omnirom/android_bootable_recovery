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

//
// Strictly to deal with reboot into system after OTA after /data
// mounts to pull the last pmsg file data and place it
// into /data/misc/recovery/ directory, rotating it in.
//
// Usage: recovery-persist [--force-persist]
//
//    On systems without /cache mount, all file content representing in the
//    recovery/ directory stored in /sys/fs/pstore/pmsg-ramoops-0 in logger
//    format that reside in the LOG_ID_SYSTEM buffer at ANDROID_LOG_INFO
//    priority or higher is transfered to the /data/misc/recovery/ directory.
//    The content is matched and rotated in as need be.
//
//    --force-persist  ignore /cache mount, always rotate in the contents.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <string>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <private/android_logger.h> /* private pmsg functions */

#include "rotate_logs.h"

static const char *LAST_LOG_FILE = "/data/misc/recovery/last_log";
static const char *LAST_PMSG_FILE = "/sys/fs/pstore/pmsg-ramoops-0";
static const char *LAST_KMSG_FILE = "/data/misc/recovery/last_kmsg";
static const char *LAST_CONSOLE_FILE = "/sys/fs/pstore/console-ramoops-0";
static const char *ALT_LAST_CONSOLE_FILE = "/sys/fs/pstore/console-ramoops";

// close a file, log an error if the error indicator is set
static void check_and_fclose(FILE *fp, const char *name) {
    fflush(fp);
    if (ferror(fp)) {
        PLOG(ERROR) << "Error in " << name;
    }
    fclose(fp);
}

static void copy_file(const char* source, const char* destination) {
    FILE* dest_fp = fopen(destination, "w");
    if (dest_fp == nullptr) {
        PLOG(ERROR) << "Can't open " << destination;
    } else {
        FILE* source_fp = fopen(source, "r");
        if (source_fp != nullptr) {
            char buf[4096];
            size_t bytes;
            while ((bytes = fread(buf, 1, sizeof(buf), source_fp)) != 0) {
                fwrite(buf, 1, bytes, dest_fp);
            }
            check_and_fclose(source_fp, source);
        }
        check_and_fclose(dest_fp, destination);
    }
}

static bool rotated = false;

ssize_t logsave(
        log_id_t /* logId */,
        char /* prio */,
        const char *filename,
        const char *buf, size_t len,
        void * /* arg */) {

    std::string destination("/data/misc/");
    destination += filename;

    std::string buffer(buf, len);

    {
        std::string content;
        android::base::ReadFileToString(destination, &content);

        if (buffer.compare(content) == 0) {
            return len;
        }
    }

    // ToDo: Any others that match? Are we pulling in multiple
    // already-rotated files? Algorithm thus far is KISS: one file,
    // one rotation allowed.

    rotate_logs(LAST_LOG_FILE, LAST_KMSG_FILE);
    rotated = true;

    return android::base::WriteStringToFile(buffer, destination.c_str());
}

int main(int argc, char **argv) {

    /* Is /cache a mount?, we have been delivered where we are not wanted */
    /*
     * Following code halves the size of the executable as compared to:
     *
     *    load_volume_table();
     *    has_cache = volume_for_path(CACHE_ROOT) != nullptr;
     */
    bool has_cache = false;
    static const char mounts_file[] = "/proc/mounts";
    FILE *fp = fopen(mounts_file, "r");
    if (!fp) {
        PLOG(ERROR) << "failed to open " << mounts_file;
    } else {
        char *line = NULL;
        size_t len = 0;
        ssize_t read;
        while ((read = getline(&line, &len, fp)) != -1) {
            if (strstr(line, " /cache ")) {
                has_cache = true;
                break;
            }
        }
        free(line);
        fclose(fp);
    }

    if (has_cache) {
        /*
         * TBD: Future location to move content from
         * /cache/recovery to /data/misc/recovery/
         */
        /* if --force-persist flag, then transfer pmsg data anyways */
        if ((argc <= 1) || !argv[1] || strcmp(argv[1], "--force-persist")) {
            return 0;
        }
    }

    /* Is there something in pmsg? */
    if (access(LAST_PMSG_FILE, R_OK)) {
        return 0;
    }

    // Take last pmsg file contents and send it off to the logsave
    __android_log_pmsg_file_read(
        LOG_ID_SYSTEM, ANDROID_LOG_INFO, "recovery/", logsave, NULL);

    /* Is there a last console log too? */
    if (rotated) {
        if (!access(LAST_CONSOLE_FILE, R_OK)) {
            copy_file(LAST_CONSOLE_FILE, LAST_KMSG_FILE);
        } else if (!access(ALT_LAST_CONSOLE_FILE, R_OK)) {
            copy_file(ALT_LAST_CONSOLE_FILE, LAST_KMSG_FILE);
        }
    }

    return 0;
}
