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
#include <inttypes.h>
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
#include <android-base/logging.h>
#include <android-base/stringprintf.h>
#include <android-base/unique_fd.h>
#include <gtest/gtest.h>

#include "applypatch/applypatch.h"
#include "common/test_constants.h"
#include "edify/expr.h"
#include "otautil/paths.h"
#include "otautil/print_sha1.h"

using namespace std::string_literals;

class ApplyPatchTest : public ::testing::Test {
 protected:
  void SetUp() override {
    source_file = from_testdata_base("boot.img");
    FileContents boot_fc;
    ASSERT_TRUE(LoadFileContents(source_file, &boot_fc));
    source_size = boot_fc.data.size();
    source_sha1 = print_sha1(boot_fc.sha1);

    target_file = from_testdata_base("recovery.img");
    FileContents recovery_fc;
    ASSERT_TRUE(LoadFileContents(target_file, &recovery_fc));
    target_size = recovery_fc.data.size();
    target_sha1 = print_sha1(recovery_fc.sha1);

    source_partition = Partition(source_file, source_size, source_sha1);
    target_partition = Partition(partition_file.path, target_size, target_sha1);

    srand(time(nullptr));
    bad_sha1_a = android::base::StringPrintf("%040x", rand());
    bad_sha1_b = android::base::StringPrintf("%040x", rand());

    // Reset the cache backup file.
    Paths::Get().set_cache_temp_source(cache_temp_source.path);
  }

  void TearDown() override {
    ASSERT_TRUE(android::base::RemoveFileIfExists(cache_temp_source.path));
  }

  std::string source_file;
  std::string source_sha1;
  size_t source_size;

  std::string target_file;
  std::string target_sha1;
  size_t target_size;

  std::string bad_sha1_a;
  std::string bad_sha1_b;

  Partition source_partition;
  Partition target_partition;

 private:
  TemporaryFile partition_file;
  TemporaryFile cache_temp_source;
};

TEST_F(ApplyPatchTest, CheckPartition) {
  ASSERT_TRUE(CheckPartition(source_partition));
}

TEST_F(ApplyPatchTest, CheckPartition_Mismatching) {
  ASSERT_FALSE(CheckPartition(Partition(source_file, target_size, target_sha1)));
  ASSERT_FALSE(CheckPartition(Partition(source_file, source_size, bad_sha1_a)));

  ASSERT_FALSE(CheckPartition(Partition(source_file, source_size - 1, source_sha1)));
  ASSERT_FALSE(CheckPartition(Partition(source_file, source_size + 1, source_sha1)));
}

TEST_F(ApplyPatchTest, PatchPartitionCheck) {
  ASSERT_TRUE(PatchPartitionCheck(target_partition, source_partition));

  ASSERT_TRUE(
      PatchPartitionCheck(Partition(source_file, source_size - 1, source_sha1), source_partition));

  ASSERT_TRUE(
      PatchPartitionCheck(Partition(source_file, source_size + 1, source_sha1), source_partition));
}

TEST_F(ApplyPatchTest, PatchPartitionCheck_UseBackup) {
  ASSERT_FALSE(
      PatchPartitionCheck(target_partition, Partition(target_file, source_size, source_sha1)));

  Paths::Get().set_cache_temp_source(source_file);
  ASSERT_TRUE(
      PatchPartitionCheck(target_partition, Partition(target_file, source_size, source_sha1)));
}

TEST_F(ApplyPatchTest, PatchPartitionCheck_UseBackup_BothCorrupted) {
  ASSERT_FALSE(
      PatchPartitionCheck(target_partition, Partition(target_file, source_size, source_sha1)));

  Paths::Get().set_cache_temp_source(target_file);
  ASSERT_FALSE(
      PatchPartitionCheck(target_partition, Partition(target_file, source_size, source_sha1)));
}

TEST_F(ApplyPatchTest, PatchPartition) {
  FileContents patch_fc;
  ASSERT_TRUE(LoadFileContents(from_testdata_base("recovery-from-boot.p"), &patch_fc));
  Value patch(Value::Type::BLOB, std::string(patch_fc.data.cbegin(), patch_fc.data.cend()));

  FileContents bonus_fc;
  ASSERT_TRUE(LoadFileContents(from_testdata_base("bonus.file"), &bonus_fc));
  Value bonus(Value::Type::BLOB, std::string(bonus_fc.data.cbegin(), bonus_fc.data.cend()));

  ASSERT_TRUE(PatchPartition(target_partition, source_partition, patch, &bonus, false));
}

// Tests patching an eMMC target without a separate bonus file (i.e. recovery-from-boot patch has
// everything).
TEST_F(ApplyPatchTest, PatchPartitionWithoutBonusFile) {
  FileContents patch_fc;
  ASSERT_TRUE(LoadFileContents(from_testdata_base("recovery-from-boot-with-bonus.p"), &patch_fc));
  Value patch(Value::Type::BLOB, std::string(patch_fc.data.cbegin(), patch_fc.data.cend()));

  ASSERT_TRUE(PatchPartition(target_partition, source_partition, patch, nullptr, false));
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

  void AddFilesToDir(const std::string& dir, const std::vector<std::string>& files) {
    std::string zeros(4096, 0);
    for (const auto& file : files) {
      temporary_files_.push_back(dir + "/" + file);
      ASSERT_TRUE(android::base::WriteStringToFile(zeros, temporary_files_.back()));
    }
  }

  void SetUp() override {
    Paths::Get().set_cache_log_directory(mock_log_dir.path);
    temporary_files_.clear();
  }

  void TearDown() override {
    for (const auto& file : temporary_files_) {
      ASSERT_TRUE(android::base::RemoveFileIfExists(file));
    }
  }

  // A mock method to calculate the free space. It assumes the partition has a total size of 40960
  // bytes and all files are 4096 bytes in size.
  static size_t MockFreeSpaceChecker(const std::string& dirname) {
    std::vector<std::string> files = FindFilesInDir(dirname);
    return PARTITION_SIZE - 4096 * files.size();
  }

  TemporaryDir mock_cache;
  TemporaryDir mock_log_dir;

 private:
  std::vector<std::string> temporary_files_;
};

TEST_F(FreeCacheTest, FreeCacheSmoke) {
  std::vector<std::string> files = { "file1", "file2", "file3" };
  AddFilesToDir(mock_cache.path, files);
  ASSERT_EQ(files, FindFilesInDir(mock_cache.path));
  ASSERT_EQ(4096 * 7, MockFreeSpaceChecker(mock_cache.path));

  ASSERT_TRUE(RemoveFilesInDirectory(4096 * 9, mock_cache.path, MockFreeSpaceChecker));

  ASSERT_EQ(std::vector<std::string>{ "file3" }, FindFilesInDir(mock_cache.path));
  ASSERT_EQ(4096 * 9, MockFreeSpaceChecker(mock_cache.path));
}

TEST_F(FreeCacheTest, FreeCacheFreeSpaceCheckerError) {
  std::vector<std::string> files{ "file1", "file2", "file3" };
  AddFilesToDir(mock_cache.path, files);
  ASSERT_EQ(files, FindFilesInDir(mock_cache.path));
  ASSERT_EQ(4096 * 7, MockFreeSpaceChecker(mock_cache.path));

  ASSERT_FALSE(
      RemoveFilesInDirectory(4096 * 9, mock_cache.path, [](const std::string&) { return -1; }));
}

TEST_F(FreeCacheTest, FreeCacheOpenFile) {
  std::vector<std::string> files = { "file1", "file2" };
  AddFilesToDir(mock_cache.path, files);
  ASSERT_EQ(files, FindFilesInDir(mock_cache.path));
  ASSERT_EQ(4096 * 8, MockFreeSpaceChecker(mock_cache.path));

  std::string file1_path = mock_cache.path + "/file1"s;
  android::base::unique_fd fd(open(file1_path.c_str(), O_RDONLY));

  // file1 can't be deleted as it's opened by us.
  ASSERT_FALSE(RemoveFilesInDirectory(4096 * 10, mock_cache.path, MockFreeSpaceChecker));

  ASSERT_EQ(std::vector<std::string>{ "file1" }, FindFilesInDir(mock_cache.path));
}

TEST_F(FreeCacheTest, FreeCacheLogsSmoke) {
  std::vector<std::string> log_files = { "last_log", "last_log.1", "last_kmsg.2", "last_log.5",
                                         "last_log.10" };
  AddFilesToDir(mock_log_dir.path, log_files);
  ASSERT_EQ(4096 * 5, MockFreeSpaceChecker(mock_log_dir.path));

  ASSERT_TRUE(RemoveFilesInDirectory(4096 * 8, mock_log_dir.path, MockFreeSpaceChecker));

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

  ASSERT_TRUE(RemoveFilesInDirectory(4096 * 9, mock_log_dir.path, MockFreeSpaceChecker));

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

  ASSERT_FALSE(RemoveFilesInDirectory(4096 * 8, mock_log_dir.path, MockFreeSpaceChecker));

  // Non log files in /cache/recovery won't be deleted.
  std::vector<std::string> expected = { "block.map", "command", "last_install" };
  ASSERT_EQ(expected, FindFilesInDir(mock_log_dir.path));
}
