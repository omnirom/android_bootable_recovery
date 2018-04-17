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

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <memory>
#include <string>

#include <android-base/file.h>
#include <android/log.h>
#include <gtest/gtest.h>
#include <private/android_logger.h>

static const std::string kInjectTxtFilename = "/data/misc/recovery/inject.txt";
static const std::string kInjectTxtContent = "Hello World\nWelcome to my recovery\n";

// Failure is expected on systems that do not deliver either the
// recovery-persist or recovery-refresh executables. Tests also require
// a reboot sequence of test to truly verify.

static ssize_t __pmsg_fn(log_id_t logId, char prio, const char *filename,
                         const char *buf, size_t len, void *arg) {
  EXPECT_EQ(LOG_ID_SYSTEM, logId);
  EXPECT_EQ(ANDROID_LOG_INFO, prio);
  EXPECT_NE(std::string::npos, kInjectTxtFilename.find(filename));
  EXPECT_EQ(kInjectTxtContent, buf);
  EXPECT_EQ(kInjectTxtContent.size(), len);
  EXPECT_EQ(nullptr, arg);
  return len;
}

// recovery.refresh - May fail. Requires recovery.inject, two reboots,
//                    then expect success after second reboot.
TEST(recovery, refresh) {
  EXPECT_EQ(0, access("/system/bin/recovery-refresh", F_OK));

  ssize_t ret = __android_log_pmsg_file_read(
      LOG_ID_SYSTEM, ANDROID_LOG_INFO, "recovery/", __pmsg_fn, nullptr);
  if (ret == -ENOENT) {
    EXPECT_LT(0, __android_log_pmsg_file_write(
                     LOG_ID_SYSTEM, ANDROID_LOG_INFO, kInjectTxtFilename.c_str(),
                     kInjectTxtContent.c_str(), kInjectTxtContent.size()));

    fprintf(stderr,
            "injected test data, requires two intervening reboots to check for replication\n");
  }
  EXPECT_EQ(static_cast<ssize_t>(kInjectTxtContent.size()), ret);
}

// recovery.persist - Requires recovery.inject, then a reboot, then
//                    expect success after for this test on that boot.
TEST(recovery, persist) {
  EXPECT_EQ(0, access("/system/bin/recovery-persist", F_OK));

  ssize_t ret = __android_log_pmsg_file_read(
      LOG_ID_SYSTEM, ANDROID_LOG_INFO, "recovery/", __pmsg_fn, nullptr);
  if (ret == -ENOENT) {
    EXPECT_LT(0, __android_log_pmsg_file_write(
                     LOG_ID_SYSTEM, ANDROID_LOG_INFO, kInjectTxtFilename.c_str(),
                     kInjectTxtContent.c_str(), kInjectTxtContent.size()));

    fprintf(stderr, "injected test data, requires intervening reboot to check for storage\n");
  }

  std::string buf;
  EXPECT_TRUE(android::base::ReadFileToString(kInjectTxtFilename, &buf));
  EXPECT_EQ(kInjectTxtContent, buf);
  if (access(kInjectTxtFilename.c_str(), F_OK) == 0) {
    fprintf(stderr, "Removing persistent test data, check if reconstructed on reboot\n");
  }
  EXPECT_EQ(0, unlink(kInjectTxtFilename.c_str()));
}
