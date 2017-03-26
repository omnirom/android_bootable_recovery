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
#include <openssl/sha.h>

#include "applypatch/applypatch.h"
#include "applypatch/applypatch_modes.h"
#include "common/test_constants.h"
#include "print_sha1.h"

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
  std::string content;
  content.reserve(1024);
  for (size_t i = 0; i < 1024; i++) {
    content[i] = rand() % 256;
  }
  ASSERT_TRUE(android::base::WriteStringToFile(content, fname));
}

static bool file_cmp(const std::string& f1, const std::string& f2) {
  std::string c1;
  android::base::ReadFileToString(f1, &c1);
  std::string c2;
  android::base::ReadFileToString(f2, &c2);
  return c1 == c2;
}

class ApplyPatchTest : public ::testing::Test {
 public:
  static void SetUpTestCase() {
    // set up files
    old_file = from_testdata_base("old.file");
    new_file = from_testdata_base("new.file");
    patch_file = from_testdata_base("patch.bsdiff");
    rand_file = "/cache/applypatch_test_rand.file";
    cache_file = "/cache/saved.file";

    // write stuff to rand_file
    ASSERT_TRUE(android::base::WriteStringToFile("hello", rand_file));

    // set up SHA constants
    sha1sum(old_file, &old_sha1, &old_size);
    sha1sum(new_file, &new_sha1, &new_size);
    srand(time(nullptr));
    bad_sha1_a = android::base::StringPrintf("%040x", rand());
    bad_sha1_b = android::base::StringPrintf("%040x", rand());
  }

  static std::string old_file;
  static std::string new_file;
  static std::string rand_file;
  static std::string cache_file;
  static std::string patch_file;

  static std::string old_sha1;
  static std::string new_sha1;
  static std::string bad_sha1_a;
  static std::string bad_sha1_b;

  static size_t old_size;
  static size_t new_size;
};

std::string ApplyPatchTest::old_file;
std::string ApplyPatchTest::new_file;

static void cp(const std::string& src, const std::string& tgt) {
  std::string cmd = "cp " + src + " " + tgt;
  system(cmd.c_str());
}

static void backup_old() {
  cp(ApplyPatchTest::old_file, ApplyPatchTest::cache_file);
}

static void restore_old() {
  cp(ApplyPatchTest::cache_file, ApplyPatchTest::old_file);
}

class ApplyPatchCacheTest : public ApplyPatchTest {
 public:
  virtual void SetUp() {
    backup_old();
  }

  virtual void TearDown() {
    restore_old();
  }
};

class ApplyPatchFullTest : public ApplyPatchCacheTest {
 public:
  static void SetUpTestCase() {
    ApplyPatchTest::SetUpTestCase();

    output_f = new TemporaryFile();
    output_loc = std::string(output_f->path);

    struct FileContents fc;

    ASSERT_EQ(0, LoadFileContents(&rand_file[0], &fc));
    patches.push_back(
        std::make_unique<Value>(VAL_BLOB, std::string(fc.data.begin(), fc.data.end())));

    ASSERT_EQ(0, LoadFileContents(&patch_file[0], &fc));
    patches.push_back(
        std::make_unique<Value>(VAL_BLOB, std::string(fc.data.begin(), fc.data.end())));
  }

  static void TearDownTestCase() {
    delete output_f;
    patches.clear();
  }

  static std::vector<std::unique_ptr<Value>> patches;
  static TemporaryFile* output_f;
  static std::string output_loc;
};

class ApplyPatchDoubleCacheTest : public ApplyPatchFullTest {
 public:
  virtual void SetUp() {
    ApplyPatchCacheTest::SetUp();
    cp(cache_file, "/cache/reallysaved.file");
  }

  virtual void TearDown() {
    cp("/cache/reallysaved.file", cache_file);
    ApplyPatchCacheTest::TearDown();
  }
};

std::string ApplyPatchTest::rand_file;
std::string ApplyPatchTest::patch_file;
std::string ApplyPatchTest::cache_file;
std::string ApplyPatchTest::old_sha1;
std::string ApplyPatchTest::new_sha1;
std::string ApplyPatchTest::bad_sha1_a;
std::string ApplyPatchTest::bad_sha1_b;
size_t ApplyPatchTest::old_size;
size_t ApplyPatchTest::new_size;

std::vector<std::unique_ptr<Value>> ApplyPatchFullTest::patches;
TemporaryFile* ApplyPatchFullTest::output_f;
std::string ApplyPatchFullTest::output_loc;

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

TEST_F(ApplyPatchCacheTest, CheckCacheCorruptedSingle) {
  mangle_file(old_file);
  std::vector<std::string> sha1s = { old_sha1 };
  ASSERT_EQ(0, applypatch_check(&old_file[0], sha1s));
}

TEST_F(ApplyPatchCacheTest, CheckCacheCorruptedMultiple) {
  mangle_file(old_file);
  std::vector<std::string> sha1s = { bad_sha1_a, old_sha1, bad_sha1_b };
  ASSERT_EQ(0, applypatch_check(&old_file[0], sha1s));
}

TEST_F(ApplyPatchCacheTest, CheckCacheCorruptedFailure) {
  mangle_file(old_file);
  std::vector<std::string> sha1s = { bad_sha1_a, bad_sha1_b };
  ASSERT_NE(0, applypatch_check(&old_file[0], sha1s));
}

TEST_F(ApplyPatchCacheTest, CheckCacheMissingSingle) {
  unlink(&old_file[0]);
  std::vector<std::string> sha1s = { old_sha1 };
  ASSERT_EQ(0, applypatch_check(&old_file[0], sha1s));
}

TEST_F(ApplyPatchCacheTest, CheckCacheMissingMultiple) {
  unlink(&old_file[0]);
  std::vector<std::string> sha1s = { bad_sha1_a, old_sha1, bad_sha1_b };
  ASSERT_EQ(0, applypatch_check(&old_file[0], sha1s));
}

TEST_F(ApplyPatchCacheTest, CheckCacheMissingFailure) {
  unlink(&old_file[0]);
  std::vector<std::string> sha1s = { bad_sha1_a, bad_sha1_b };
  ASSERT_NE(0, applypatch_check(&old_file[0], sha1s));
}

TEST(ApplyPatchModesTest, InvalidArgs) {
  // At least two args (including the filename).
  ASSERT_EQ(2, applypatch_modes(1, (const char* []){ "applypatch" }));

  // Unrecognized args.
  ASSERT_EQ(2, applypatch_modes(2, (const char* []){ "applypatch", "-x" }));
}

TEST(ApplyPatchModesTest, PatchModeEmmcTarget) {
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

TEST(ApplyPatchModesTest, PatchModeInvalidArgs) {
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

TEST(ApplyPatchModesTest, CheckModeInvalidArgs) {
  // Insufficient args.
  ASSERT_EQ(2, applypatch_modes(2, (const char* []){ "applypatch", "-c" }));
}

TEST(ApplyPatchModesTest, SpaceModeInvalidArgs) {
  // Insufficient args.
  ASSERT_EQ(2, applypatch_modes(2, (const char* []){ "applypatch", "-s" }));

  // Invalid bytes arg.
  ASSERT_EQ(1, applypatch_modes(3, (const char* []){ "applypatch", "-s", "x" }));

  // 0 is invalid.
  ASSERT_EQ(1, applypatch_modes(3, (const char* []){ "applypatch", "-s", "0" }));

  // 0x10 is fine.
  ASSERT_EQ(0, applypatch_modes(3, (const char* []){ "applypatch", "-s", "0x10" }));
}

TEST(ApplyPatchModesTest, ShowLicenses) {
  ASSERT_EQ(0, applypatch_modes(2, (const char* []){ "applypatch", "-l" }));
}
