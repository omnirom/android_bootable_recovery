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

#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <android/log.h>
#include <gtest/gtest.h>
#include <log/logger.h>
#include <private/android_logger.h>

static const char myFilename[] = "/data/misc/recovery/inject.txt";
static const char myContent[] = "Hello World\nWelcome to my recovery\n";

// Failure is expected on systems that do not deliver either the
// recovery-persist or recovery-refresh executables. Tests also require
// a reboot sequence of test to truly verify.

static ssize_t __pmsg_fn(log_id_t logId, char prio, const char *filename,
                         const char *buf, size_t len, void *arg) {
    EXPECT_EQ(LOG_ID_SYSTEM, logId);
    EXPECT_EQ(ANDROID_LOG_INFO, prio);
    EXPECT_EQ(0, NULL == strstr(myFilename,filename));
    EXPECT_EQ(0, strcmp(myContent, buf));
    EXPECT_EQ(sizeof(myContent), len);
    EXPECT_EQ(0, NULL != arg);
    return len;
}

// recovery.refresh - May fail. Requires recovery.inject, two reboots,
//                    then expect success after second reboot.
TEST(recovery, refresh) {
    EXPECT_EQ(0, access("/system/bin/recovery-refresh", F_OK));

    ssize_t ret = __android_log_pmsg_file_read(
        LOG_ID_SYSTEM, ANDROID_LOG_INFO, "recovery/", __pmsg_fn, NULL);
    if (ret == -ENOENT) {
        EXPECT_LT(0, __android_log_pmsg_file_write(
            LOG_ID_SYSTEM, ANDROID_LOG_INFO,
            myFilename, myContent, sizeof(myContent)));
        fprintf(stderr, "injected test data, "
                        "requires two intervening reboots "
                        "to check for replication\n");
    }
    EXPECT_EQ((ssize_t)sizeof(myContent), ret);
}

// recovery.persist - Requires recovery.inject, then a reboot, then
//                    expect success after for this test on that boot.
TEST(recovery, persist) {
    EXPECT_EQ(0, access("/system/bin/recovery-persist", F_OK));

    ssize_t ret = __android_log_pmsg_file_read(
        LOG_ID_SYSTEM, ANDROID_LOG_INFO, "recovery/", __pmsg_fn, NULL);
    if (ret == -ENOENT) {
        EXPECT_LT(0, __android_log_pmsg_file_write(
            LOG_ID_SYSTEM, ANDROID_LOG_INFO,
            myFilename, myContent, sizeof(myContent)));
        fprintf(stderr, "injected test data, "
                        "requires intervening reboot "
                        "to check for storage\n");
    }

    int fd = open(myFilename, O_RDONLY);
    EXPECT_LE(0, fd);

    char buf[sizeof(myContent) + 32];
    ret = read(fd, buf, sizeof(buf));
    close(fd);
    EXPECT_EQ(ret, (ssize_t)sizeof(myContent));
    EXPECT_EQ(0, strcmp(myContent, buf));
    if (fd >= 0) {
        fprintf(stderr, "Removing persistent test data, "
                        "check if reconstructed on reboot\n");
    }
    EXPECT_EQ(0, unlink(myFilename));
}
