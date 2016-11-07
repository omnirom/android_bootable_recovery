/*
 * Copyright (C) 2016 The Android Open Source Project
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
#include <fcntl.h>
#include <unistd.h>

#include <memory>
#include <vector>

#include <android-base/file.h>
#include <android-base/test_utils.h>
#include <gtest/gtest.h>
#include <otautil/SysUtil.h>
#include <otautil/ZipUtil.h>
#include <ziparchive/zip_archive.h>

#include "common/test_constants.h"

static const std::string kATxtContents("abcdefghabcdefgh\n");
static const std::string kBTxtContents("abcdefgh\n");

TEST(ZipTest, ExtractPackageRecursive) {
  std::string zip_path = from_testdata_base("ziptest_valid.zip");
  ZipArchiveHandle handle;
  ASSERT_EQ(0, OpenArchive(zip_path.c_str(), &handle));

  // Extract the whole package into a temp directory.
  TemporaryDir td;
  ASSERT_NE(nullptr, td.path);
  ExtractPackageRecursive(handle, "", td.path, nullptr, nullptr);

  // Make sure all the files are extracted correctly.
  std::string path(td.path);
  ASSERT_EQ(0, access((path + "/a.txt").c_str(), O_RDONLY));
  ASSERT_EQ(0, access((path + "/b.txt").c_str(), O_RDONLY));
  ASSERT_EQ(0, access((path + "/b/c.txt").c_str(), O_RDONLY));
  ASSERT_EQ(0, access((path + "/b/d.txt").c_str(), O_RDONLY));

  // The content of the file is the same as expected.
  std::string content1;
  ASSERT_TRUE(android::base::ReadFileToString(path + "/a.txt", &content1));
  ASSERT_EQ(kATxtContents, content1);

  std::string content2;
  ASSERT_TRUE(android::base::ReadFileToString(path + "/b/d.txt", &content2));
  ASSERT_EQ(kBTxtContents, content2);
}

TEST(ZipTest, OpenFromMemory) {
  MemMapping map;
  std::string zip_path = from_testdata_base("ziptest_dummy-update.zip");
  ASSERT_EQ(0, sysMapFile(zip_path.c_str(), &map));

  // Map an update package into memory and open the archive from there.
  ZipArchiveHandle handle;
  ASSERT_EQ(0, OpenArchiveFromMemory(map.addr, map.length, zip_path.c_str(), &handle));

  static constexpr const char* BINARY_PATH = "META-INF/com/google/android/update-binary";
  ZipString binary_path(BINARY_PATH);
  ZipEntry binary_entry;
  // Make sure the package opens correctly and its entry can be read.
  ASSERT_EQ(0, FindEntry(handle, binary_path, &binary_entry));

  TemporaryFile tmp_binary;
  ASSERT_NE(-1, tmp_binary.fd);
  ASSERT_EQ(0, ExtractEntryToFile(handle, &binary_entry, tmp_binary.fd));

  sysReleaseMap(&map);
}

