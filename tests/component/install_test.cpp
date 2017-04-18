/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include <android-base/test_utils.h>
#include <gtest/gtest.h>
#include <ziparchive/zip_archive.h>
#include <ziparchive/zip_writer.h>

#include "install.h"

TEST(InstallTest, verify_package_compatibility_no_entry) {
  TemporaryFile temp_file;
  FILE* zip_file = fdopen(temp_file.fd, "w");
  ZipWriter writer(zip_file);
  // The archive must have something to be opened correctly.
  ASSERT_EQ(0, writer.StartEntry("dummy_entry", 0));
  ASSERT_EQ(0, writer.FinishEntry());
  ASSERT_EQ(0, writer.Finish());
  ASSERT_EQ(0, fclose(zip_file));

  // Doesn't contain compatibility zip entry.
  ZipArchiveHandle zip;
  ASSERT_EQ(0, OpenArchive(temp_file.path, &zip));
  ASSERT_TRUE(verify_package_compatibility(zip));
  CloseArchive(zip);
}

TEST(InstallTest, verify_package_compatibility_invalid_entry) {
  TemporaryFile temp_file;
  FILE* zip_file = fdopen(temp_file.fd, "w");
  ZipWriter writer(zip_file);
  ASSERT_EQ(0, writer.StartEntry("compatibility.zip", 0));
  ASSERT_EQ(0, writer.FinishEntry());
  ASSERT_EQ(0, writer.Finish());
  ASSERT_EQ(0, fclose(zip_file));

  // Empty compatibility zip entry.
  ZipArchiveHandle zip;
  ASSERT_EQ(0, OpenArchive(temp_file.path, &zip));
  ASSERT_FALSE(verify_package_compatibility(zip));
  CloseArchive(zip);
}
