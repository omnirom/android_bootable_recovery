/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include <stdint.h>
#include <unistd.h>

#include <functional>
#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>
#include <android-base/unique_fd.h>
#include <gtest/gtest.h>

#include "fuse_provider.h"
#include "fuse_sideload.h"
#include "install/install.h"

TEST(FuseBlockMapTest, CreateFromBlockMap_smoke) {
  TemporaryFile fake_block_device;
  std::vector<std::string> lines = {
    fake_block_device.path, "10000 4096", "3", "10 11", "20 21", "22 23",
  };

  TemporaryFile temp_file;
  android::base::WriteStringToFile(android::base::Join(lines, '\n'), temp_file.path);
  auto block_map_data = FuseBlockDataProvider::CreateFromBlockMap(temp_file.path, 4096);

  ASSERT_TRUE(block_map_data);
  ASSERT_EQ(10000, block_map_data->file_size());
  ASSERT_EQ(4096, block_map_data->fuse_block_size());
  ASSERT_EQ(RangeSet({ { 10, 11 }, { 20, 21 }, { 22, 23 } }),
            static_cast<FuseBlockDataProvider*>(block_map_data.get())->ranges());
}

TEST(FuseBlockMapTest, ReadBlockAlignedData_smoke) {
  std::string content;
  content.reserve(40960);
  for (char c = 0; c < 10; c++) {
    content += std::string(4096, c);
  }
  TemporaryFile fake_block_device;
  ASSERT_TRUE(android::base::WriteStringToFile(content, fake_block_device.path));

  std::vector<std::string> lines = {
    fake_block_device.path,
    "20000 4096",
    "1",
    "0 5",
  };
  TemporaryFile temp_file;
  android::base::WriteStringToFile(android::base::Join(lines, '\n'), temp_file.path);
  auto block_map_data = FuseBlockDataProvider::CreateFromBlockMap(temp_file.path, 4096);

  std::vector<uint8_t> result(2000);
  ASSERT_TRUE(block_map_data->ReadBlockAlignedData(result.data(), 2000, 1));
  ASSERT_EQ(std::vector<uint8_t>(content.begin() + 4096, content.begin() + 6096), result);

  result.resize(20000);
  ASSERT_TRUE(block_map_data->ReadBlockAlignedData(result.data(), 20000, 0));
  ASSERT_EQ(std::vector<uint8_t>(content.begin(), content.begin() + 20000), result);
}

TEST(FuseBlockMapTest, ReadBlockAlignedData_large_fuse_block) {
  std::string content;
  for (char c = 0; c < 10; c++) {
    content += std::string(4096, c);
  }

  TemporaryFile temp_file;
  ASSERT_TRUE(android::base::WriteStringToFile(content, temp_file.path));

  std::vector<std::string> lines = {
    temp_file.path, "36384 4096", "2", "0 5", "6 10",
  };
  TemporaryFile block_map;
  ASSERT_TRUE(android::base::WriteStringToFile(android::base::Join(lines, '\n'), block_map.path));

  auto block_map_data = FuseBlockDataProvider::CreateFromBlockMap(block_map.path, 16384);
  ASSERT_TRUE(block_map_data);

  std::vector<uint8_t> result(20000);
  // Out of bound read
  ASSERT_FALSE(block_map_data->ReadBlockAlignedData(result.data(), 20000, 2));
  ASSERT_TRUE(block_map_data->ReadBlockAlignedData(result.data(), 20000, 1));
  // expected source block contains: 4, 6-9
  std::string expected = content.substr(16384, 4096) + content.substr(24576, 15904);
  ASSERT_EQ(std::vector<uint8_t>(expected.begin(), expected.end()), result);
}
