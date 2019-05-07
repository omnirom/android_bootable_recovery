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
#include <unistd.h>

#include <memory>
#include <vector>

#include <android-base/file.h>
#include <gtest/gtest.h>
#include <ziparchive/zip_archive.h>

#include "common/test_constants.h"
#include "otautil/sysutil.h"

TEST(ZipTest, OpenFromMemory) {
  std::string zip_path = from_testdata_base("ziptest_dummy-update.zip");
  MemMapping map;
  ASSERT_TRUE(map.MapFile(zip_path));

  // Map an update package into memory and open the archive from there.
  ZipArchiveHandle handle;
  ASSERT_EQ(0, OpenArchiveFromMemory(map.addr, map.length, zip_path.c_str(), &handle));

  static constexpr const char* BINARY_PATH = "META-INF/com/google/android/update-binary";
  ZipEntry binary_entry;
  // Make sure the package opens correctly and its entry can be read.
  ASSERT_EQ(0, FindEntry(handle, BINARY_PATH, &binary_entry));

  TemporaryFile tmp_binary;
  ASSERT_NE(-1, tmp_binary.fd);
  ASSERT_EQ(0, ExtractEntryToFile(handle, &binary_entry, tmp_binary.fd));

  CloseArchive(handle);
}

