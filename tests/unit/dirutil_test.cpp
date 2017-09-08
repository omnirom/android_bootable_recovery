/*
 * Copyright 2016 The Android Open Source Project
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

#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>

#include <android-base/test_utils.h>
#include <gtest/gtest.h>
#include <otautil/DirUtil.h>

TEST(DirUtilTest, create_invalid) {
  // Requesting to create an empty dir is invalid.
  ASSERT_EQ(-1, dirCreateHierarchy("", 0755, nullptr, false, nullptr));
  ASSERT_EQ(ENOENT, errno);

  // Requesting to strip the name with no slash present.
  ASSERT_EQ(-1, dirCreateHierarchy("abc", 0755, nullptr, true, nullptr));
  ASSERT_EQ(ENOENT, errno);

  // Creating a dir that already exists.
  TemporaryDir td;
  ASSERT_EQ(0, dirCreateHierarchy(td.path, 0755, nullptr, false, nullptr));

  // "///" is a valid dir.
  ASSERT_EQ(0, dirCreateHierarchy("///", 0755, nullptr, false, nullptr));

  // Request to create a dir, but a file with the same name already exists.
  TemporaryFile tf;
  ASSERT_EQ(-1, dirCreateHierarchy(tf.path, 0755, nullptr, false, nullptr));
  ASSERT_EQ(ENOTDIR, errno);
}

TEST(DirUtilTest, create_smoke) {
  TemporaryDir td;
  std::string prefix(td.path);
  std::string path = prefix + "/a/b";
  constexpr mode_t mode = 0755;
  ASSERT_EQ(0, dirCreateHierarchy(path.c_str(), mode, nullptr, false, nullptr));

  // Verify.
  struct stat sb;
  ASSERT_EQ(0, stat(path.c_str(), &sb)) << strerror(errno);
  ASSERT_TRUE(S_ISDIR(sb.st_mode));
  constexpr mode_t mask = S_IRWXU | S_IRWXG | S_IRWXO;
  ASSERT_EQ(mode, sb.st_mode & mask);

  // Clean up.
  ASSERT_EQ(0, rmdir((prefix + "/a/b").c_str()));
  ASSERT_EQ(0, rmdir((prefix + "/a").c_str()));
}

TEST(DirUtilTest, create_strip_filename) {
  TemporaryDir td;
  std::string prefix(td.path);
  std::string path = prefix + "/a/b";
  ASSERT_EQ(0, dirCreateHierarchy(path.c_str(), 0755, nullptr, true, nullptr));

  // Verify that "../a" exists but not "../a/b".
  struct stat sb;
  ASSERT_EQ(0, stat((prefix + "/a").c_str(), &sb)) << strerror(errno);
  ASSERT_TRUE(S_ISDIR(sb.st_mode));

  ASSERT_EQ(-1, stat(path.c_str(), &sb));
  ASSERT_EQ(ENOENT, errno);

  // Clean up.
  ASSERT_EQ(0, rmdir((prefix + "/a").c_str()));
}

TEST(DirUtilTest, create_mode_and_timestamp) {
  TemporaryDir td;
  std::string prefix(td.path);
  std::string path = prefix + "/a/b";
  // Set the timestamp to 8/1/2008.
  constexpr struct utimbuf timestamp = { 1217592000, 1217592000 };
  constexpr mode_t mode = 0751;
  ASSERT_EQ(0, dirCreateHierarchy(path.c_str(), mode, &timestamp, false, nullptr));

  // Verify the mode and timestamp for "../a/b".
  struct stat sb;
  ASSERT_EQ(0, stat(path.c_str(), &sb)) << strerror(errno);
  ASSERT_TRUE(S_ISDIR(sb.st_mode));
  constexpr mode_t mask = S_IRWXU | S_IRWXG | S_IRWXO;
  ASSERT_EQ(mode, sb.st_mode & mask);

  timespec time;
  time.tv_sec = 1217592000;
  time.tv_nsec = 0;

  ASSERT_EQ(time.tv_sec, static_cast<long>(sb.st_atime));
  ASSERT_EQ(time.tv_sec, static_cast<long>(sb.st_mtime));

  // Verify the mode for "../a". Note that the timestamp for intermediate directories (e.g. "../a")
  // may not be 'timestamp' according to the current implementation.
  ASSERT_EQ(0, stat((prefix + "/a").c_str(), &sb)) << strerror(errno);
  ASSERT_TRUE(S_ISDIR(sb.st_mode));
  ASSERT_EQ(mode, sb.st_mode & mask);

  // Clean up.
  ASSERT_EQ(0, rmdir((prefix + "/a/b").c_str()));
  ASSERT_EQ(0, rmdir((prefix + "/a").c_str()));
}

TEST(DirUtilTest, unlink_invalid) {
  // File doesn't exist.
  ASSERT_EQ(-1, dirUnlinkHierarchy("doesntexist"));

  // Nonexistent directory.
  TemporaryDir td;
  std::string path(td.path);
  ASSERT_EQ(-1, dirUnlinkHierarchy((path + "/a").c_str()));
  ASSERT_EQ(ENOENT, errno);
}

TEST(DirUtilTest, unlink_smoke) {
  // Unlink a file.
  TemporaryFile tf;
  ASSERT_EQ(0, dirUnlinkHierarchy(tf.path));
  ASSERT_EQ(-1, access(tf.path, F_OK));

  TemporaryDir td;
  std::string path(td.path);
  constexpr mode_t mode = 0700;
  ASSERT_EQ(0, mkdir((path + "/a").c_str(), mode));
  ASSERT_EQ(0, mkdir((path + "/a/b").c_str(), mode));
  ASSERT_EQ(0, mkdir((path + "/a/b/c").c_str(), mode));
  ASSERT_EQ(0, mkdir((path + "/a/d").c_str(), mode));

  // Remove "../a" recursively.
  ASSERT_EQ(0, dirUnlinkHierarchy((path + "/a").c_str()));

  // Verify it's gone.
  ASSERT_EQ(-1, access((path + "/a").c_str(), F_OK));
}
