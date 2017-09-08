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

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <string>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/unique_fd.h>
#include <bootloader_message/bootloader_message.h>
#include <gtest/gtest.h>

#include "common/component_test_util.h"

static const std::string UNCRYPT_SOCKET = "/dev/socket/uncrypt";
static const std::string INIT_SVC_SETUP_BCB = "init.svc.setup-bcb";
static const std::string INIT_SVC_CLEAR_BCB = "init.svc.clear-bcb";
static const std::string INIT_SVC_UNCRYPT = "init.svc.uncrypt";
static constexpr int SOCKET_CONNECTION_MAX_RETRY = 30;

class UncryptTest : public ::testing::Test {
 protected:
  UncryptTest() : has_misc(true) {}

  virtual void SetUp() override {
    ASSERT_TRUE(android::base::SetProperty("ctl.stop", "setup-bcb"));
    ASSERT_TRUE(android::base::SetProperty("ctl.stop", "clear-bcb"));
    ASSERT_TRUE(android::base::SetProperty("ctl.stop", "uncrypt"));

    bool success = false;
    for (int retry = 0; retry < SOCKET_CONNECTION_MAX_RETRY; retry++) {
      std::string setup_bcb = android::base::GetProperty(INIT_SVC_SETUP_BCB, "");
      std::string clear_bcb = android::base::GetProperty(INIT_SVC_CLEAR_BCB, "");
      std::string uncrypt = android::base::GetProperty(INIT_SVC_UNCRYPT, "");
      LOG(INFO) << "setup-bcb: [" << setup_bcb << "] clear-bcb: [" << clear_bcb << "] uncrypt: ["
                << uncrypt << "]";
      if (setup_bcb != "running" && clear_bcb != "running" && uncrypt != "running") {
        success = true;
        break;
      }
      sleep(1);
    }

    ASSERT_TRUE(success) << "uncrypt service is not available.";

    has_misc = parse_misc();
  }

  bool has_misc;
};

TEST_F(UncryptTest, setup_bcb) {
  if (!has_misc) {
    GTEST_LOG_(INFO) << "Test skipped due to no /misc partition found on the device.";
    return;
  }

  // Trigger the setup-bcb service.
  ASSERT_TRUE(android::base::SetProperty("ctl.start", "setup-bcb"));

  // Test tends to be flaky if proceeding immediately ("Transport endpoint is not connected").
  sleep(1);

  struct sockaddr_un un = {};
  un.sun_family = AF_UNIX;
  strlcpy(un.sun_path, UNCRYPT_SOCKET.c_str(), sizeof(un.sun_path));

  int sockfd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  ASSERT_NE(-1, sockfd);

  // Connect to the uncrypt socket.
  bool success = false;
  for (int retry = 0; retry < SOCKET_CONNECTION_MAX_RETRY; retry++) {
    if (connect(sockfd, reinterpret_cast<struct sockaddr*>(&un), sizeof(struct sockaddr_un)) != 0) {
      success = true;
      break;
    }
    sleep(1);
  }
  ASSERT_TRUE(success);

  // Send out the BCB message.
  std::string message = "--update_message=abc value";
  std::string message_in_bcb = "recovery\n--update_message=abc value\n";
  int length = static_cast<int>(message.size());
  int length_out = htonl(length);
  ASSERT_TRUE(android::base::WriteFully(sockfd, &length_out, sizeof(int)))
      << "Failed to write length: " << strerror(errno);
  ASSERT_TRUE(android::base::WriteFully(sockfd, message.data(), length))
      << "Failed to write message: " << strerror(errno);

  // Check the status code from uncrypt.
  int status;
  ASSERT_TRUE(android::base::ReadFully(sockfd, &status, sizeof(int)));
  ASSERT_EQ(100U, ntohl(status));

  // Ack having received the status code.
  int code = 0;
  ASSERT_TRUE(android::base::WriteFully(sockfd, &code, sizeof(int)));

  ASSERT_EQ(0, close(sockfd));

  ASSERT_TRUE(android::base::SetProperty("ctl.stop", "setup-bcb"));

  // Verify the message by reading from BCB directly.
  bootloader_message boot;
  std::string err;
  ASSERT_TRUE(read_bootloader_message(&boot, &err)) << "Failed to read BCB: " << err;

  ASSERT_EQ("boot-recovery", std::string(boot.command));
  ASSERT_EQ(message_in_bcb, std::string(boot.recovery));

  // The rest of the boot.recovery message should be zero'd out.
  ASSERT_LE(message_in_bcb.size(), sizeof(boot.recovery));
  size_t left = sizeof(boot.recovery) - message_in_bcb.size();
  ASSERT_EQ(std::string(left, '\0'), std::string(&boot.recovery[message_in_bcb.size()], left));

  // Clear the BCB.
  ASSERT_TRUE(clear_bootloader_message(&err)) << "Failed to clear BCB: " << err;
}

TEST_F(UncryptTest, clear_bcb) {
  if (!has_misc) {
    GTEST_LOG_(INFO) << "Test skipped due to no /misc partition found on the device.";
    return;
  }

  // Trigger the clear-bcb service.
  ASSERT_TRUE(android::base::SetProperty("ctl.start", "clear-bcb"));

  // Test tends to be flaky if proceeding immediately ("Transport endpoint is not connected").
  sleep(1);

  struct sockaddr_un un = {};
  un.sun_family = AF_UNIX;
  strlcpy(un.sun_path, UNCRYPT_SOCKET.c_str(), sizeof(un.sun_path));

  int sockfd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  ASSERT_NE(-1, sockfd);

  // Connect to the uncrypt socket.
  bool success = false;
  for (int retry = 0; retry < SOCKET_CONNECTION_MAX_RETRY; retry++) {
    if (connect(sockfd, reinterpret_cast<struct sockaddr*>(&un), sizeof(struct sockaddr_un)) != 0) {
      success = true;
      break;
    }
    sleep(1);
  }
  ASSERT_TRUE(success);

  // Check the status code from uncrypt.
  int status;
  ASSERT_TRUE(android::base::ReadFully(sockfd, &status, sizeof(int)));
  ASSERT_EQ(100U, ntohl(status));

  // Ack having received the status code.
  int code = 0;
  ASSERT_TRUE(android::base::WriteFully(sockfd, &code, sizeof(int)));

  ASSERT_EQ(0, close(sockfd));

  ASSERT_TRUE(android::base::SetProperty("ctl.stop", "clear-bcb"));

  // Verify the content by reading from BCB directly.
  bootloader_message boot;
  std::string err;
  ASSERT_TRUE(read_bootloader_message(&boot, &err)) << "Failed to read BCB: " << err;

  // All the bytes should be cleared.
  ASSERT_EQ(std::string(sizeof(boot), '\0'),
            std::string(reinterpret_cast<const char*>(&boot), sizeof(boot)));
}
