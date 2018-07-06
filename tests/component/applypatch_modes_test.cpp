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
#include <android-base/stringprintf.h>
#include <android-base/test_utils.h>
#include <bsdiff/bsdiff.h>
#include <gtest/gtest.h>
#include <openssl/sha.h>
#include <zlib.h>

#include "applypatch/applypatch_modes.h"
#include "common/test_constants.h"
#include "otautil/paths.h"
#include "otautil/print_sha1.h"

using namespace std::string_literals;

// TODO(b/67849209) Remove after debug the flakiness.
static void DecompressAndDumpRecoveryImage(const std::string& image_path) {
  // Expected recovery_image structure
  // chunk normal:  45066 bytes
  // chunk deflate: 479442 bytes
  // chunk normal:  5199 bytes
  std::string recovery_content;
  ASSERT_TRUE(android::base::ReadFileToString(image_path, &recovery_content));
  ASSERT_GT(recovery_content.size(), 45066 + 5199);

  z_stream strm = {};
  strm.avail_in = recovery_content.size() - 45066 - 5199;
  strm.next_in =
      const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(recovery_content.data())) + 45066;

  ASSERT_EQ(Z_OK, inflateInit2(&strm, -15));

  constexpr unsigned int BUFFER_SIZE = 32768;
  std::vector<uint8_t> uncompressed_data(BUFFER_SIZE);
  size_t uncompressed_length = 0;
  SHA_CTX ctx;
  SHA1_Init(&ctx);
  int ret;
  do {
    strm.avail_out = BUFFER_SIZE;
    strm.next_out = uncompressed_data.data();

    ret = inflate(&strm, Z_NO_FLUSH);
    ASSERT_GE(ret, 0);

    SHA1_Update(&ctx, uncompressed_data.data(), BUFFER_SIZE - strm.avail_out);
    uncompressed_length += BUFFER_SIZE - strm.avail_out;
  } while (ret != Z_STREAM_END);
  inflateEnd(&strm);

  uint8_t digest[SHA_DIGEST_LENGTH];
  SHA1_Final(digest, &ctx);
  GTEST_LOG_(INFO) << "uncompressed length " << uncompressed_length
                   << " sha1: " << short_sha1(digest);
}

static void sha1sum(const std::string& fname, std::string* sha1, size_t* fsize = nullptr) {
  ASSERT_TRUE(sha1 != nullptr);

  std::string data;
  ASSERT_TRUE(android::base::ReadFileToString(fname, &data));

  if (fsize != nullptr) {
    *fsize = data.size();
  }

  uint8_t digest[SHA_DIGEST_LENGTH];
  SHA1(reinterpret_cast<const uint8_t*>(data.c_str()), data.size(), digest);
  *sha1 = print_sha1(digest);
}

static void test_logger(android::base::LogId /* id */, android::base::LogSeverity severity,
                        const char* /* tag */, const char* /* file */, unsigned int /* line */,
                        const char* message) {
  if (severity >= android::base::GetMinimumLogSeverity()) {
    fprintf(stdout, "%s\n", message);
  }
}

class ApplyPatchModesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    Paths::Get().set_cache_temp_source(cache_source.path);
    android::base::InitLogging(nullptr, &test_logger);
    android::base::SetMinimumLogSeverity(android::base::LogSeverity::DEBUG);
  }

  TemporaryFile cache_source;
};

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
  size_t recovery_img_size;
  std::string recovery_img_sha1;
  sha1sum(recovery_img, &recovery_img_sha1, &recovery_img_size);
  std::string recovery_img_size_arg = std::to_string(recovery_img_size);

  std::string bonus_file = from_testdata_base("bonus.file");

  // applypatch -b <bonus-file> <src-file> <tgt-file> <tgt-sha1> <tgt-size> <src-sha1>:<patch>
  std::string src_file_arg =
      "EMMC:" + boot_img + ":" + std::to_string(boot_img_size) + ":" + boot_img_sha1;
  TemporaryFile tgt_file;
  std::string tgt_file_arg = "EMMC:"s + tgt_file.path;
  std::string patch_arg = boot_img_sha1 + ":" + from_testdata_base("recovery-from-boot.p");
  std::vector<const char*> args = { "applypatch",
                                    "-b",
                                    bonus_file.c_str(),
                                    src_file_arg.c_str(),
                                    tgt_file_arg.c_str(),
                                    recovery_img_sha1.c_str(),
                                    recovery_img_size_arg.c_str(),
                                    patch_arg.c_str() };
  ASSERT_EQ(0, applypatch_modes(args.size(), args.data()));
}

// Tests patching the EMMC target without a separate bonus file (i.e. recovery-from-boot patch has
// everything).
TEST_F(ApplyPatchModesTest, PatchModeEmmcTargetWithoutBonusFile) {
  std::string boot_img = from_testdata_base("boot.img");
  size_t boot_img_size;
  std::string boot_img_sha1;
  sha1sum(boot_img, &boot_img_sha1, &boot_img_size);

  std::string recovery_img = from_testdata_base("recovery.img");
  size_t recovery_img_size;
  std::string recovery_img_sha1;
  sha1sum(recovery_img, &recovery_img_sha1, &recovery_img_size);
  std::string recovery_img_size_arg = std::to_string(recovery_img_size);

  // applypatch <src-file> <tgt-file> <tgt-sha1> <tgt-size> <src-sha1>:<patch>
  std::string src_file_arg =
      "EMMC:" + boot_img + ":" + std::to_string(boot_img_size) + ":" + boot_img_sha1;
  TemporaryFile tgt_file;
  std::string tgt_file_arg = "EMMC:"s + tgt_file.path;
  std::string patch_arg =
      boot_img_sha1 + ":" + from_testdata_base("recovery-from-boot-with-bonus.p");
  std::vector<const char*> args = { "applypatch",
                                    src_file_arg.c_str(),
                                    tgt_file_arg.c_str(),
                                    recovery_img_sha1.c_str(),
                                    recovery_img_size_arg.c_str(),
                                    patch_arg.c_str() };

  if (applypatch_modes(args.size(), args.data()) != 0) {
    DecompressAndDumpRecoveryImage(tgt_file.path);
    FAIL();
  }
}

TEST_F(ApplyPatchModesTest, PatchModeEmmcTargetWithMultiplePatches) {
  std::string boot_img = from_testdata_base("boot.img");
  size_t boot_img_size;
  std::string boot_img_sha1;
  sha1sum(boot_img, &boot_img_sha1, &boot_img_size);

  std::string recovery_img = from_testdata_base("recovery.img");
  size_t recovery_img_size;
  std::string recovery_img_sha1;
  sha1sum(recovery_img, &recovery_img_sha1, &recovery_img_size);
  std::string recovery_img_size_arg = std::to_string(recovery_img_size);

  std::string bonus_file = from_testdata_base("bonus.file");

  // applypatch -b <bonus-file> <src-file> <tgt-file> <tgt-sha1> <tgt-size> \
  //            <src-sha1-fake1>:<patch1> <src-sha1>:<patch2> <src-sha1-fake2>:<patch3>
  std::string src_file_arg =
      "EMMC:" + boot_img + ":" + std::to_string(boot_img_size) + ":" + boot_img_sha1;
  TemporaryFile tgt_file;
  std::string tgt_file_arg = "EMMC:"s + tgt_file.path;
  std::string bad_sha1_a = android::base::StringPrintf("%040x", rand());
  std::string bad_sha1_b = android::base::StringPrintf("%040x", rand());
  std::string patch1 = bad_sha1_a + ":" + from_testdata_base("recovery-from-boot.p");
  std::string patch2 = boot_img_sha1 + ":" + from_testdata_base("recovery-from-boot.p");
  std::string patch3 = bad_sha1_b + ":" + from_testdata_base("recovery-from-boot.p");
  std::vector<const char*> args = { "applypatch",
                                    "-b",
                                    bonus_file.c_str(),
                                    src_file_arg.c_str(),
                                    tgt_file_arg.c_str(),
                                    recovery_img_sha1.c_str(),
                                    recovery_img_size_arg.c_str(),
                                    patch1.c_str(),
                                    patch2.c_str(),
                                    patch3.c_str() };
  // TODO(b/67849209): Remove after addressing the flakiness.
  printf("Calling applypatch_modes with the following args:\n");
  for (const auto& arg : args) {
    printf("  %s\n", arg);
  }

  if (applypatch_modes(args.size(), args.data()) != 0) {
    DecompressAndDumpRecoveryImage(tgt_file.path);
    FAIL();
  }
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
    recovery_img_size.c_str(),
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
    bad_arg1.c_str(),
  };
  ASSERT_NE(0, applypatch_modes(args5.size(), args5.data()));

  // Target size cannot be zero.
  TemporaryFile tmp6;
  std::string patch = boot_img_sha1 + ":" + from_testdata_base("recovery-from-boot-with-bonus.p");
  std::vector<const char*> args6 = {
    "applypatch",  boot_img.c_str(), tmp6.path, recovery_img_sha1.c_str(),
    "0",  // target size
    patch.c_str(),
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
