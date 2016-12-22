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
#include <android-base/test_utils.h>
#include <gtest/gtest.h>
#include <otautil/ZipUtil.h>
#include <ziparchive/zip_archive.h>

#include "common/test_constants.h"

TEST(ZipUtilTest, invalid_args) {
  std::string zip_path = from_testdata_base("ziptest_valid.zip");
  ZipArchiveHandle handle;
  ASSERT_EQ(0, OpenArchive(zip_path.c_str(), &handle));

  // zip_path must be a relative path.
  ASSERT_FALSE(ExtractPackageRecursive(handle, "/a/b", "/tmp", nullptr, nullptr));

  // dest_path must be an absolute path.
  ASSERT_FALSE(ExtractPackageRecursive(handle, "a/b", "tmp", nullptr, nullptr));
  ASSERT_FALSE(ExtractPackageRecursive(handle, "a/b", "", nullptr, nullptr));

  CloseArchive(handle);
}

TEST(ZipUtilTest, extract_all) {
  std::string zip_path = from_testdata_base("ziptest_valid.zip");
  ZipArchiveHandle handle;
  ASSERT_EQ(0, OpenArchive(zip_path.c_str(), &handle));

  // Extract the whole package into a temp directory.
  TemporaryDir td;
  ExtractPackageRecursive(handle, "", td.path, nullptr, nullptr);

  // Make sure all the files are extracted correctly.
  std::string path(td.path);
  ASSERT_EQ(0, access((path + "/a.txt").c_str(), F_OK));
  ASSERT_EQ(0, access((path + "/b.txt").c_str(), F_OK));
  ASSERT_EQ(0, access((path + "/b/c.txt").c_str(), F_OK));
  ASSERT_EQ(0, access((path + "/b/d.txt").c_str(), F_OK));

  // The content of the file is the same as expected.
  std::string content1;
  ASSERT_TRUE(android::base::ReadFileToString(path + "/a.txt", &content1));
  ASSERT_EQ(kATxtContents, content1);

  std::string content2;
  ASSERT_TRUE(android::base::ReadFileToString(path + "/b/d.txt", &content2));
  ASSERT_EQ(kDTxtContents, content2);

  // Clean up the temp files under td.
  ASSERT_EQ(0, unlink((path + "/a.txt").c_str()));
  ASSERT_EQ(0, unlink((path + "/b.txt").c_str()));
  ASSERT_EQ(0, unlink((path + "/b/c.txt").c_str()));
  ASSERT_EQ(0, unlink((path + "/b/d.txt").c_str()));
  ASSERT_EQ(0, rmdir((path + "/b").c_str()));

  CloseArchive(handle);
}

TEST(ZipUtilTest, extract_prefix_with_slash) {
  std::string zip_path = from_testdata_base("ziptest_valid.zip");
  ZipArchiveHandle handle;
  ASSERT_EQ(0, OpenArchive(zip_path.c_str(), &handle));

  // Extract all the entries starting with "b/".
  TemporaryDir td;
  ExtractPackageRecursive(handle, "b/", td.path, nullptr, nullptr);

  // Make sure all the files with "b/" prefix are extracted correctly.
  std::string path(td.path);
  ASSERT_EQ(0, access((path + "/c.txt").c_str(), F_OK));
  ASSERT_EQ(0, access((path + "/d.txt").c_str(), F_OK));

  // And the rest are not extracted.
  ASSERT_EQ(-1, access((path + "/a.txt").c_str(), F_OK));
  ASSERT_EQ(ENOENT, errno);
  ASSERT_EQ(-1, access((path + "/b.txt").c_str(), F_OK));
  ASSERT_EQ(ENOENT, errno);

  // The content of the file is the same as expected.
  std::string content1;
  ASSERT_TRUE(android::base::ReadFileToString(path + "/c.txt", &content1));
  ASSERT_EQ(kCTxtContents, content1);

  std::string content2;
  ASSERT_TRUE(android::base::ReadFileToString(path + "/d.txt", &content2));
  ASSERT_EQ(kDTxtContents, content2);

  // Clean up the temp files under td.
  ASSERT_EQ(0, unlink((path + "/c.txt").c_str()));
  ASSERT_EQ(0, unlink((path + "/d.txt").c_str()));

  CloseArchive(handle);
}

TEST(ZipUtilTest, extract_prefix_without_slash) {
  std::string zip_path = from_testdata_base("ziptest_valid.zip");
  ZipArchiveHandle handle;
  ASSERT_EQ(0, OpenArchive(zip_path.c_str(), &handle));

  // Extract all the file entries starting with "b/".
  TemporaryDir td;
  ExtractPackageRecursive(handle, "b", td.path, nullptr, nullptr);

  // Make sure all the files with "b/" prefix are extracted correctly.
  std::string path(td.path);
  ASSERT_EQ(0, access((path + "/c.txt").c_str(), F_OK));
  ASSERT_EQ(0, access((path + "/d.txt").c_str(), F_OK));

  // And the rest are not extracted.
  ASSERT_EQ(-1, access((path + "/a.txt").c_str(), F_OK));
  ASSERT_EQ(ENOENT, errno);
  ASSERT_EQ(-1, access((path + "/b.txt").c_str(), F_OK));
  ASSERT_EQ(ENOENT, errno);

  // The content of the file is the same as expected.
  std::string content1;
  ASSERT_TRUE(android::base::ReadFileToString(path + "/c.txt", &content1));
  ASSERT_EQ(kCTxtContents, content1);

  std::string content2;
  ASSERT_TRUE(android::base::ReadFileToString(path + "/d.txt", &content2));
  ASSERT_EQ(kDTxtContents, content2);

  // Clean up the temp files under td.
  ASSERT_EQ(0, unlink((path + "/c.txt").c_str()));
  ASSERT_EQ(0, unlink((path + "/d.txt").c_str()));

  CloseArchive(handle);
}

TEST(ZipUtilTest, set_timestamp) {
  std::string zip_path = from_testdata_base("ziptest_valid.zip");
  ZipArchiveHandle handle;
  ASSERT_EQ(0, OpenArchive(zip_path.c_str(), &handle));

  // Set the timestamp to 8/1/2008.
  constexpr struct utimbuf timestamp = { 1217592000, 1217592000 };

  // Extract all the entries starting with "b/".
  TemporaryDir td;
  ExtractPackageRecursive(handle, "b", td.path, &timestamp, nullptr);

  // Make sure all the files with "b/" prefix are extracted correctly.
  std::string path(td.path);
  std::string file_c = path + "/c.txt";
  std::string file_d = path + "/d.txt";
  ASSERT_EQ(0, access(file_c.c_str(), F_OK));
  ASSERT_EQ(0, access(file_d.c_str(), F_OK));

  // Verify the timestamp.
  timespec time;
  time.tv_sec = 1217592000;
  time.tv_nsec = 0;

  struct stat sb;
  ASSERT_EQ(0, stat(file_c.c_str(), &sb)) << strerror(errno);
  ASSERT_EQ(time.tv_sec, static_cast<long>(sb.st_atime));
  ASSERT_EQ(time.tv_sec, static_cast<long>(sb.st_mtime));

  ASSERT_EQ(0, stat(file_d.c_str(), &sb)) << strerror(errno);
  ASSERT_EQ(time.tv_sec, static_cast<long>(sb.st_atime));
  ASSERT_EQ(time.tv_sec, static_cast<long>(sb.st_mtime));

  // Clean up the temp files under td.
  ASSERT_EQ(0, unlink(file_c.c_str()));
  ASSERT_EQ(0, unlink(file_d.c_str()));

  CloseArchive(handle);
}
