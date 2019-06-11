/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include <unistd.h>

#include <memory>
#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/strings.h>
#include <gtest/gtest.h>

#include "fuse_provider.h"
#include "fuse_sideload.h"

TEST(SideloadTest, fuse_device) {
  ASSERT_EQ(0, access("/dev/fuse", R_OK | W_OK));
}

class FuseTestDataProvider : public FuseDataProvider {
 public:
  FuseTestDataProvider(uint64_t file_size, uint32_t block_size)
      : FuseDataProvider(file_size, block_size) {}

 private:
  bool ReadBlockAlignedData(uint8_t*, uint32_t, uint32_t) const override {
    return true;
  }

  bool Valid() const override {
    return true;
  }
};

TEST(SideloadTest, run_fuse_sideload_wrong_parameters) {
  auto provider_small_block = std::make_unique<FuseTestDataProvider>(4096, 4095);
  ASSERT_EQ(-1, run_fuse_sideload(std::move(provider_small_block)));

  auto provider_large_block = std::make_unique<FuseTestDataProvider>(4096, (1 << 22) + 1);
  ASSERT_EQ(-1, run_fuse_sideload(std::move(provider_large_block)));

  auto provider_too_many_blocks =
      std::make_unique<FuseTestDataProvider>(((1 << 18) + 1) * 4096, 4096);
  ASSERT_EQ(-1, run_fuse_sideload(std::move(provider_too_many_blocks)));
}

TEST(SideloadTest, run_fuse_sideload) {
  const std::vector<std::string> blocks = {
    std::string(2048, 'a') + std::string(2048, 'b'),
    std::string(2048, 'c') + std::string(2048, 'd'),
    std::string(2048, 'e') + std::string(2048, 'f'),
    std::string(2048, 'g') + std::string(2048, 'h'),
  };
  const std::string content = android::base::Join(blocks, "");
  ASSERT_EQ(16384U, content.size());

  TemporaryFile temp_file;
  ASSERT_TRUE(android::base::WriteStringToFile(content, temp_file.path));

  auto provider = std::make_unique<FuseFileDataProvider>(temp_file.path, 4096);
  ASSERT_TRUE(provider->Valid());
  TemporaryDir mount_point;
  pid_t pid = fork();
  if (pid == 0) {
    ASSERT_EQ(0, run_fuse_sideload(std::move(provider), mount_point.path));
    _exit(EXIT_SUCCESS);
  }

  std::string package = std::string(mount_point.path) + "/" + FUSE_SIDELOAD_HOST_FILENAME;
  int status;
  static constexpr int kSideloadInstallTimeout = 10;
  for (int i = 0; i < kSideloadInstallTimeout; ++i) {
    ASSERT_NE(-1, waitpid(pid, &status, WNOHANG));

    struct stat sb;
    if (stat(package.c_str(), &sb) == 0) {
      break;
    }

    if (errno == ENOENT && i < kSideloadInstallTimeout - 1) {
      sleep(1);
      continue;
    }
    FAIL() << "Timed out waiting for the fuse-provided package.";
  }

  std::string content_via_fuse;
  ASSERT_TRUE(android::base::ReadFileToString(package, &content_via_fuse));
  ASSERT_EQ(content, content_via_fuse);

  std::string exit_flag = std::string(mount_point.path) + "/" + FUSE_SIDELOAD_HOST_EXIT_FLAG;
  struct stat sb;
  ASSERT_EQ(0, stat(exit_flag.c_str(), &sb));

  waitpid(pid, &status, 0);
  ASSERT_EQ(0, WEXITSTATUS(status));
  ASSERT_EQ(EXIT_SUCCESS, WEXITSTATUS(status));
}
