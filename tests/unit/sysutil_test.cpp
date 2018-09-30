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

#include <gtest/gtest.h>

#include <string>

#include <android-base/file.h>
#include <android-base/test_utils.h>

#include "otautil/SysUtil.h"

TEST(SysUtilTest, InvalidArgs) {
  MemMapping mapping;

  // Invalid argument.
  ASSERT_FALSE(mapping.MapFile(""));
}

TEST(SysUtilTest, MapFileRegularFile) {
  TemporaryFile temp_file1;
  std::string content = "abc";
  ASSERT_TRUE(android::base::WriteStringToFile(content, temp_file1.path));

  // MemMapping::MapFile() should map the file to one range.
  MemMapping mapping;
  ASSERT_TRUE(mapping.MapFile(temp_file1.path));
  ASSERT_NE(nullptr, mapping.addr);
  ASSERT_EQ(content.size(), mapping.length);
  ASSERT_EQ(1U, mapping.ranges());
}

TEST(SysUtilTest, MapFileBlockMap) {
  // Create a file that has 10 blocks.
  TemporaryFile package;
  std::string content;
  constexpr size_t file_size = 4096 * 10;
  content.reserve(file_size);
  ASSERT_TRUE(android::base::WriteStringToFile(content, package.path));

  TemporaryFile block_map_file;
  std::string filename = std::string("@") + block_map_file.path;
  MemMapping mapping;

  // One range.
  std::string block_map_content = std::string(package.path) + "\n40960 4096\n1\n0 10\n";
  ASSERT_TRUE(android::base::WriteStringToFile(block_map_content, block_map_file.path));

  ASSERT_TRUE(mapping.MapFile(filename));
  ASSERT_EQ(file_size, mapping.length);
  ASSERT_EQ(1U, mapping.ranges());

  // It's okay to not have the trailing '\n'.
  block_map_content = std::string(package.path) + "\n40960 4096\n1\n0 10";
  ASSERT_TRUE(android::base::WriteStringToFile(block_map_content, block_map_file.path));

  ASSERT_TRUE(mapping.MapFile(filename));
  ASSERT_EQ(file_size, mapping.length);
  ASSERT_EQ(1U, mapping.ranges());

  // Or having multiple trailing '\n's.
  block_map_content = std::string(package.path) + "\n40960 4096\n1\n0 10\n\n\n";
  ASSERT_TRUE(android::base::WriteStringToFile(block_map_content, block_map_file.path));

  ASSERT_TRUE(mapping.MapFile(filename));
  ASSERT_EQ(file_size, mapping.length);
  ASSERT_EQ(1U, mapping.ranges());

  // Multiple ranges.
  block_map_content = std::string(package.path) + "\n40960 4096\n3\n0 3\n3 5\n5 10\n";
  ASSERT_TRUE(android::base::WriteStringToFile(block_map_content, block_map_file.path));

  ASSERT_TRUE(mapping.MapFile(filename));
  ASSERT_EQ(file_size, mapping.length);
  ASSERT_EQ(3U, mapping.ranges());
}

TEST(SysUtilTest, MapFileBlockMapInvalidBlockMap) {
  MemMapping mapping;
  TemporaryFile temp_file;
  std::string filename = std::string("@") + temp_file.path;

  // Block map file is too short.
  ASSERT_TRUE(android::base::WriteStringToFile("/somefile\n", temp_file.path));
  ASSERT_FALSE(mapping.MapFile(filename));

  ASSERT_TRUE(android::base::WriteStringToFile("/somefile\n4096 4096\n0\n", temp_file.path));
  ASSERT_FALSE(mapping.MapFile(filename));

  // Block map file has unexpected number of lines.
  ASSERT_TRUE(android::base::WriteStringToFile("/somefile\n4096 4096\n1\n", temp_file.path));
  ASSERT_FALSE(mapping.MapFile(filename));

  ASSERT_TRUE(android::base::WriteStringToFile("/somefile\n4096 4096\n2\n0 1\n", temp_file.path));
  ASSERT_FALSE(mapping.MapFile(filename));

  // Invalid size/blksize/range_count.
  ASSERT_TRUE(android::base::WriteStringToFile("/somefile\nabc 4096\n1\n0 1\n", temp_file.path));
  ASSERT_FALSE(mapping.MapFile(filename));

  ASSERT_TRUE(android::base::WriteStringToFile("/somefile\n4096 4096\n\n0 1\n", temp_file.path));
  ASSERT_FALSE(mapping.MapFile(filename));

  // size/blksize/range_count don't match.
  ASSERT_TRUE(android::base::WriteStringToFile("/somefile\n0 4096\n1\n0 1\n", temp_file.path));
  ASSERT_FALSE(mapping.MapFile(filename));

  ASSERT_TRUE(android::base::WriteStringToFile("/somefile\n4096 0\n1\n0 1\n", temp_file.path));
  ASSERT_FALSE(mapping.MapFile(filename));

  ASSERT_TRUE(android::base::WriteStringToFile("/somefile\n4096 4096\n0\n0 1\n", temp_file.path));
  ASSERT_FALSE(mapping.MapFile(filename));

  // Invalid block dev path.
  ASSERT_TRUE(android::base::WriteStringToFile("/doesntexist\n4096 4096\n1\n0 1\n", temp_file.path));
  ASSERT_FALSE(mapping.MapFile(filename));
}
