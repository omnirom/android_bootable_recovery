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

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/stringprintf.h>
#include <android-base/test_utils.h>
#include <android-base/unique_fd.h>
#include <gtest/gtest.h>
#include <openssl/sha.h>

#include "applypatch/applypatch.h"
#include "common/test_constants.h"
#include "otautil/paths.h"
#include "otautil/print_sha1.h"

using namespace std::string_literals;

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

static void mangle_file(const std::string& fname) {
  std::string content(1024, '\0');
  for (size_t i = 0; i < 1024; i++) {
    content[i] = rand() % 256;
  }
  ASSERT_TRUE(android::base::WriteStringToFile(content, fname));
}

class ApplyPatchTest : public ::testing::Test {
 public:
  void SetUp() override {
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
  std::string src_file = "EMMC:" + old_file + ":" + std::to_string(old_size) + ":" + old_sha1;
  std::vector<std::string> sha1s;
  ASSERT_EQ(0, applypatch_check(src_file.c_str(), sha1s));

  // EMMC:old_file:(size-1):sha1:(size+1):sha1 should fail the check.
  src_file = "EMMC:" + old_file + ":" + std::to_string(old_size - 1) + ":" + old_sha1 + ":" +
             std::to_string(old_size + 1) + ":" + old_sha1;
  ASSERT_EQ(1, applypatch_check(src_file.c_str(), sha1s));

  // EMMC:old_file:(size-1):sha1:size:sha1:(size+1):sha1 should pass the check.
  src_file = "EMMC:" + old_file + ":" + std::to_string(old_size - 1) + ":" + old_sha1 + ":" +
             std::to_string(old_size) + ":" + old_sha1 + ":" + std::to_string(old_size + 1) + ":" +
             old_sha1;
  ASSERT_EQ(0, applypatch_check(src_file.c_str(), sha1s));

  // EMMC:old_file:(size+1):sha1:(size-1):sha1:size:sha1 should pass the check.
  src_file = "EMMC:" + old_file + ":" + std::to_string(old_size + 1) + ":" + old_sha1 + ":" +
             std::to_string(old_size - 1) + ":" + old_sha1 + ":" + std::to_string(old_size) + ":" +
             old_sha1;
  ASSERT_EQ(0, applypatch_check(src_file.c_str(), sha1s));

  // EMMC:new_file:(size+1):old_sha1:(size-1):old_sha1:size:old_sha1:size:new_sha1
  // should pass the check.
  src_file = "EMMC:" + new_file + ":" + std::to_string(old_size + 1) + ":" + old_sha1 + ":" +
             std::to_string(old_size - 1) + ":" + old_sha1 + ":" + std::to_string(old_size) + ":" +
             old_sha1 + ":" + std::to_string(new_size) + ":" + new_sha1;
  ASSERT_EQ(0, applypatch_check(src_file.c_str(), sha1s));
}

class ApplyPatchCacheTest : public ApplyPatchTest {
 protected:
  void SetUp() override {
    ApplyPatchTest::SetUp();
    Paths::Get().set_cache_temp_source(old_file);
  }
};

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

class FreeCacheTest : public ::testing::Test {
 protected:
  static constexpr size_t PARTITION_SIZE = 4096 * 10;

  // Returns a sorted list of files in |dirname|.
  static std::vector<std::string> FindFilesInDir(const std::string& dirname) {
    std::vector<std::string> file_list;

    std::unique_ptr<DIR, decltype(&closedir)> d(opendir(dirname.c_str()), closedir);
    struct dirent* de;
    while ((de = readdir(d.get())) != 0) {
      std::string path = dirname + "/" + de->d_name;

      struct stat st;
      if (stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
        file_list.emplace_back(de->d_name);
      }
    }

    std::sort(file_list.begin(), file_list.end());
    return file_list;
  }

  static void AddFilesToDir(const std::string& dir, const std::vector<std::string>& files) {
    std::string zeros(4096, 0);
    for (const auto& file : files) {
      std::string path = dir + "/" + file;
      ASSERT_TRUE(android::base::WriteStringToFile(zeros, path));
    }
  }

  void SetUp() override {
    Paths::Get().set_cache_log_directory(mock_log_dir.path);
  }

  // A mock method to calculate the free space. It assumes the partition has a total size of 40960
  // bytes and all files are 4096 bytes in size.
  size_t MockFreeSpaceChecker(const std::string& dirname) {
    std::vector<std::string> files = FindFilesInDir(dirname);
    return PARTITION_SIZE - 4096 * files.size();
  }

  TemporaryDir mock_cache;
  TemporaryDir mock_log_dir;
};

TEST_F(FreeCacheTest, FreeCacheSmoke) {
  std::vector<std::string> files = { "file1", "file2", "file3" };
  AddFilesToDir(mock_cache.path, files);
  ASSERT_EQ(files, FindFilesInDir(mock_cache.path));
  ASSERT_EQ(4096 * 7, MockFreeSpaceChecker(mock_cache.path));

  ASSERT_TRUE(RemoveFilesInDirectory(4096 * 9, mock_cache.path, [&](const std::string& dir) {
    return this->MockFreeSpaceChecker(dir);
  }));

  ASSERT_EQ(std::vector<std::string>{ "file3" }, FindFilesInDir(mock_cache.path));
  ASSERT_EQ(4096 * 9, MockFreeSpaceChecker(mock_cache.path));
}

TEST_F(FreeCacheTest, FreeCacheOpenFile) {
  std::vector<std::string> files = { "file1", "file2" };
  AddFilesToDir(mock_cache.path, files);
  ASSERT_EQ(files, FindFilesInDir(mock_cache.path));
  ASSERT_EQ(4096 * 8, MockFreeSpaceChecker(mock_cache.path));

  std::string file1_path = mock_cache.path + "/file1"s;
  android::base::unique_fd fd(open(file1_path.c_str(), O_RDONLY));

  // file1 can't be deleted as it's opened by us.
  ASSERT_FALSE(RemoveFilesInDirectory(4096 * 10, mock_cache.path, [&](const std::string& dir) {
    return this->MockFreeSpaceChecker(dir);
  }));

  ASSERT_EQ(std::vector<std::string>{ "file1" }, FindFilesInDir(mock_cache.path));
}

TEST_F(FreeCacheTest, FreeCacheLogsSmoke) {
  std::vector<std::string> log_files = { "last_log", "last_log.1", "last_kmsg.2", "last_log.5",
                                         "last_log.10" };
  AddFilesToDir(mock_log_dir.path, log_files);
  ASSERT_EQ(4096 * 5, MockFreeSpaceChecker(mock_log_dir.path));

  ASSERT_TRUE(RemoveFilesInDirectory(4096 * 8, mock_log_dir.path, [&](const std::string& dir) {
    return this->MockFreeSpaceChecker(dir);
  }));

  // Logs with a higher index will be deleted first
  std::vector<std::string> expected = { "last_log", "last_log.1" };
  ASSERT_EQ(expected, FindFilesInDir(mock_log_dir.path));
  ASSERT_EQ(4096 * 8, MockFreeSpaceChecker(mock_log_dir.path));
}

TEST_F(FreeCacheTest, FreeCacheLogsStringComparison) {
  std::vector<std::string> log_files = { "last_log.1", "last_kmsg.1", "last_log.not_number",
                                         "last_kmsgrandom" };
  AddFilesToDir(mock_log_dir.path, log_files);
  ASSERT_EQ(4096 * 6, MockFreeSpaceChecker(mock_log_dir.path));

  ASSERT_TRUE(RemoveFilesInDirectory(4096 * 9, mock_log_dir.path, [&](const std::string& dir) {
    return this->MockFreeSpaceChecker(dir);
  }));

  // Logs with incorrect format will be deleted first; and the last_kmsg with the same index is
  // deleted before last_log.
  std::vector<std::string> expected = { "last_log.1" };
  ASSERT_EQ(expected, FindFilesInDir(mock_log_dir.path));
  ASSERT_EQ(4096 * 9, MockFreeSpaceChecker(mock_log_dir.path));
}

TEST_F(FreeCacheTest, FreeCacheLogsOtherFiles) {
  std::vector<std::string> log_files = { "last_install", "command", "block.map", "last_log",
                                         "last_kmsg.1" };
  AddFilesToDir(mock_log_dir.path, log_files);
  ASSERT_EQ(4096 * 5, MockFreeSpaceChecker(mock_log_dir.path));

  ASSERT_FALSE(RemoveFilesInDirectory(4096 * 8, mock_log_dir.path, [&](const std::string& dir) {
    return this->MockFreeSpaceChecker(dir);
  }));

  // Non log files in /cache/recovery won't be deleted.
  std::vector<std::string> expected = { "block.map", "command", "last_install" };
  ASSERT_EQ(expected, FindFilesInDir(mock_log_dir.path));
}
