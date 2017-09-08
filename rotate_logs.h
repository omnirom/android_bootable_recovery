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

#ifndef _ROTATE_LOGS_H
#define _ROTATE_LOGS_H

#include <string>

#include <private/android_logger.h> /* private pmsg functions */

constexpr int KEEP_LOG_COUNT = 10;

ssize_t logbasename(log_id_t /* logId */,
        char /* prio */,
        const char *filename,
        const char * /* buf */, size_t len,
        void *arg);

ssize_t logrotate(
        log_id_t logId,
        char prio,
        const char *filename,
        const char *buf, size_t len,
        void *arg);

// Rename last_log -> last_log.1 -> last_log.2 -> ... -> last_log.$max.
// Similarly rename last_kmsg -> last_kmsg.1 -> ... -> last_kmsg.$max.
// Overwrite any existing last_log.$max and last_kmsg.$max.
void rotate_logs(const char* last_log_file, const char* last_kmsg_file);

#endif //_ROTATE_LOG_H
