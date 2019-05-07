/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include <functional>
#include <string>
#include <vector>

#include <android-base/file.h>
#include <gtest/gtest.h>
#include <openssl/sha.h>
#include <ziparchive/zip_writer.h>

#include "common/test_constants.h"
#include "install/package.h"

class PackageTest : public ::testing::Test {
 protected:
  void SetUp() override;

  // A list of package classes for test, including MemoryPackage and FilePackage.
  std::vector<std::unique_ptr<Package>> packages_;

  TemporaryFile temp_file_;   // test package file.
  std::string file_content_;  // actual bytes of the package file.
};

void PackageTest::SetUp() {
  std::vector<std::string> entries = { "file1.txt", "file2.txt", "dir1/file3.txt" };
  FILE* file_ptr = fdopen(temp_file_.release(), "wb");
  ZipWriter writer(file_ptr);
  for (const auto& entry : entries) {
    ASSERT_EQ(0, writer.StartEntry(entry.c_str(), ZipWriter::kCompress));
    ASSERT_EQ(0, writer.WriteBytes(entry.c_str(), entry.size()));
    ASSERT_EQ(0, writer.FinishEntry());
  }
  writer.Finish();
  ASSERT_EQ(0, fclose(file_ptr));

  ASSERT_TRUE(android::base::ReadFileToString(temp_file_.path, &file_content_));
  auto memory_package = Package::CreateMemoryPackage(temp_file_.path, nullptr);
  ASSERT_TRUE(memory_package);
  packages_.emplace_back(std::move(memory_package));

  auto file_package = Package::CreateFilePackage(temp_file_.path, nullptr);
  ASSERT_TRUE(file_package);
  packages_.emplace_back(std::move(file_package));
}

TEST_F(PackageTest, ReadFullyAtOffset_success) {
  for (const auto& package : packages_) {
    std::vector<uint8_t> buffer(file_content_.size());
    ASSERT_TRUE(package->ReadFullyAtOffset(buffer.data(), file_content_.size(), 0));
    ASSERT_EQ(file_content_, std::string(buffer.begin(), buffer.end()));

    ASSERT_TRUE(package->ReadFullyAtOffset(buffer.data(), file_content_.size() - 10, 10));
    ASSERT_EQ(file_content_.substr(10), std::string(buffer.begin(), buffer.end() - 10));
  }
}

TEST_F(PackageTest, ReadFullyAtOffset_failure) {
  for (const auto& package : packages_) {
    std::vector<uint8_t> buffer(file_content_.size());
    // Out of bound read.
    ASSERT_FALSE(package->ReadFullyAtOffset(buffer.data(), file_content_.size(), 10));
  }
}

TEST_F(PackageTest, UpdateHashAtOffset_sha1_hash) {
  // Check that the hash matches for first half of the file.
  uint64_t hash_size = file_content_.size() / 2;
  std::vector<uint8_t> expected_sha(SHA_DIGEST_LENGTH);
  SHA1(reinterpret_cast<uint8_t*>(file_content_.data()), hash_size, expected_sha.data());

  for (const auto& package : packages_) {
    SHA_CTX ctx;
    SHA1_Init(&ctx);
    std::vector<HasherUpdateCallback> hashers{ std::bind(&SHA1_Update, &ctx, std::placeholders::_1,
                                                         std::placeholders::_2) };
    package->UpdateHashAtOffset(hashers, 0, hash_size);

    std::vector<uint8_t> calculated_sha(SHA_DIGEST_LENGTH);
    SHA1_Final(calculated_sha.data(), &ctx);
    ASSERT_EQ(expected_sha, calculated_sha);
  }
}

TEST_F(PackageTest, GetZipArchiveHandle_extract_entry) {
  for (const auto& package : packages_) {
    ZipArchiveHandle zip = package->GetZipArchiveHandle();
    ASSERT_TRUE(zip);

    // Check that we can extract one zip entry.
    std::string_view entry_name = "dir1/file3.txt";
    ZipEntry entry;
    ASSERT_EQ(0, FindEntry(zip, entry_name, &entry));

    std::vector<uint8_t> extracted(entry_name.size());
    ASSERT_EQ(0, ExtractToMemory(zip, &entry, extracted.data(), extracted.size()));
    ASSERT_EQ(entry_name, std::string(extracted.begin(), extracted.end()));
  }
}
