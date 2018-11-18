/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agree to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>

#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>
#include <bsdiff/bsdiff.h>
#include <gtest/gtest.h>
#include <openssl/sha.h>

#include "applypatch/applypatch_modes.h"
#include "common/test_constants.h"
#include "otautil/paths.h"
#include "otautil/print_sha1.h"
#include "otautil/sysutil.h"

using namespace std::string_literals;

// Loads a given partition and returns a string of form "EMMC:name:size:hash".
static std::string GetEmmcTargetString(const std::string& filename,
                                       const std::string& display_name = "") {
  std::string data;
  if (!android::base::ReadFileToString(filename, &data)) {
    PLOG(ERROR) << "Failed to read " << filename;
    return {};
  }

  uint8_t digest[SHA_DIGEST_LENGTH];
  SHA1(reinterpret_cast<const uint8_t*>(data.c_str()), data.size(), digest);

  return "EMMC:"s + (display_name.empty() ? filename : display_name) + ":" +
         std::to_string(data.size()) + ":" + print_sha1(digest);
}

class ApplyPatchModesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    source = GetEmmcTargetString(from_testdata_base("boot.img"));
    ASSERT_FALSE(source.empty());

    std::string recovery_file = from_testdata_base("recovery.img");
    recovery = GetEmmcTargetString(recovery_file);
    ASSERT_FALSE(recovery.empty());

    ASSERT_TRUE(android::base::WriteStringToFile("", patched_file_.path));
    target = GetEmmcTargetString(recovery_file, patched_file_.path);
    ASSERT_FALSE(target.empty());

    Paths::Get().set_cache_temp_source(cache_source_.path);
  }

  std::string source;
  std::string target;
  std::string recovery;

 private:
  TemporaryFile cache_source_;
  TemporaryFile patched_file_;
};

static int InvokeApplyPatchModes(const std::vector<std::string>& args) {
  auto args_to_call = StringVectorToNullTerminatedArray(args);
  return applypatch_modes(args_to_call.size() - 1, args_to_call.data());
}

static void VerifyPatchedTarget(const std::string& target) {
  std::vector<std::string> pieces = android::base::Split(target, ":");
  ASSERT_EQ(4, pieces.size());
  ASSERT_EQ("EMMC", pieces[0]);

  std::string patched_emmc = GetEmmcTargetString(pieces[1]);
  ASSERT_FALSE(patched_emmc.empty());
  ASSERT_EQ(target, patched_emmc);
}

TEST_F(ApplyPatchModesTest, InvalidArgs) {
  // At least two args (including the filename).
  ASSERT_EQ(2, InvokeApplyPatchModes({ "applypatch" }));

  // Unrecognized args.
  ASSERT_EQ(2, InvokeApplyPatchModes({ "applypatch", "-x" }));
}

TEST_F(ApplyPatchModesTest, PatchModeEmmcTarget) {
  std::vector<std::string> args{
    "applypatch",
    "--bonus",
    from_testdata_base("bonus.file"),
    "--patch",
    from_testdata_base("recovery-from-boot.p"),
    "--target",
    target,
    "--source",
    source,
  };
  ASSERT_EQ(0, InvokeApplyPatchModes(args));
  VerifyPatchedTarget(target);
}

// Tests patching an eMMC target without a separate bonus file (i.e. recovery-from-boot patch has
// everything).
TEST_F(ApplyPatchModesTest, PatchModeEmmcTargetWithoutBonusFile) {
  std::vector<std::string> args{
    "applypatch", "--patch", from_testdata_base("recovery-from-boot-with-bonus.p"),
    "--target",   target,    "--source",
    source,
  };

  ASSERT_EQ(0, InvokeApplyPatchModes(args));
  VerifyPatchedTarget(target);
}

// Ensures that applypatch works with a bsdiff based recovery-from-boot.p.
TEST_F(ApplyPatchModesTest, PatchModeEmmcTargetWithBsdiffPatch) {
  // Generate the bsdiff patch of recovery-from-boot.p.
  std::string src_content;
  ASSERT_TRUE(android::base::ReadFileToString(from_testdata_base("boot.img"), &src_content));

  std::string tgt_content;
  ASSERT_TRUE(android::base::ReadFileToString(from_testdata_base("recovery.img"), &tgt_content));

  TemporaryFile patch_file;
  ASSERT_EQ(0,
            bsdiff::bsdiff(reinterpret_cast<const uint8_t*>(src_content.data()), src_content.size(),
                           reinterpret_cast<const uint8_t*>(tgt_content.data()), tgt_content.size(),
                           patch_file.path, nullptr));

  std::vector<std::string> args{
    "applypatch", "--patch", patch_file.path, "--target", target, "--source", source,
  };
  ASSERT_EQ(0, InvokeApplyPatchModes(args));
  VerifyPatchedTarget(target);
}

TEST_F(ApplyPatchModesTest, PatchModeInvalidArgs) {
  // Invalid bonus file.
  std::vector<std::string> args{
    "applypatch", "--bonus", "/doesntexist", "--patch", from_testdata_base("recovery-from-boot.p"),
    "--target",   target,    "--source",     source,
  };
  ASSERT_NE(0, InvokeApplyPatchModes(args));

  // With bonus file, but missing args.
  ASSERT_NE(0,
            InvokeApplyPatchModes({ "applypatch", "--bonus", from_testdata_base("bonus.file") }));
}

TEST_F(ApplyPatchModesTest, FlashMode) {
  std::vector<std::string> args{
    "applypatch", "--flash", from_testdata_base("recovery.img"), "--target", target,
  };
  ASSERT_EQ(0, InvokeApplyPatchModes(args));
  VerifyPatchedTarget(target);
}

TEST_F(ApplyPatchModesTest, FlashModeInvalidArgs) {
  std::vector<std::string> args{
    "applypatch", "--bonus", from_testdata_base("bonus.file"), "--flash", source,
    "--target",   target,
  };
  ASSERT_NE(0, InvokeApplyPatchModes(args));
}

TEST_F(ApplyPatchModesTest, CheckMode) {
  ASSERT_EQ(0, InvokeApplyPatchModes({ "applypatch", "--check", recovery }));
  ASSERT_EQ(0, InvokeApplyPatchModes({ "applypatch", "--check", source }));
}

TEST_F(ApplyPatchModesTest, CheckModeInvalidArgs) {
  ASSERT_EQ(2, InvokeApplyPatchModes({ "applypatch", "--check" }));
}

TEST_F(ApplyPatchModesTest, CheckModeNonEmmcTarget) {
  ASSERT_NE(0, InvokeApplyPatchModes({ "applypatch", "--check", from_testdata_base("boot.img") }));
}

TEST_F(ApplyPatchModesTest, ShowLicenses) {
  ASSERT_EQ(0, InvokeApplyPatchModes({ "applypatch", "--license" }));
}
