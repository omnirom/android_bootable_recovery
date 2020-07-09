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

#include <android-base/file.h>
#include <gtest/gtest.h>

#include "otautil/dirutil.h"

TEST(DirUtilTest, create_invalid) {
  // Requesting to create an empty dir is invalid.
  ASSERT_EQ(-1, mkdir_recursively("", 0755, false, nullptr));
  ASSERT_EQ(ENOENT, errno);

  // Requesting to strip the name with no slash present.
  ASSERT_EQ(-1, mkdir_recursively("abc", 0755, true, nullptr));
  ASSERT_EQ(ENOENT, errno);

  // Creating a dir that already exists.
  TemporaryDir td;
  ASSERT_EQ(0, mkdir_recursively(td.path, 0755, false, nullptr));

  // "///" is a valid dir.
  ASSERT_EQ(0, mkdir_recursively("///", 0755, false, nullptr));

  // Request to create a dir, but a file with the same name already exists.
  TemporaryFile tf;
  ASSERT_EQ(-1, mkdir_recursively(tf.path, 0755, false, nullptr));
  ASSERT_EQ(ENOTDIR, errno);
}

TEST(DirUtilTest, create_smoke) {
  TemporaryDir td;
  std::string prefix(td.path);
  std::string path = prefix + "/a/b";
  constexpr mode_t mode = 0755;
  ASSERT_EQ(0, mkdir_recursively(path, mode, false, nullptr));

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
  ASSERT_EQ(0, mkdir_recursively(path, 0755, true, nullptr));

  // Verify that "../a" exists but not "../a/b".
  struct stat sb;
  ASSERT_EQ(0, stat((prefix + "/a").c_str(), &sb)) << strerror(errno);
  ASSERT_TRUE(S_ISDIR(sb.st_mode));

  ASSERT_EQ(-1, stat(path.c_str(), &sb));
  ASSERT_EQ(ENOENT, errno);

  // Clean up.
  ASSERT_EQ(0, rmdir((prefix + "/a").c_str()));
}

TEST(DirUtilTest, create_mode) {
  TemporaryDir td;
  std::string prefix(td.path);
  std::string path = prefix + "/a/b";
  constexpr mode_t mode = 0751;
  ASSERT_EQ(0, mkdir_recursively(path, mode, false, nullptr));

  // Verify the mode for "../a/b".
  struct stat sb;
  ASSERT_EQ(0, stat(path.c_str(), &sb)) << strerror(errno);
  ASSERT_TRUE(S_ISDIR(sb.st_mode));
  constexpr mode_t mask = S_IRWXU | S_IRWXG | S_IRWXO;
  ASSERT_EQ(mode, sb.st_mode & mask);

  // Verify the mode for "../a".
  ASSERT_EQ(0, stat((prefix + "/a").c_str(), &sb)) << strerror(errno);
  ASSERT_TRUE(S_ISDIR(sb.st_mode));
  ASSERT_EQ(mode, sb.st_mode & mask);

  // Clean up.
  ASSERT_EQ(0, rmdir((prefix + "/a/b").c_str()));
  ASSERT_EQ(0, rmdir((prefix + "/a").c_str()));
}
