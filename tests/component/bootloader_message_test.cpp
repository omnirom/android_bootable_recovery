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

#include <string>
#include <vector>

#include <android-base/strings.h>
#include <bootloader_message/bootloader_message.h>
#include <gtest/gtest.h>

#include "common/component_test_util.h"

class BootloaderMessageTest : public ::testing::Test {
 protected:
  BootloaderMessageTest() : has_misc(true) {}

  virtual void SetUp() override {
    has_misc = parse_misc();
  }

  virtual void TearDown() override {
    // Clear the BCB.
    if (has_misc) {
      std::string err;
      ASSERT_TRUE(clear_bootloader_message(&err)) << "Failed to clear BCB: " << err;
    }
  }

  bool has_misc;
};

TEST_F(BootloaderMessageTest, clear_bootloader_message) {
  if (!has_misc) {
    GTEST_LOG_(INFO) << "Test skipped due to no /misc partition found on the device.";
    return;
  }

  // Clear the BCB.
  std::string err;
  ASSERT_TRUE(clear_bootloader_message(&err)) << "Failed to clear BCB: " << err;

  // Verify the content.
  bootloader_message boot;
  ASSERT_TRUE(read_bootloader_message(&boot, &err)) << "Failed to read BCB: " << err;

  // All the bytes should be cleared.
  ASSERT_EQ(std::string(sizeof(boot), '\0'),
            std::string(reinterpret_cast<const char*>(&boot), sizeof(boot)));
}

TEST_F(BootloaderMessageTest, read_and_write_bootloader_message) {
  if (!has_misc) {
    GTEST_LOG_(INFO) << "Test skipped due to no /misc partition found on the device.";
    return;
  }

  // Write the BCB.
  bootloader_message boot = {};
  strlcpy(boot.command, "command", sizeof(boot.command));
  strlcpy(boot.recovery, "message1\nmessage2\n", sizeof(boot.recovery));
  strlcpy(boot.status, "status1", sizeof(boot.status));

  std::string err;
  ASSERT_TRUE(write_bootloader_message(boot, &err)) << "Failed to write BCB: " << err;

  // Read and verify.
  bootloader_message boot_verify;
  ASSERT_TRUE(read_bootloader_message(&boot_verify, &err)) << "Failed to read BCB: " << err;

  ASSERT_EQ(std::string(reinterpret_cast<const char*>(&boot), sizeof(boot)),
            std::string(reinterpret_cast<const char*>(&boot_verify), sizeof(boot_verify)));
}

TEST_F(BootloaderMessageTest, write_bootloader_message_options) {
  if (!has_misc) {
    GTEST_LOG_(INFO) << "Test skipped due to no /misc partition found on the device.";
    return;
  }

  // Write the options to BCB.
  std::vector<std::string> options = { "option1", "option2" };
  std::string err;
  ASSERT_TRUE(write_bootloader_message(options, &err)) << "Failed to write BCB: " << err;

  // Inject some bytes into boot, which should be overwritten while reading.
  bootloader_message boot;
  strlcpy(boot.recovery, "random message", sizeof(boot.recovery));
  strlcpy(boot.reserved, "reserved bytes", sizeof(boot.reserved));

  ASSERT_TRUE(read_bootloader_message(&boot, &err)) << "Failed to read BCB: " << err;

  // Verify that command and recovery fields should be set.
  ASSERT_EQ("boot-recovery", std::string(boot.command));
  std::string expected = "recovery\n" + android::base::Join(options, "\n") + "\n";
  ASSERT_EQ(expected, std::string(boot.recovery));

  // The rest should be cleared.
  ASSERT_EQ(std::string(sizeof(boot.status), '\0'), std::string(boot.status, sizeof(boot.status)));
  ASSERT_EQ(std::string(sizeof(boot.stage), '\0'), std::string(boot.stage, sizeof(boot.stage)));
  ASSERT_EQ(std::string(sizeof(boot.reserved), '\0'),
            std::string(boot.reserved, sizeof(boot.reserved)));
}

TEST_F(BootloaderMessageTest, write_bootloader_message_options_empty) {
  if (!has_misc) {
    GTEST_LOG_(INFO) << "Test skipped due to no /misc partition found on the device.";
    return;
  }

  // Write empty vector.
  std::vector<std::string> options;
  std::string err;
  ASSERT_TRUE(write_bootloader_message(options, &err)) << "Failed to write BCB: " << err;

  // Read and verify.
  bootloader_message boot;
  ASSERT_TRUE(read_bootloader_message(&boot, &err)) << "Failed to read BCB: " << err;

  // command and recovery fields should be set.
  ASSERT_EQ("boot-recovery", std::string(boot.command));
  ASSERT_EQ("recovery\n", std::string(boot.recovery));

  // The rest should be cleared.
  ASSERT_EQ(std::string(sizeof(boot.status), '\0'), std::string(boot.status, sizeof(boot.status)));
  ASSERT_EQ(std::string(sizeof(boot.stage), '\0'), std::string(boot.stage, sizeof(boot.stage)));
  ASSERT_EQ(std::string(sizeof(boot.reserved), '\0'),
            std::string(boot.reserved, sizeof(boot.reserved)));
}

TEST_F(BootloaderMessageTest, write_bootloader_message_options_long) {
  if (!has_misc) {
    GTEST_LOG_(INFO) << "Test skipped due to no /misc partition found on the device.";
    return;
  }

  // Write super long message.
  std::vector<std::string> options;
  for (int i = 0; i < 100; i++) {
    options.push_back("option: " + std::to_string(i));
  }

  std::string err;
  ASSERT_TRUE(write_bootloader_message(options, &err)) << "Failed to write BCB: " << err;

  // Read and verify.
  bootloader_message boot;
  ASSERT_TRUE(read_bootloader_message(&boot, &err)) << "Failed to read BCB: " << err;

  // Make sure it's long enough.
  std::string expected = "recovery\n" + android::base::Join(options, "\n") + "\n";
  ASSERT_GE(expected.size(), sizeof(boot.recovery));

  // command and recovery fields should be set.
  ASSERT_EQ("boot-recovery", std::string(boot.command));
  ASSERT_EQ(expected.substr(0, sizeof(boot.recovery) - 1), std::string(boot.recovery));
  ASSERT_EQ('\0', boot.recovery[sizeof(boot.recovery) - 1]);

  // The rest should be cleared.
  ASSERT_EQ(std::string(sizeof(boot.status), '\0'), std::string(boot.status, sizeof(boot.status)));
  ASSERT_EQ(std::string(sizeof(boot.stage), '\0'), std::string(boot.stage, sizeof(boot.stage)));
  ASSERT_EQ(std::string(sizeof(boot.reserved), '\0'),
            std::string(boot.reserved, sizeof(boot.reserved)));
}

TEST_F(BootloaderMessageTest, update_bootloader_message) {
  if (!has_misc) {
    GTEST_LOG_(INFO) << "Test skipped due to no /misc partition found on the device.";
    return;
  }

  // Inject some bytes into boot, which should be not overwritten later.
  bootloader_message boot;
  strlcpy(boot.recovery, "random message", sizeof(boot.recovery));
  strlcpy(boot.reserved, "reserved bytes", sizeof(boot.reserved));
  std::string err;
  ASSERT_TRUE(write_bootloader_message(boot, &err)) << "Failed to write BCB: " << err;

  // Update the BCB message.
  std::vector<std::string> options = { "option1", "option2" };
  ASSERT_TRUE(update_bootloader_message(options, &err)) << "Failed to update BCB: " << err;

  bootloader_message boot_verify;
  ASSERT_TRUE(read_bootloader_message(&boot_verify, &err)) << "Failed to read BCB: " << err;

  // Verify that command and recovery fields should be set.
  ASSERT_EQ("boot-recovery", std::string(boot_verify.command));
  std::string expected = "recovery\n" + android::base::Join(options, "\n") + "\n";
  ASSERT_EQ(expected, std::string(boot_verify.recovery));

  // The rest should be intact.
  ASSERT_EQ(std::string(boot.status), std::string(boot_verify.status));
  ASSERT_EQ(std::string(boot.stage), std::string(boot_verify.stage));
  ASSERT_EQ(std::string(boot.reserved), std::string(boot_verify.reserved));
}
