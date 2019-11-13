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
#include <string_view>
#include <vector>

#include <android-base/file.h>
#include <android-base/strings.h>
#include <bootloader_message/bootloader_message.h>
#include <gtest/gtest.h>

using namespace std::string_literals;

extern void SetMiscBlockDeviceForTest(std::string_view misc_device);

TEST(BootloaderMessageTest, read_and_write_bootloader_message) {
  TemporaryFile temp_misc;

  // Write the BCB.
  bootloader_message boot = {};
  strlcpy(boot.command, "command", sizeof(boot.command));
  strlcpy(boot.recovery, "message1\nmessage2\n", sizeof(boot.recovery));
  strlcpy(boot.status, "status1", sizeof(boot.status));

  std::string err;
  ASSERT_TRUE(write_bootloader_message_to(boot, temp_misc.path, &err))
      << "Failed to write BCB: " << err;

  // Read and verify.
  bootloader_message boot_verify;
  ASSERT_TRUE(read_bootloader_message_from(&boot_verify, temp_misc.path, &err))
      << "Failed to read BCB: " << err;

  ASSERT_EQ(std::string(reinterpret_cast<const char*>(&boot), sizeof(boot)),
            std::string(reinterpret_cast<const char*>(&boot_verify), sizeof(boot_verify)));
}

TEST(BootloaderMessageTest, update_bootloader_message_in_struct) {
  // Write the options to BCB.
  std::vector<std::string> options = { "option1", "option2" };

  bootloader_message boot = {};
  // Inject some bytes into boot.
  strlcpy(boot.recovery, "random message", sizeof(boot.recovery));
  strlcpy(boot.status, "status bytes", sizeof(boot.status));
  strlcpy(boot.stage, "stage bytes", sizeof(boot.stage));
  strlcpy(boot.reserved, "reserved bytes", sizeof(boot.reserved));

  ASSERT_TRUE(update_bootloader_message_in_struct(&boot, options));

  // Verify that command and recovery fields should be set.
  ASSERT_EQ("boot-recovery", std::string(boot.command));
  std::string expected = "recovery\n" + android::base::Join(options, "\n") + "\n";
  ASSERT_EQ(expected, std::string(boot.recovery));

  // The rest should be intact.
  ASSERT_EQ("status bytes", std::string(boot.status));
  ASSERT_EQ("stage bytes", std::string(boot.stage));
  ASSERT_EQ("reserved bytes", std::string(boot.reserved));
}

TEST(BootloaderMessageTest, update_bootloader_message_recovery_options_empty) {
  // Write empty vector.
  std::vector<std::string> options;

  // Read and verify.
  bootloader_message boot = {};
  ASSERT_TRUE(update_bootloader_message_in_struct(&boot, options));

  // command and recovery fields should be set.
  ASSERT_EQ("boot-recovery", std::string(boot.command));
  ASSERT_EQ("recovery\n", std::string(boot.recovery));

  // The rest should be empty.
  ASSERT_EQ(std::string(sizeof(boot.status), '\0'), std::string(boot.status, sizeof(boot.status)));
  ASSERT_EQ(std::string(sizeof(boot.stage), '\0'), std::string(boot.stage, sizeof(boot.stage)));
  ASSERT_EQ(std::string(sizeof(boot.reserved), '\0'),
            std::string(boot.reserved, sizeof(boot.reserved)));
}

TEST(BootloaderMessageTest, update_bootloader_message_recovery_options_long) {
  // Write super long message.
  std::vector<std::string> options;
  for (int i = 0; i < 100; i++) {
    options.push_back("option: " + std::to_string(i));
  }

  // Read and verify.
  bootloader_message boot = {};
  ASSERT_TRUE(update_bootloader_message_in_struct(&boot, options));

  // Make sure it's long enough.
  std::string expected = "recovery\n" + android::base::Join(options, "\n") + "\n";
  ASSERT_GE(expected.size(), sizeof(boot.recovery));

  // command and recovery fields should be set.
  ASSERT_EQ("boot-recovery", std::string(boot.command));
  ASSERT_EQ(expected.substr(0, sizeof(boot.recovery) - 1), std::string(boot.recovery));
  ASSERT_EQ('\0', boot.recovery[sizeof(boot.recovery) - 1]);

  // The rest should be empty.
  ASSERT_EQ(std::string(sizeof(boot.status), '\0'), std::string(boot.status, sizeof(boot.status)));
  ASSERT_EQ(std::string(sizeof(boot.stage), '\0'), std::string(boot.stage, sizeof(boot.stage)));
  ASSERT_EQ(std::string(sizeof(boot.reserved), '\0'),
            std::string(boot.reserved, sizeof(boot.reserved)));
}
