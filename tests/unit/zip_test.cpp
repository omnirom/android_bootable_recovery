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
#include <pthread.h>
#include <unistd.h>

#include <memory>
#include <vector>

#include <android-base/file.h>
#include <android-base/stringprintf.h>
#include <android-base/unique_fd.h>
#include <android-base/test_utils.h>
#include <gtest/gtest.h>
#include <otautil/SysUtil.h>
#include <otautil/ZipUtil.h>
#include <ziparchive/zip_archive.h>

static const std::string DATA_PATH(getenv("ANDROID_DATA"));
static const std::string TESTDATA_PATH("/recovery/testdata/");

static const std::vector<uint8_t> kATxtContents {
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h',
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h',
    '\n'
};

static const std::vector<uint8_t> kBTxtContents {
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h',
    '\n'
};

TEST(otazip, ExtractPackageRecursive) {
    TemporaryDir td;
    ASSERT_NE(td.path, nullptr);
    ZipArchiveHandle handle;
    std::string zip_path = DATA_PATH + TESTDATA_PATH + "/ziptest_valid.zip";
    ASSERT_EQ(0, OpenArchive(zip_path.c_str(), &handle));
    // Extract the whole package into a temp directory.
    ExtractPackageRecursive(handle, "", td.path, nullptr, nullptr);
    // Make sure all the files are extracted correctly.
    std::string path(td.path);
    android::base::unique_fd fd(open((path + "/a.txt").c_str(), O_RDONLY));
    ASSERT_NE(fd, -1);
    std::vector<uint8_t> read_data;
    read_data.resize(kATxtContents.size());
    // The content of the file is the same as expected.
    ASSERT_TRUE(android::base::ReadFully(fd.get(), read_data.data(), read_data.size()));
    ASSERT_EQ(0, memcmp(read_data.data(), kATxtContents.data(), kATxtContents.size()));

    fd.reset(open((path + "/b.txt").c_str(), O_RDONLY));
    ASSERT_NE(fd, -1);
    fd.reset(open((path + "/b/c.txt").c_str(), O_RDONLY));
    ASSERT_NE(fd, -1);
    fd.reset(open((path + "/b/d.txt").c_str(), O_RDONLY));
    ASSERT_NE(fd, -1);
    read_data.resize(kBTxtContents.size());
    ASSERT_TRUE(android::base::ReadFully(fd.get(), read_data.data(), read_data.size()));
    ASSERT_EQ(0, memcmp(read_data.data(), kBTxtContents.data(), kBTxtContents.size()));
}

TEST(otazip, OpenFromMemory) {
    MemMapping map;
    std::string zip_path = DATA_PATH + TESTDATA_PATH + "/ziptest_dummy-update.zip";
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
}

