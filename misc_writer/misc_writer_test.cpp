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

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <android-base/file.h>
#include <bootloader_message/bootloader_message.h>
#include <gtest/gtest.h>

#include "misc_writer/misc_writer.h"

using namespace std::string_literals;

namespace android {
namespace hardware {
namespace google {
namespace pixel {

class MiscWriterTest : public ::testing::Test {
 protected:
  void TearDown() override {
    // Clear the vendor space.
    auto zeros = std::string(WIPE_PACKAGE_OFFSET_IN_MISC - VENDOR_SPACE_OFFSET_IN_MISC, 0);
    std::string err;
    ASSERT_TRUE(MiscWriter::WriteMiscPartitionVendorSpace(zeros.data(), zeros.size(), 0, &err))
        << err;
  }

  void CheckMiscPartitionVendorSpaceContent(size_t offset, const std::string& expected);

  std::unique_ptr<MiscWriter> misc_writer_;
};

void MiscWriterTest::CheckMiscPartitionVendorSpaceContent(size_t offset,
                                                          const std::string& expected) {
  ASSERT_TRUE(MiscWriter::OffsetAndSizeInVendorSpace(offset, expected.size()));
  std::string err;
  auto misc_blk_device = get_misc_blk_device(&err);
  ASSERT_FALSE(misc_blk_device.empty());
  android::base::unique_fd fd(open(misc_blk_device.c_str(), O_RDONLY));
  ASSERT_NE(-1, fd);

  std::string content(expected.size(), 0);
  ASSERT_TRUE(android::base::ReadFullyAtOffset(fd, content.data(), content.size(),
                                               VENDOR_SPACE_OFFSET_IN_MISC + offset));
  ASSERT_EQ(expected, content);
}

TEST_F(MiscWriterTest, SetClearDarkTheme) {
  misc_writer_ = std::make_unique<MiscWriter>(MiscWriterActions::kSetDarkThemeFlag);
  ASSERT_TRUE(misc_writer_);
  ASSERT_TRUE(misc_writer_->PerformAction());
  std::string expected = "theme-dark";
  CheckMiscPartitionVendorSpaceContent(0, expected);

  misc_writer_ = std::make_unique<MiscWriter>(MiscWriterActions::kClearDarkThemeFlag);
  ASSERT_TRUE(misc_writer_->PerformAction());
  std::string zeros(expected.size(), 0);
  CheckMiscPartitionVendorSpaceContent(0, zeros);
}

TEST_F(MiscWriterTest, SetClearDarkTheme_OffsetOverride) {
  misc_writer_ = std::make_unique<MiscWriter>(MiscWriterActions::kSetDarkThemeFlag);
  size_t offset = 12360;
  ASSERT_TRUE(misc_writer_->PerformAction(offset));
  std::string expected = "theme-dark";
  CheckMiscPartitionVendorSpaceContent(offset, expected);

  misc_writer_ = std::make_unique<MiscWriter>(MiscWriterActions::kClearDarkThemeFlag);
  ASSERT_TRUE(misc_writer_->PerformAction(offset));
  std::string zeros(expected.size(), 0);
  CheckMiscPartitionVendorSpaceContent(offset, zeros);
}

TEST_F(MiscWriterTest, SetClearSota) {
  misc_writer_ = std::make_unique<MiscWriter>(MiscWriterActions::kSetSotaFlag);
  ASSERT_TRUE(misc_writer_);
  ASSERT_TRUE(misc_writer_->PerformAction());
  std::string expected = "enable-sota";
  CheckMiscPartitionVendorSpaceContent(32, expected);

  // Test we can write to the override offset.
  size_t override_offset = 12360;
  ASSERT_FALSE(misc_writer_->PerformAction(override_offset));
  CheckMiscPartitionVendorSpaceContent(override_offset, expected);

  misc_writer_ = std::make_unique<MiscWriter>(MiscWriterActions::kClearSotaFlag);
  ASSERT_TRUE(misc_writer_->PerformAction());
  std::string zeros(expected.size(), 0);
  CheckMiscPartitionVendorSpaceContent(32, zeros);
}

TEST_F(MiscWriterTest, WriteMiscPartitionVendorSpace) {
  std::string kTestMessage = "kTestMessage";
  std::string err;
  ASSERT_TRUE(
      MiscWriter::WriteMiscPartitionVendorSpace(kTestMessage.data(), kTestMessage.size(), 0, &err));

  CheckMiscPartitionVendorSpaceContent(0, kTestMessage);

  // Write with an offset.
  ASSERT_TRUE(MiscWriter::WriteMiscPartitionVendorSpace("\x00\x00", 2, 5, &err));
  CheckMiscPartitionVendorSpaceContent(0, "kTest\x00\x00ssage"s);

  // Write with the right size.
  auto start_offset =
      WIPE_PACKAGE_OFFSET_IN_MISC - VENDOR_SPACE_OFFSET_IN_MISC - kTestMessage.size();
  ASSERT_TRUE(MiscWriter::WriteMiscPartitionVendorSpace(kTestMessage.data(), kTestMessage.size(),
                                                        start_offset, &err));

  // Out-of-bound write.
  ASSERT_FALSE(MiscWriter::WriteMiscPartitionVendorSpace(kTestMessage.data(), kTestMessage.size(),
                                                         start_offset + 1, &err));

  // Message won't fit.
  std::string long_message(WIPE_PACKAGE_OFFSET_IN_MISC - VENDOR_SPACE_OFFSET_IN_MISC + 1, 'a');
  ASSERT_FALSE(
      MiscWriter::WriteMiscPartitionVendorSpace(long_message.data(), long_message.size(), 0, &err));
}

}  // namespace pixel
}  // namespace google
}  // namespace hardware
}  // namespace android
