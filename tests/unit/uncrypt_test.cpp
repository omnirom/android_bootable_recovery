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

#include <algorithm>
#include <string>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/unique_fd.h>
#include <bootloader_message/bootloader_message.h>
#include <gtest/gtest.h>

using namespace std::string_literals;

static const std::string UNCRYPT_SOCKET = "/dev/socket/uncrypt";
static const std::string INIT_SVC_SETUP_BCB = "init.svc.setup-bcb";
static const std::string INIT_SVC_CLEAR_BCB = "init.svc.clear-bcb";
static const std::string INIT_SVC_UNCRYPT = "init.svc.uncrypt";
static constexpr int SOCKET_CONNECTION_MAX_RETRY = 30;

static void StopService() {
  ASSERT_TRUE(android::base::SetProperty("ctl.stop", "setup-bcb"));
  ASSERT_TRUE(android::base::SetProperty("ctl.stop", "clear-bcb"));
  ASSERT_TRUE(android::base::SetProperty("ctl.stop", "uncrypt"));

  bool success = false;
  for (int retry = 0; retry < SOCKET_CONNECTION_MAX_RETRY; retry++) {
    std::string setup_bcb = android::base::GetProperty(INIT_SVC_SETUP_BCB, "");
    std::string clear_bcb = android::base::GetProperty(INIT_SVC_CLEAR_BCB, "");
    std::string uncrypt = android::base::GetProperty(INIT_SVC_UNCRYPT, "");
    GTEST_LOG_(INFO) << "setup-bcb: [" << setup_bcb << "] clear-bcb: [" << clear_bcb
                     << "] uncrypt: [" << uncrypt << "]";
    if (setup_bcb != "running" && clear_bcb != "running" && uncrypt != "running") {
      success = true;
      break;
    }
    sleep(1);
  }

  ASSERT_TRUE(success) << "uncrypt service is not available.";
}

class UncryptTest : public ::testing::Test {
 protected:
  UncryptTest() : has_misc(true) {}

  void SetUp() override {
    std::string err;
    has_misc = !get_bootloader_message_blk_device(&err).empty();
  }

  void TearDown() override {
    // Clear the BCB.
    if (has_misc) {
      std::string err;
      ASSERT_TRUE(clear_bootloader_message(&err)) << "Failed to clear BCB: " << err;
    }
  }

  void SetupOrClearBcb(bool isSetup, const std::string& message,
                       const std::string& message_in_bcb) const {
    // Restart the setup-bcb service.
    StopService();
    ASSERT_TRUE(android::base::SetProperty("ctl.start", isSetup ? "setup-bcb" : "clear-bcb"));

    // Test tends to be flaky if proceeding immediately ("Transport endpoint is not connected").
    sleep(1);

    sockaddr_un un = {};
    un.sun_family = AF_UNIX;
    strlcpy(un.sun_path, UNCRYPT_SOCKET.c_str(), sizeof(un.sun_path));

    int sockfd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    ASSERT_NE(-1, sockfd);

    // Connect to the uncrypt socket.
    bool success = false;
    for (int retry = 0; retry < SOCKET_CONNECTION_MAX_RETRY; retry++) {
      if (connect(sockfd, reinterpret_cast<sockaddr*>(&un), sizeof(sockaddr_un)) != 0) {
        success = true;
        break;
      }
      sleep(1);
    }
    ASSERT_TRUE(success);

    if (isSetup) {
      // Send out the BCB message.
      int length = static_cast<int>(message.size());
      int length_out = htonl(length);
      ASSERT_TRUE(android::base::WriteFully(sockfd, &length_out, sizeof(int)))
          << "Failed to write length: " << strerror(errno);
      ASSERT_TRUE(android::base::WriteFully(sockfd, message.data(), length))
          << "Failed to write message: " << strerror(errno);
    }

    // Check the status code from uncrypt.
    int status;
    ASSERT_TRUE(android::base::ReadFully(sockfd, &status, sizeof(int)));
    ASSERT_EQ(100U, ntohl(status));

    // Ack having received the status code.
    int code = 0;
    ASSERT_TRUE(android::base::WriteFully(sockfd, &code, sizeof(int)));

    ASSERT_EQ(0, close(sockfd));

    ASSERT_TRUE(android::base::SetProperty("ctl.stop", isSetup ? "setup-bcb" : "clear-bcb"));

    // Verify the message by reading from BCB directly.
    bootloader_message boot;
    std::string err;
    ASSERT_TRUE(read_bootloader_message(&boot, &err)) << "Failed to read BCB: " << err;

    if (isSetup) {
      ASSERT_EQ("boot-recovery", std::string(boot.command));
      ASSERT_EQ(message_in_bcb, std::string(boot.recovery));

      // The rest of the boot.recovery message should be zero'd out.
      ASSERT_LE(message_in_bcb.size(), sizeof(boot.recovery));
      size_t left = sizeof(boot.recovery) - message_in_bcb.size();
      ASSERT_EQ(std::string(left, '\0'), std::string(&boot.recovery[message_in_bcb.size()], left));

      // Clear the BCB.
      ASSERT_TRUE(clear_bootloader_message(&err)) << "Failed to clear BCB: " << err;
    } else {
      // All the bytes should be cleared.
      ASSERT_EQ(std::string(sizeof(boot), '\0'),
                std::string(reinterpret_cast<const char*>(&boot), sizeof(boot)));
    }
  }

  void VerifyBootloaderMessage(const std::string& expected) {
    std::string err;
    bootloader_message boot;
    ASSERT_TRUE(read_bootloader_message(&boot, &err)) << "Failed to read BCB: " << err;

    // Check that we have all the expected bytes.
    ASSERT_EQ(expected, std::string(reinterpret_cast<const char*>(&boot), sizeof(boot)));
  }

  bool has_misc;
};

TEST_F(UncryptTest, setup_bcb) {
  if (!has_misc) {
    GTEST_LOG_(INFO) << "Test skipped due to no /misc partition found on the device.";
    return;
  }

  std::string random_data;
  random_data.reserve(sizeof(bootloader_message));
  generate_n(back_inserter(random_data), sizeof(bootloader_message), []() { return rand() % 128; });

  bootloader_message boot;
  memcpy(&boot, random_data.c_str(), random_data.size());

  std::string err;
  ASSERT_TRUE(write_bootloader_message(boot, &err)) << "Failed to write BCB: " << err;
  VerifyBootloaderMessage(random_data);

  ASSERT_TRUE(clear_bootloader_message(&err)) << "Failed to clear BCB: " << err;
  VerifyBootloaderMessage(std::string(sizeof(bootloader_message), '\0'));

  std::string message = "--update_message=abc value";
  std::string message_in_bcb = "recovery\n--update_message=abc value\n";
  SetupOrClearBcb(true, message, message_in_bcb);

  SetupOrClearBcb(false, "", "");

  TemporaryFile wipe_package;
  ASSERT_TRUE(android::base::WriteStringToFile(std::string(345, 'a'), wipe_package.path));

  // It's expected to store a wipe package in /misc, with the package size passed to recovery.
  message = "--wipe_ab\n--wipe_package="s + wipe_package.path + "\n--reason=wipePackage"s;
  message_in_bcb = "recovery\n--wipe_ab\n--wipe_package_size=345\n--reason=wipePackage\n";
  SetupOrClearBcb(true, message, message_in_bcb);
}
