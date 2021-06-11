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

#include <fcntl.h>
#include <gtest/gtest.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <time.h>

#include <memory>
#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/stringprintf.h>
#include <android-base/test_utils.h>
#include <bsdiff/bsdiff.h>
#include <openssl/sha.h>

#include "applypatch/applypatch.h"
#include "applypatch/applypatch_modes.h"
#include "common/test_constants.h"
#include "otautil/cache_location.h"
#include "otautil/print_sha1.h"

using namespace std::string_literals;

static void sha1sum(const std::string& fname, std::string* sha1, size_t* fsize = nullptr) {
  ASSERT_NE(nullptr, sha1);

  std::string data;
  ASSERT_TRUE(android::base::ReadFileToString(fname, &data));

  if (fsize != nullptr) {
    *fsize = data.size();
  }

  uint8_t digest[SHA_DIGEST_LENGTH];
  SHA1(reinterpret_cast<const uint8_t*>(data.c_str()), data.size(), digest);
  *sha1 = print_sha1(digest);
}

static void mangle_file(const std::string& fname) {
  std::string content(1024, '\0');
  for (size_t i = 0; i < 1024; i++) {
    content[i] = rand() % 256;
  }
  ASSERT_TRUE(android::base::WriteStringToFile(content, fname));
}

class ApplyPatchTest : public ::testing::Test {
 public:
  virtual void SetUp() override {
    // set up files
    old_file = from_testdata_base("old.file");
    new_file = from_testdata_base("new.file");
    nonexistent_file = from_testdata_base("nonexistent.file");

    // set up SHA constants
    sha1sum(old_file, &old_sha1, &old_size);
    sha1sum(new_file, &new_sha1, &new_size);
    srand(time(nullptr));
    bad_sha1_a = android::base::StringPrintf("%040x", rand());
    bad_sha1_b = android::base::StringPrintf("%040x", rand());
  }

  std::string old_file;
  std::string new_file;
  std::string nonexistent_file;

  std::string old_sha1;
  std::string new_sha1;
  std::string bad_sha1_a;
  std::string bad_sha1_b;

  size_t old_size;
  size_t new_size;
};

class ApplyPatchCacheTest : public ApplyPatchTest {
 protected:
  void SetUp() override {
    ApplyPatchTest::SetUp();
    CacheLocation::location().set_cache_temp_source(old_file);
  }
};

class ApplyPatchModesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    CacheLocation::location().set_cache_temp_source(cache_source.path);
  }

  TemporaryFile cache_source;
};

TEST_F(ApplyPatchTest, CheckModeSkip) {
  std::vector<std::string> sha1s;
  ASSERT_EQ(0, applypatch_check(&old_file[0], sha1s));
}

TEST_F(ApplyPatchTest, CheckModeSingle) {
  std::vector<std::string> sha1s = { old_sha1 };
  ASSERT_EQ(0, applypatch_check(&old_file[0], sha1s));
}

TEST_F(ApplyPatchTest, CheckModeMultiple) {
  std::vector<std::string> sha1s = { bad_sha1_a, old_sha1, bad_sha1_b };
  ASSERT_EQ(0, applypatch_check(&old_file[0], sha1s));
}

TEST_F(ApplyPatchTest, CheckModeFailure) {
  std::vector<std::string> sha1s = { bad_sha1_a, bad_sha1_b };
  ASSERT_NE(0, applypatch_check(&old_file[0], sha1s));
}

TEST_F(ApplyPatchTest, CheckModeEmmcTarget) {
  // EMMC:old_file:size:sha1 should pass the check.
  std::string src_file =
      "EMMC:" + old_file + ":" + std::to_string(old_size) + ":" + old_sha1;
  std::vector<std::string> sha1s;
  ASSERT_EQ(0, applypatch_check(src_file.c_str(), sha1s));

  // EMMC:old_file:(size-1):sha1:(size+1):sha1 should fail the check.
  src_file = "EMMC:" + old_file + ":" + std::to_string(old_size - 1) + ":" + old_sha1 + ":" +
             std::to_string(old_size + 1) + ":" + old_sha1;
  ASSERT_EQ(1, applypatch_check(src_file.c_str(), sha1s));

  // EMMC:old_file:(size-1):sha1:size:sha1:(size+1):sha1 should pass the check.
  src_file = "EMMC:" + old_file + ":" +
             std::to_string(old_size - 1) + ":" + old_sha1 + ":" +
             std::to_string(old_size) + ":" + old_sha1 + ":" +
             std::to_string(old_size + 1) + ":" + old_sha1;
  ASSERT_EQ(0, applypatch_check(src_file.c_str(), sha1s));

  // EMMC:old_file:(size+1):sha1:(size-1):sha1:size:sha1 should pass the check.
  src_file = "EMMC:" + old_file + ":" +
             std::to_string(old_size + 1) + ":" + old_sha1 + ":" +
             std::to_string(old_size - 1) + ":" + old_sha1 + ":" +
             std::to_string(old_size) + ":" + old_sha1;
  ASSERT_EQ(0, applypatch_check(src_file.c_str(), sha1s));

  // EMMC:new_file:(size+1):old_sha1:(size-1):old_sha1:size:old_sha1:size:new_sha1
  // should pass the check.
  src_file = "EMMC:" + new_file + ":" +
             std::to_string(old_size + 1) + ":" + old_sha1 + ":" +
             std::to_string(old_size - 1) + ":" + old_sha1 + ":" +
             std::to_string(old_size) + ":" + old_sha1 + ":" +
             std::to_string(new_size) + ":" + new_sha1;
  ASSERT_EQ(0, applypatch_check(src_file.c_str(), sha1s));
}

TEST_F(ApplyPatchCacheTest, CheckCacheCorruptedSourceSingle) {
  TemporaryFile temp_file;
  mangle_file(temp_file.path);
  std::vector<std::string> sha1s_single = { old_sha1 };
  ASSERT_EQ(0, applypatch_check(temp_file.path, sha1s_single));
  ASSERT_EQ(0, applypatch_check(nonexistent_file.c_str(), sha1s_single));
}

TEST_F(ApplyPatchCacheTest, CheckCacheCorruptedSourceMultiple) {
  TemporaryFile temp_file;
  mangle_file(temp_file.path);
  std::vector<std::string> sha1s_multiple = { bad_sha1_a, old_sha1, bad_sha1_b };
  ASSERT_EQ(0, applypatch_check(temp_file.path, sha1s_multiple));
  ASSERT_EQ(0, applypatch_check(nonexistent_file.c_str(), sha1s_multiple));
}

TEST_F(ApplyPatchCacheTest, CheckCacheCorruptedSourceFailure) {
  TemporaryFile temp_file;
  mangle_file(temp_file.path);
  std::vector<std::string> sha1s_failure = { bad_sha1_a, bad_sha1_b };
  ASSERT_NE(0, applypatch_check(temp_file.path, sha1s_failure));
  ASSERT_NE(0, applypatch_check(nonexistent_file.c_str(), sha1s_failure));
}

TEST_F(ApplyPatchModesTest, InvalidArgs) {
  // At least two args (including the filename).
  ASSERT_EQ(2, applypatch_modes(1, (const char* []){ "applypatch" }));

  // Unrecognized args.
  ASSERT_EQ(2, applypatch_modes(2, (const char* []){ "applypatch", "-x" }));
}

TEST_F(ApplyPatchModesTest, PatchModeEmmcTarget) {
  std::string boot_img = from_testdata_base("boot.img");
  size_t boot_img_size;
  std::string boot_img_sha1;
  sha1sum(boot_img, &boot_img_sha1, &boot_img_size);

  std::string recovery_img = from_testdata_base("recovery.img");
  size_t size;
  std::string recovery_img_sha1;
  sha1sum(recovery_img, &recovery_img_sha1, &size);
  std::string recovery_img_size = std::to_string(size);

  std::string bonus_file = from_testdata_base("bonus.file");

  // applypatch -b <bonus-file> <src-file> <tgt-file> <tgt-sha1> <tgt-size> <src-sha1>:<patch>
  TemporaryFile tmp1;
  std::string src_file =
      "EMMC:" + boot_img + ":" + std::to_string(boot_img_size) + ":" + boot_img_sha1;
  std::string tgt_file = "EMMC:" + std::string(tmp1.path);
  std::string patch = boot_img_sha1 + ":" + from_testdata_base("recovery-from-boot.p");
  std::vector<const char*> args = {
    "applypatch",
    "-b",
    bonus_file.c_str(),
    src_file.c_str(),
    tgt_file.c_str(),
    recovery_img_sha1.c_str(),
    recovery_img_size.c_str(),
    patch.c_str()
  };
  ASSERT_EQ(0, applypatch_modes(args.size(), args.data()));

  // applypatch <src-file> <tgt-file> <tgt-sha1> <tgt-size> <src-sha1>:<patch>
  TemporaryFile tmp2;
  patch = boot_img_sha1 + ":" + from_testdata_base("recovery-from-boot-with-bonus.p");
  tgt_file = "EMMC:" + std::string(tmp2.path);
  std::vector<const char*> args2 = {
    "applypatch",
    src_file.c_str(),
    tgt_file.c_str(),
    recovery_img_sha1.c_str(),
    recovery_img_size.c_str(),
    patch.c_str()
  };
  ASSERT_EQ(0, applypatch_modes(args2.size(), args2.data()));

  // applypatch -b <bonus-file> <src-file> <tgt-file> <tgt-sha1> <tgt-size> \
  //               <src-sha1-fake>:<patch1> <src-sha1>:<patch2>
  TemporaryFile tmp3;
  tgt_file = "EMMC:" + std::string(tmp3.path);
  std::string bad_sha1_a = android::base::StringPrintf("%040x", rand());
  std::string bad_sha1_b = android::base::StringPrintf("%040x", rand());
  std::string patch1 = bad_sha1_a + ":" + from_testdata_base("recovery-from-boot.p");
  std::string patch2 = boot_img_sha1 + ":" + from_testdata_base("recovery-from-boot.p");
  std::string patch3 = bad_sha1_b + ":" + from_testdata_base("recovery-from-boot.p");
  std::vector<const char*> args3 = {
    "applypatch",
    "-b",
    bonus_file.c_str(),
    src_file.c_str(),
    tgt_file.c_str(),
    recovery_img_sha1.c_str(),
    recovery_img_size.c_str(),
    patch1.c_str(),
    patch2.c_str(),
    patch3.c_str()
  };
  ASSERT_EQ(0, applypatch_modes(args3.size(), args3.data()));
}

// Ensures that applypatch works with a bsdiff based recovery-from-boot.p.
TEST_F(ApplyPatchModesTest, PatchModeEmmcTargetWithBsdiffPatch) {
  std::string boot_img_file = from_testdata_base("boot.img");
  std::string boot_img_sha1;
  size_t boot_img_size;
  sha1sum(boot_img_file, &boot_img_sha1, &boot_img_size);

  std::string recovery_img_file = from_testdata_base("recovery.img");
  std::string recovery_img_sha1;
  size_t recovery_img_size;
  sha1sum(recovery_img_file, &recovery_img_sha1, &recovery_img_size);

  // Generate the bsdiff patch of recovery-from-boot.p.
  std::string src_content;
  ASSERT_TRUE(android::base::ReadFileToString(boot_img_file, &src_content));

  std::string tgt_content;
  ASSERT_TRUE(android::base::ReadFileToString(recovery_img_file, &tgt_content));

  TemporaryFile patch_file;
  ASSERT_EQ(0,
            bsdiff::bsdiff(reinterpret_cast<const uint8_t*>(src_content.data()), src_content.size(),
                           reinterpret_cast<const uint8_t*>(tgt_content.data()), tgt_content.size(),
                           patch_file.path, nullptr));

  // applypatch <src-file> <tgt-file> <tgt-sha1> <tgt-size> <src-sha1>:<patch>
  std::string src_file_arg =
      "EMMC:" + boot_img_file + ":" + std::to_string(boot_img_size) + ":" + boot_img_sha1;
  TemporaryFile tgt_file;
  std::string tgt_file_arg = "EMMC:"s + tgt_file.path;
  std::string recovery_img_size_arg = std::to_string(recovery_img_size);
  std::string patch_arg = boot_img_sha1 + ":" + patch_file.path;
  std::vector<const char*> args = { "applypatch",
                                    src_file_arg.c_str(),
                                    tgt_file_arg.c_str(),
                                    recovery_img_sha1.c_str(),
                                    recovery_img_size_arg.c_str(),
                                    patch_arg.c_str() };
  ASSERT_EQ(0, applypatch_modes(args.size(), args.data()));

  // Double check the patched recovery image.
  std::string tgt_file_sha1;
  size_t tgt_file_size;
  sha1sum(tgt_file.path, &tgt_file_sha1, &tgt_file_size);
  ASSERT_EQ(recovery_img_size, tgt_file_size);
  ASSERT_EQ(recovery_img_sha1, tgt_file_sha1);
}

TEST_F(ApplyPatchModesTest, PatchModeInvalidArgs) {
  // Invalid bonus file.
  ASSERT_NE(0, applypatch_modes(3, (const char* []){ "applypatch", "-b", "/doesntexist" }));

  std::string bonus_file = from_testdata_base("bonus.file");
  // With bonus file, but missing args.
  ASSERT_EQ(2, applypatch_modes(3, (const char* []){ "applypatch", "-b", bonus_file.c_str() }));

  std::string boot_img = from_testdata_base("boot.img");
  size_t boot_img_size;
  std::string boot_img_sha1;
  sha1sum(boot_img, &boot_img_sha1, &boot_img_size);

  std::string recovery_img = from_testdata_base("recovery.img");
  size_t size;
  std::string recovery_img_sha1;
  sha1sum(recovery_img, &recovery_img_sha1, &size);
  std::string recovery_img_size = std::to_string(size);

  // Bonus file is not supported in flash mode.
  // applypatch -b <bonus-file> <src-file> <tgt-file> <tgt-sha1> <tgt-size>
  TemporaryFile tmp4;
  std::vector<const char*> args4 = {
    "applypatch",
    "-b",
    bonus_file.c_str(),
    boot_img.c_str(),
    tmp4.path,
    recovery_img_sha1.c_str(),
    recovery_img_size.c_str()
  };
  ASSERT_NE(0, applypatch_modes(args4.size(), args4.data()));

  // Failed to parse patch args.
  TemporaryFile tmp5;
  std::string bad_arg1 =
      "invalid-sha1:filename" + from_testdata_base("recovery-from-boot-with-bonus.p");
  std::vector<const char*> args5 = {
    "applypatch",
    boot_img.c_str(),
    tmp5.path,
    recovery_img_sha1.c_str(),
    recovery_img_size.c_str(),
    bad_arg1.c_str()
  };
  ASSERT_NE(0, applypatch_modes(args5.size(), args5.data()));

  // Target size cannot be zero.
  TemporaryFile tmp6;
  std::string patch = boot_img_sha1 + ":" + from_testdata_base("recovery-from-boot-with-bonus.p");
  std::vector<const char*> args6 = {
    "applypatch",
    boot_img.c_str(),
    tmp6.path,
    recovery_img_sha1.c_str(),
    "0",  // target size
    patch.c_str()
  };
  ASSERT_NE(0, applypatch_modes(args6.size(), args6.data()));
}

TEST_F(ApplyPatchModesTest, CheckModeInvalidArgs) {
  // Insufficient args.
  ASSERT_EQ(2, applypatch_modes(2, (const char* []){ "applypatch", "-c" }));
}

TEST_F(ApplyPatchModesTest, ShowLicenses) {
  ASSERT_EQ(0, applypatch_modes(2, (const char* []){ "applypatch", "-l" }));
}
