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

#include <stdio.h>

#include <algorithm>
#include <string>
#include <tuple>
#include <vector>

#include <android-base/file.h>
#include <android-base/memory.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#include <applypatch/imgdiff.h>
#include <applypatch/imgdiff_image.h>
#include <applypatch/imgpatch.h>
#include <gtest/gtest.h>
#include <ziparchive/zip_writer.h>

#include "common/test_constants.h"

using android::base::get_unaligned;

// Sanity check for the given imgdiff patch header.
static void verify_patch_header(const std::string& patch, size_t* num_normal, size_t* num_raw,
                                size_t* num_deflate) {
  const size_t size = patch.size();
  const char* data = patch.data();

  ASSERT_GE(size, 12U);
  ASSERT_EQ("IMGDIFF2", std::string(data, 8));

  const int num_chunks = get_unaligned<int32_t>(data + 8);
  ASSERT_GE(num_chunks, 0);

  size_t normal = 0;
  size_t raw = 0;
  size_t deflate = 0;

  size_t pos = 12;
  for (int i = 0; i < num_chunks; ++i) {
    ASSERT_LE(pos + 4, size);
    int type = get_unaligned<int32_t>(data + pos);
    pos += 4;
    if (type == CHUNK_NORMAL) {
      pos += 24;
      ASSERT_LE(pos, size);
      normal++;
    } else if (type == CHUNK_RAW) {
      ASSERT_LE(pos + 4, size);
      ssize_t data_len = get_unaligned<int32_t>(data + pos);
      ASSERT_GT(data_len, 0);
      pos += 4 + data_len;
      ASSERT_LE(pos, size);
      raw++;
    } else if (type == CHUNK_DEFLATE) {
      pos += 60;
      ASSERT_LE(pos, size);
      deflate++;
    } else {
      FAIL() << "Invalid patch type: " << type;
    }
  }

  if (num_normal != nullptr) *num_normal = normal;
  if (num_raw != nullptr) *num_raw = raw;
  if (num_deflate != nullptr) *num_deflate = deflate;
}

static void GenerateTarget(const std::string& src, const std::string& patch, std::string* patched) {
  patched->clear();
  ASSERT_EQ(0, ApplyImagePatch(reinterpret_cast<const unsigned char*>(src.data()), src.size(),
                               reinterpret_cast<const unsigned char*>(patch.data()), patch.size(),
                               [&](const unsigned char* data, size_t len) {
                                 patched->append(reinterpret_cast<const char*>(data), len);
                                 return len;
                               }));
}

static void verify_patched_image(const std::string& src, const std::string& patch,
                                 const std::string& tgt) {
  std::string patched;
  GenerateTarget(src, patch, &patched);
  ASSERT_EQ(tgt, patched);
}

TEST(ImgdiffTest, invalid_args) {
  // Insufficient inputs.
  ASSERT_EQ(2, imgdiff(1, (const char* []){ "imgdiff" }));
  ASSERT_EQ(2, imgdiff(2, (const char* []){ "imgdiff", "-z" }));
  ASSERT_EQ(2, imgdiff(2, (const char* []){ "imgdiff", "-b" }));
  ASSERT_EQ(2, imgdiff(3, (const char* []){ "imgdiff", "-z", "-b" }));

  // Failed to read bonus file.
  ASSERT_EQ(1, imgdiff(3, (const char* []){ "imgdiff", "-b", "doesntexist" }));

  // Failed to read input files.
  ASSERT_EQ(1, imgdiff(4, (const char* []){ "imgdiff", "doesntexist", "doesntexist", "output" }));
  ASSERT_EQ(
      1, imgdiff(5, (const char* []){ "imgdiff", "-z", "doesntexist", "doesntexist", "output" }));
}

TEST(ImgdiffTest, image_mode_smoke) {
  // Random bytes.
  const std::string src("abcdefg");
  TemporaryFile src_file;
  ASSERT_TRUE(android::base::WriteStringToFile(src, src_file.path));

  const std::string tgt("abcdefgxyz");
  TemporaryFile tgt_file;
  ASSERT_TRUE(android::base::WriteStringToFile(tgt, tgt_file.path));

  TemporaryFile patch_file;
  std::vector<const char*> args = {
    "imgdiff", src_file.path, tgt_file.path, patch_file.path,
  };
  ASSERT_EQ(0, imgdiff(args.size(), args.data()));

  // Verify.
  std::string patch;
  ASSERT_TRUE(android::base::ReadFileToString(patch_file.path, &patch));

  // Expect one CHUNK_RAW entry.
  size_t num_normal;
  size_t num_raw;
  size_t num_deflate;
  verify_patch_header(patch, &num_normal, &num_raw, &num_deflate);
  ASSERT_EQ(0U, num_normal);
  ASSERT_EQ(0U, num_deflate);
  ASSERT_EQ(1U, num_raw);

  verify_patched_image(src, patch, tgt);
}

TEST(ImgdiffTest, zip_mode_smoke_store) {
  // Construct src and tgt zip files.
  TemporaryFile src_file;
  FILE* src_file_ptr = fdopen(src_file.release(), "wb");
  ZipWriter src_writer(src_file_ptr);
  ASSERT_EQ(0, src_writer.StartEntry("file1.txt", 0));  // Store mode.
  const std::string src_content("abcdefg");
  ASSERT_EQ(0, src_writer.WriteBytes(src_content.data(), src_content.size()));
  ASSERT_EQ(0, src_writer.FinishEntry());
  ASSERT_EQ(0, src_writer.Finish());
  ASSERT_EQ(0, fclose(src_file_ptr));

  TemporaryFile tgt_file;
  FILE* tgt_file_ptr = fdopen(tgt_file.release(), "wb");
  ZipWriter tgt_writer(tgt_file_ptr);
  ASSERT_EQ(0, tgt_writer.StartEntry("file1.txt", 0));  // Store mode.
  const std::string tgt_content("abcdefgxyz");
  ASSERT_EQ(0, tgt_writer.WriteBytes(tgt_content.data(), tgt_content.size()));
  ASSERT_EQ(0, tgt_writer.FinishEntry());
  ASSERT_EQ(0, tgt_writer.Finish());
  ASSERT_EQ(0, fclose(tgt_file_ptr));

  // Compute patch.
  TemporaryFile patch_file;
  std::vector<const char*> args = {
    "imgdiff", "-z", src_file.path, tgt_file.path, patch_file.path,
  };
  ASSERT_EQ(0, imgdiff(args.size(), args.data()));

  // Verify.
  std::string tgt;
  ASSERT_TRUE(android::base::ReadFileToString(tgt_file.path, &tgt));
  std::string src;
  ASSERT_TRUE(android::base::ReadFileToString(src_file.path, &src));
  std::string patch;
  ASSERT_TRUE(android::base::ReadFileToString(patch_file.path, &patch));

  // Expect one CHUNK_RAW entry.
  size_t num_normal;
  size_t num_raw;
  size_t num_deflate;
  verify_patch_header(patch, &num_normal, &num_raw, &num_deflate);
  ASSERT_EQ(0U, num_normal);
  ASSERT_EQ(0U, num_deflate);
  ASSERT_EQ(1U, num_raw);

  verify_patched_image(src, patch, tgt);
}

TEST(ImgdiffTest, zip_mode_smoke_compressed) {
  // Generate 1 block of random data.
  std::string random_data;
  random_data.reserve(4096);
  generate_n(back_inserter(random_data), 4096, []() { return rand() % 256; });

  // Construct src and tgt zip files.
  TemporaryFile src_file;
  FILE* src_file_ptr = fdopen(src_file.release(), "wb");
  ZipWriter src_writer(src_file_ptr);
  ASSERT_EQ(0, src_writer.StartEntry("file1.txt", ZipWriter::kCompress));
  const std::string src_content = random_data;
  ASSERT_EQ(0, src_writer.WriteBytes(src_content.data(), src_content.size()));
  ASSERT_EQ(0, src_writer.FinishEntry());
  ASSERT_EQ(0, src_writer.Finish());
  ASSERT_EQ(0, fclose(src_file_ptr));

  TemporaryFile tgt_file;
  FILE* tgt_file_ptr = fdopen(tgt_file.release(), "wb");
  ZipWriter tgt_writer(tgt_file_ptr);
  ASSERT_EQ(0, tgt_writer.StartEntry("file1.txt", ZipWriter::kCompress));
  const std::string tgt_content = random_data + "extra contents";
  ASSERT_EQ(0, tgt_writer.WriteBytes(tgt_content.data(), tgt_content.size()));
  ASSERT_EQ(0, tgt_writer.FinishEntry());
  ASSERT_EQ(0, tgt_writer.Finish());
  ASSERT_EQ(0, fclose(tgt_file_ptr));

  // Compute patch.
  TemporaryFile patch_file;
  std::vector<const char*> args = {
    "imgdiff", "-z", src_file.path, tgt_file.path, patch_file.path,
  };
  ASSERT_EQ(0, imgdiff(args.size(), args.data()));

  // Verify.
  std::string tgt;
  ASSERT_TRUE(android::base::ReadFileToString(tgt_file.path, &tgt));
  std::string src;
  ASSERT_TRUE(android::base::ReadFileToString(src_file.path, &src));
  std::string patch;
  ASSERT_TRUE(android::base::ReadFileToString(patch_file.path, &patch));

  // Expect three entries: CHUNK_RAW (header) + CHUNK_DEFLATE (data) + CHUNK_RAW (footer).
  size_t num_normal;
  size_t num_raw;
  size_t num_deflate;
  verify_patch_header(patch, &num_normal, &num_raw, &num_deflate);
  ASSERT_EQ(0U, num_normal);
  ASSERT_EQ(1U, num_deflate);
  ASSERT_EQ(2U, num_raw);

  verify_patched_image(src, patch, tgt);
}

TEST(ImgdiffTest, zip_mode_empty_target) {
  TemporaryFile src_file;
  FILE* src_file_ptr = fdopen(src_file.release(), "wb");
  ZipWriter src_writer(src_file_ptr);
  ASSERT_EQ(0, src_writer.StartEntry("file1.txt", ZipWriter::kCompress));
  const std::string src_content = "abcdefg";
  ASSERT_EQ(0, src_writer.WriteBytes(src_content.data(), src_content.size()));
  ASSERT_EQ(0, src_writer.FinishEntry());
  ASSERT_EQ(0, src_writer.Finish());
  ASSERT_EQ(0, fclose(src_file_ptr));

  // Construct a empty entry in the target zip.
  TemporaryFile tgt_file;
  FILE* tgt_file_ptr = fdopen(tgt_file.release(), "wb");
  ZipWriter tgt_writer(tgt_file_ptr);
  ASSERT_EQ(0, tgt_writer.StartEntry("file1.txt", ZipWriter::kCompress));
  const std::string tgt_content;
  ASSERT_EQ(0, tgt_writer.WriteBytes(tgt_content.data(), tgt_content.size()));
  ASSERT_EQ(0, tgt_writer.FinishEntry());
  ASSERT_EQ(0, tgt_writer.Finish());

  // Compute patch.
  TemporaryFile patch_file;
  std::vector<const char*> args = {
    "imgdiff", "-z", src_file.path, tgt_file.path, patch_file.path,
  };
  ASSERT_EQ(0, imgdiff(args.size(), args.data()));

  // Verify.
  std::string tgt;
  ASSERT_TRUE(android::base::ReadFileToString(tgt_file.path, &tgt));
  std::string src;
  ASSERT_TRUE(android::base::ReadFileToString(src_file.path, &src));
  std::string patch;
  ASSERT_TRUE(android::base::ReadFileToString(patch_file.path, &patch));

  verify_patched_image(src, patch, tgt);
}

TEST(ImgdiffTest, zip_mode_smoke_trailer_zeros) {
  // Generate 1 block of random data.
  std::string random_data;
  random_data.reserve(4096);
  generate_n(back_inserter(random_data), 4096, []() { return rand() % 256; });

  // Construct src and tgt zip files.
  TemporaryFile src_file;
  FILE* src_file_ptr = fdopen(src_file.release(), "wb");
  ZipWriter src_writer(src_file_ptr);
  ASSERT_EQ(0, src_writer.StartEntry("file1.txt", ZipWriter::kCompress));
  const std::string src_content = random_data;
  ASSERT_EQ(0, src_writer.WriteBytes(src_content.data(), src_content.size()));
  ASSERT_EQ(0, src_writer.FinishEntry());
  ASSERT_EQ(0, src_writer.Finish());
  ASSERT_EQ(0, fclose(src_file_ptr));

  TemporaryFile tgt_file;
  FILE* tgt_file_ptr = fdopen(tgt_file.release(), "wb");
  ZipWriter tgt_writer(tgt_file_ptr);
  ASSERT_EQ(0, tgt_writer.StartEntry("file1.txt", ZipWriter::kCompress));
  const std::string tgt_content = random_data + "abcdefg";
  ASSERT_EQ(0, tgt_writer.WriteBytes(tgt_content.data(), tgt_content.size()));
  ASSERT_EQ(0, tgt_writer.FinishEntry());
  ASSERT_EQ(0, tgt_writer.Finish());
  // Add trailing zeros to the target zip file.
  std::vector<uint8_t> zeros(10);
  ASSERT_EQ(zeros.size(), fwrite(zeros.data(), sizeof(uint8_t), zeros.size(), tgt_file_ptr));
  ASSERT_EQ(0, fclose(tgt_file_ptr));

  // Compute patch.
  TemporaryFile patch_file;
  std::vector<const char*> args = {
    "imgdiff", "-z", src_file.path, tgt_file.path, patch_file.path,
  };
  ASSERT_EQ(0, imgdiff(args.size(), args.data()));

  // Verify.
  std::string tgt;
  ASSERT_TRUE(android::base::ReadFileToString(tgt_file.path, &tgt));
  std::string src;
  ASSERT_TRUE(android::base::ReadFileToString(src_file.path, &src));
  std::string patch;
  ASSERT_TRUE(android::base::ReadFileToString(patch_file.path, &patch));

  // Expect three entries: CHUNK_RAW (header) + CHUNK_DEFLATE (data) + CHUNK_RAW (footer).
  size_t num_normal;
  size_t num_raw;
  size_t num_deflate;
  verify_patch_header(patch, &num_normal, &num_raw, &num_deflate);
  ASSERT_EQ(0U, num_normal);
  ASSERT_EQ(1U, num_deflate);
  ASSERT_EQ(2U, num_raw);

  verify_patched_image(src, patch, tgt);
}

TEST(ImgdiffTest, image_mode_simple) {
  std::string gzipped_source_path = from_testdata_base("gzipped_source");
  std::string gzipped_source;
  ASSERT_TRUE(android::base::ReadFileToString(gzipped_source_path, &gzipped_source));

  const std::string src = "abcdefg" + gzipped_source;
  TemporaryFile src_file;
  ASSERT_TRUE(android::base::WriteStringToFile(src, src_file.path));

  std::string gzipped_target_path = from_testdata_base("gzipped_target");
  std::string gzipped_target;
  ASSERT_TRUE(android::base::ReadFileToString(gzipped_target_path, &gzipped_target));
  const std::string tgt = "abcdefgxyz" + gzipped_target;

  TemporaryFile tgt_file;
  ASSERT_TRUE(android::base::WriteStringToFile(tgt, tgt_file.path));

  TemporaryFile patch_file;
  std::vector<const char*> args = {
    "imgdiff", src_file.path, tgt_file.path, patch_file.path,
  };
  ASSERT_EQ(0, imgdiff(args.size(), args.data()));

  // Verify.
  std::string patch;
  ASSERT_TRUE(android::base::ReadFileToString(patch_file.path, &patch));

  // Expect three entries: CHUNK_RAW (header) + CHUNK_DEFLATE (data) + CHUNK_RAW (footer).
  size_t num_normal;
  size_t num_raw;
  size_t num_deflate;
  verify_patch_header(patch, &num_normal, &num_raw, &num_deflate);
  ASSERT_EQ(0U, num_normal);
  ASSERT_EQ(1U, num_deflate);
  ASSERT_EQ(2U, num_raw);

  verify_patched_image(src, patch, tgt);
}

TEST(ImgdiffTest, image_mode_bad_gzip) {
  // Modify the uncompressed length in the gzip footer.
  const std::vector<char> src_data = { 'a',    'b',    'c',    'd',    'e',    'f',    'g',
                                       'h',    '\x1f', '\x8b', '\x08', '\x00', '\xc4', '\x1e',
                                       '\x53', '\x58', '\x00', '\x03', '\xab', '\xa8', '\xac',
                                       '\x02', '\x00', '\x67', '\xba', '\x8e', '\xeb', '\x03',
                                       '\xff', '\xff', '\xff' };
  const std::string src(src_data.cbegin(), src_data.cend());
  TemporaryFile src_file;
  ASSERT_TRUE(android::base::WriteStringToFile(src, src_file.path));

  // Modify the uncompressed length in the gzip footer.
  const std::vector<char> tgt_data = {
    'a',    'b',    'c',    'd',    'e',    'f',    'g',    'x',    'y',    'z',    '\x1f', '\x8b',
    '\x08', '\x00', '\x62', '\x1f', '\x53', '\x58', '\x00', '\x03', '\xab', '\xa8', '\xa8', '\xac',
    '\xac', '\xaa', '\x02', '\x00', '\x96', '\x30', '\x06', '\xb7', '\x06', '\xff', '\xff', '\xff'
  };
  const std::string tgt(tgt_data.cbegin(), tgt_data.cend());
  TemporaryFile tgt_file;
  ASSERT_TRUE(android::base::WriteStringToFile(tgt, tgt_file.path));

  TemporaryFile patch_file;
  std::vector<const char*> args = {
    "imgdiff", src_file.path, tgt_file.path, patch_file.path,
  };
  ASSERT_EQ(0, imgdiff(args.size(), args.data()));

  // Verify.
  std::string patch;
  ASSERT_TRUE(android::base::ReadFileToString(patch_file.path, &patch));
  verify_patched_image(src, patch, tgt);
}

TEST(ImgdiffTest, image_mode_different_num_chunks) {
  // src: "abcdefgh" + gzipped "xyz" (echo -n "xyz" | gzip -f | hd) + gzipped "test".
  const std::vector<char> src_data = {
    'a',    'b',    'c',    'd',    'e',    'f',    'g',    'h',    '\x1f', '\x8b', '\x08',
    '\x00', '\xc4', '\x1e', '\x53', '\x58', '\x00', '\x03', '\xab', '\xa8', '\xac', '\x02',
    '\x00', '\x67', '\xba', '\x8e', '\xeb', '\x03', '\x00', '\x00', '\x00', '\x1f', '\x8b',
    '\x08', '\x00', '\xb2', '\x3a', '\x53', '\x58', '\x00', '\x03', '\x2b', '\x49', '\x2d',
    '\x2e', '\x01', '\x00', '\x0c', '\x7e', '\x7f', '\xd8', '\x04', '\x00', '\x00', '\x00'
  };
  const std::string src(src_data.cbegin(), src_data.cend());
  TemporaryFile src_file;
  ASSERT_TRUE(android::base::WriteStringToFile(src, src_file.path));

  // tgt: "abcdefgxyz" + gzipped "xxyyzz".
  const std::vector<char> tgt_data = {
    'a',    'b',    'c',    'd',    'e',    'f',    'g',    'x',    'y',    'z',    '\x1f', '\x8b',
    '\x08', '\x00', '\x62', '\x1f', '\x53', '\x58', '\x00', '\x03', '\xab', '\xa8', '\xa8', '\xac',
    '\xac', '\xaa', '\x02', '\x00', '\x96', '\x30', '\x06', '\xb7', '\x06', '\x00', '\x00', '\x00'
  };
  const std::string tgt(tgt_data.cbegin(), tgt_data.cend());
  TemporaryFile tgt_file;
  ASSERT_TRUE(android::base::WriteStringToFile(tgt, tgt_file.path));

  TemporaryFile patch_file;
  std::vector<const char*> args = {
    "imgdiff", src_file.path, tgt_file.path, patch_file.path,
  };
  ASSERT_EQ(1, imgdiff(args.size(), args.data()));
}

TEST(ImgdiffTest, image_mode_merge_chunks) {
  // src: "abcdefg" + gzipped_source.
  std::string gzipped_source_path = from_testdata_base("gzipped_source");
  std::string gzipped_source;
  ASSERT_TRUE(android::base::ReadFileToString(gzipped_source_path, &gzipped_source));

  const std::string src = "abcdefg" + gzipped_source;
  TemporaryFile src_file;
  ASSERT_TRUE(android::base::WriteStringToFile(src, src_file.path));

  // tgt: gzipped_target + "abcdefgxyz".
  std::string gzipped_target_path = from_testdata_base("gzipped_target");
  std::string gzipped_target;
  ASSERT_TRUE(android::base::ReadFileToString(gzipped_target_path, &gzipped_target));

  const std::string tgt = gzipped_target + "abcdefgxyz";
  TemporaryFile tgt_file;
  ASSERT_TRUE(android::base::WriteStringToFile(tgt, tgt_file.path));

  // Since a gzipped entry will become CHUNK_RAW (header) + CHUNK_DEFLATE (data) +
  // CHUNK_RAW (footer), they both should contain the same chunk types after merging.

  TemporaryFile patch_file;
  std::vector<const char*> args = {
    "imgdiff", src_file.path, tgt_file.path, patch_file.path,
  };
  ASSERT_EQ(0, imgdiff(args.size(), args.data()));

  // Verify.
  std::string patch;
  ASSERT_TRUE(android::base::ReadFileToString(patch_file.path, &patch));

  // Expect three entries: CHUNK_RAW (header) + CHUNK_DEFLATE (data) + CHUNK_RAW (footer).
  size_t num_normal;
  size_t num_raw;
  size_t num_deflate;
  verify_patch_header(patch, &num_normal, &num_raw, &num_deflate);
  ASSERT_EQ(0U, num_normal);
  ASSERT_EQ(1U, num_deflate);
  ASSERT_EQ(2U, num_raw);

  verify_patched_image(src, patch, tgt);
}

TEST(ImgdiffTest, image_mode_spurious_magic) {
  // src: "abcdefgh" + '0x1f8b0b00' + some bytes.
  const std::vector<char> src_data = { 'a',    'b',    'c',    'd',    'e',    'f',    'g',
                                       'h',    '\x1f', '\x8b', '\x08', '\x00', '\xc4', '\x1e',
                                       '\x53', '\x58', 't',    'e',    's',    't' };
  const std::string src(src_data.cbegin(), src_data.cend());
  TemporaryFile src_file;
  ASSERT_TRUE(android::base::WriteStringToFile(src, src_file.path));

  // tgt: "abcdefgxyz".
  const std::vector<char> tgt_data = { 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'x', 'y', 'z' };
  const std::string tgt(tgt_data.cbegin(), tgt_data.cend());
  TemporaryFile tgt_file;
  ASSERT_TRUE(android::base::WriteStringToFile(tgt, tgt_file.path));

  TemporaryFile patch_file;
  std::vector<const char*> args = {
    "imgdiff", src_file.path, tgt_file.path, patch_file.path,
  };
  ASSERT_EQ(0, imgdiff(args.size(), args.data()));

  // Verify.
  std::string patch;
  ASSERT_TRUE(android::base::ReadFileToString(patch_file.path, &patch));

  // Expect one CHUNK_RAW (header) entry.
  size_t num_normal;
  size_t num_raw;
  size_t num_deflate;
  verify_patch_header(patch, &num_normal, &num_raw, &num_deflate);
  ASSERT_EQ(0U, num_normal);
  ASSERT_EQ(0U, num_deflate);
  ASSERT_EQ(1U, num_raw);

  verify_patched_image(src, patch, tgt);
}

TEST(ImgdiffTest, image_mode_short_input1) {
  // src: "abcdefgh" + '0x1f8b0b'.
  const std::vector<char> src_data = { 'a', 'b', 'c',    'd',    'e',   'f',
                                       'g', 'h', '\x1f', '\x8b', '\x08' };
  const std::string src(src_data.cbegin(), src_data.cend());
  TemporaryFile src_file;
  ASSERT_TRUE(android::base::WriteStringToFile(src, src_file.path));

  // tgt: "abcdefgxyz".
  const std::vector<char> tgt_data = { 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'x', 'y', 'z' };
  const std::string tgt(tgt_data.cbegin(), tgt_data.cend());
  TemporaryFile tgt_file;
  ASSERT_TRUE(android::base::WriteStringToFile(tgt, tgt_file.path));

  TemporaryFile patch_file;
  std::vector<const char*> args = {
    "imgdiff", src_file.path, tgt_file.path, patch_file.path,
  };
  ASSERT_EQ(0, imgdiff(args.size(), args.data()));

  // Verify.
  std::string patch;
  ASSERT_TRUE(android::base::ReadFileToString(patch_file.path, &patch));

  // Expect one CHUNK_RAW (header) entry.
  size_t num_normal;
  size_t num_raw;
  size_t num_deflate;
  verify_patch_header(patch, &num_normal, &num_raw, &num_deflate);
  ASSERT_EQ(0U, num_normal);
  ASSERT_EQ(0U, num_deflate);
  ASSERT_EQ(1U, num_raw);

  verify_patched_image(src, patch, tgt);
}

TEST(ImgdiffTest, image_mode_short_input2) {
  // src: "abcdefgh" + '0x1f8b0b00'.
  const std::vector<char> src_data = { 'a', 'b', 'c',    'd',    'e',    'f',
                                       'g', 'h', '\x1f', '\x8b', '\x08', '\x00' };
  const std::string src(src_data.cbegin(), src_data.cend());
  TemporaryFile src_file;
  ASSERT_TRUE(android::base::WriteStringToFile(src, src_file.path));

  // tgt: "abcdefgxyz".
  const std::vector<char> tgt_data = { 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'x', 'y', 'z' };
  const std::string tgt(tgt_data.cbegin(), tgt_data.cend());
  TemporaryFile tgt_file;
  ASSERT_TRUE(android::base::WriteStringToFile(tgt, tgt_file.path));

  TemporaryFile patch_file;
  std::vector<const char*> args = {
    "imgdiff", src_file.path, tgt_file.path, patch_file.path,
  };
  ASSERT_EQ(0, imgdiff(args.size(), args.data()));

  // Verify.
  std::string patch;
  ASSERT_TRUE(android::base::ReadFileToString(patch_file.path, &patch));

  // Expect one CHUNK_RAW (header) entry.
  size_t num_normal;
  size_t num_raw;
  size_t num_deflate;
  verify_patch_header(patch, &num_normal, &num_raw, &num_deflate);
  ASSERT_EQ(0U, num_normal);
  ASSERT_EQ(0U, num_deflate);
  ASSERT_EQ(1U, num_raw);

  verify_patched_image(src, patch, tgt);
}

TEST(ImgdiffTest, image_mode_single_entry_long) {
  // src: "abcdefgh" + '0x1f8b0b00' + some bytes.
  const std::vector<char> src_data = { 'a',    'b',    'c',    'd',    'e',    'f',    'g',
                                       'h',    '\x1f', '\x8b', '\x08', '\x00', '\xc4', '\x1e',
                                       '\x53', '\x58', 't',    'e',    's',    't' };
  const std::string src(src_data.cbegin(), src_data.cend());
  TemporaryFile src_file;
  ASSERT_TRUE(android::base::WriteStringToFile(src, src_file.path));

  // tgt: "abcdefgxyz" + 200 bytes.
  std::vector<char> tgt_data = { 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'x', 'y', 'z' };
  tgt_data.resize(tgt_data.size() + 200);

  const std::string tgt(tgt_data.cbegin(), tgt_data.cend());
  TemporaryFile tgt_file;
  ASSERT_TRUE(android::base::WriteStringToFile(tgt, tgt_file.path));

  TemporaryFile patch_file;
  std::vector<const char*> args = {
    "imgdiff", src_file.path, tgt_file.path, patch_file.path,
  };
  ASSERT_EQ(0, imgdiff(args.size(), args.data()));

  // Verify.
  std::string patch;
  ASSERT_TRUE(android::base::ReadFileToString(patch_file.path, &patch));

  // Expect one CHUNK_NORMAL entry, since it's exceeding the 160-byte limit for RAW.
  size_t num_normal;
  size_t num_raw;
  size_t num_deflate;
  verify_patch_header(patch, &num_normal, &num_raw, &num_deflate);
  ASSERT_EQ(1U, num_normal);
  ASSERT_EQ(0U, num_deflate);
  ASSERT_EQ(0U, num_raw);

  verify_patched_image(src, patch, tgt);
}

TEST(ImgpatchTest, image_mode_patch_corruption) {
  // src: "abcdefgh" + gzipped "xyz" (echo -n "xyz" | gzip -f | hd).
  const std::vector<char> src_data = { 'a',    'b',    'c',    'd',    'e',    'f',    'g',
                                       'h',    '\x1f', '\x8b', '\x08', '\x00', '\xc4', '\x1e',
                                       '\x53', '\x58', '\x00', '\x03', '\xab', '\xa8', '\xac',
                                       '\x02', '\x00', '\x67', '\xba', '\x8e', '\xeb', '\x03',
                                       '\x00', '\x00', '\x00' };
  const std::string src(src_data.cbegin(), src_data.cend());
  TemporaryFile src_file;
  ASSERT_TRUE(android::base::WriteStringToFile(src, src_file.path));

  // tgt: "abcdefgxyz" + gzipped "xxyyzz".
  const std::vector<char> tgt_data = {
    'a',    'b',    'c',    'd',    'e',    'f',    'g',    'x',    'y',    'z',    '\x1f', '\x8b',
    '\x08', '\x00', '\x62', '\x1f', '\x53', '\x58', '\x00', '\x03', '\xab', '\xa8', '\xa8', '\xac',
    '\xac', '\xaa', '\x02', '\x00', '\x96', '\x30', '\x06', '\xb7', '\x06', '\x00', '\x00', '\x00'
  };
  const std::string tgt(tgt_data.cbegin(), tgt_data.cend());
  TemporaryFile tgt_file;
  ASSERT_TRUE(android::base::WriteStringToFile(tgt, tgt_file.path));

  TemporaryFile patch_file;
  std::vector<const char*> args = {
    "imgdiff", src_file.path, tgt_file.path, patch_file.path,
  };
  ASSERT_EQ(0, imgdiff(args.size(), args.data()));

  // Verify.
  std::string patch;
  ASSERT_TRUE(android::base::ReadFileToString(patch_file.path, &patch));
  verify_patched_image(src, patch, tgt);

  // Corrupt the end of the patch and expect the ApplyImagePatch to fail.
  patch.insert(patch.end() - 10, 10, '0');
  ASSERT_EQ(-1, ApplyImagePatch(reinterpret_cast<const unsigned char*>(src.data()), src.size(),
                                reinterpret_cast<const unsigned char*>(patch.data()), patch.size(),
                                [](const unsigned char* /*data*/, size_t len) { return len; }));
}

static void construct_store_entry(const std::vector<std::tuple<std::string, size_t, char>>& info,
                                  ZipWriter* writer) {
  for (auto& t : info) {
    // Create t(1) blocks of t(2), and write the data to t(0)
    ASSERT_EQ(0, writer->StartEntry(std::get<0>(t).c_str(), 0));
    const std::string content(std::get<1>(t) * 4096, std::get<2>(t));
    ASSERT_EQ(0, writer->WriteBytes(content.data(), content.size()));
    ASSERT_EQ(0, writer->FinishEntry());
  }
}

static void construct_deflate_entry(const std::vector<std::tuple<std::string, size_t, size_t>>& info,
                                    ZipWriter* writer, const std::string& data) {
  for (auto& t : info) {
    // t(0): entry_name; t(1): block offset; t(2) length in blocks.
    ASSERT_EQ(0, writer->StartEntry(std::get<0>(t).c_str(), ZipWriter::kCompress));
    ASSERT_EQ(0, writer->WriteBytes(data.data() + std::get<1>(t) * 4096, std::get<2>(t) * 4096));
    ASSERT_EQ(0, writer->FinishEntry());
  }
}

// Look for the source and patch pieces in debug_dir. Generate a target piece from each pair.
// Concatenate all the target pieces and match against the orignal one. Used pieces in debug_dir
// will be cleaned up.
static void GenerateAndCheckSplitTarget(const std::string& debug_dir, size_t count,
                                        const std::string& tgt) {
  std::string patched;
  for (size_t i = 0; i < count; i++) {
    std::string split_src_path = android::base::StringPrintf("%s/src-%zu", debug_dir.c_str(), i);
    std::string split_src;
    ASSERT_TRUE(android::base::ReadFileToString(split_src_path, &split_src));
    ASSERT_EQ(0, unlink(split_src_path.c_str()));

    std::string split_patch_path =
        android::base::StringPrintf("%s/patch-%zu", debug_dir.c_str(), i);
    std::string split_patch;
    ASSERT_TRUE(android::base::ReadFileToString(split_patch_path, &split_patch));
    ASSERT_EQ(0, unlink(split_patch_path.c_str()));

    std::string split_tgt;
    GenerateTarget(split_src, split_patch, &split_tgt);
    patched += split_tgt;
  }

  // Verify we can get back the original target image.
  ASSERT_EQ(tgt, patched);
}

std::vector<ImageChunk> ConstructImageChunks(
    const std::vector<uint8_t>& content, const std::vector<std::tuple<std::string, size_t>>& info) {
  std::vector<ImageChunk> chunks;
  size_t start = 0;
  for (const auto& t : info) {
    size_t length = std::get<1>(t);
    chunks.emplace_back(CHUNK_NORMAL, start, &content, length, std::get<0>(t));
    start += length;
  }

  return chunks;
}

TEST(ImgdiffTest, zip_mode_split_image_smoke) {
  std::vector<uint8_t> content;
  content.reserve(4096 * 50);
  uint8_t n = 0;
  generate_n(back_inserter(content), 4096 * 50, [&n]() { return n++ / 4096; });

  ZipModeImage tgt_image(false, 4096 * 10);
  std::vector<ImageChunk> tgt_chunks = ConstructImageChunks(content, { { "a", 100 },
                                                                       { "b", 4096 * 2 },
                                                                       { "c", 4096 * 3 },
                                                                       { "d", 300 },
                                                                       { "e-0", 4096 * 10 },
                                                                       { "e-1", 4096 * 5 },
                                                                       { "CD", 200 } });
  tgt_image.Initialize(std::move(tgt_chunks),
                       std::vector<uint8_t>(content.begin(), content.begin() + 82520));

  tgt_image.DumpChunks();

  ZipModeImage src_image(true, 4096 * 10);
  std::vector<ImageChunk> src_chunks = ConstructImageChunks(content, { { "b", 4096 * 3 },
                                                                       { "c-0", 4096 * 10 },
                                                                       { "c-1", 4096 * 2 },
                                                                       { "a", 4096 * 5 },
                                                                       { "e-0", 4096 * 10 },
                                                                       { "e-1", 10000 },
                                                                       { "CD", 5000 } });
  src_image.Initialize(std::move(src_chunks),
                       std::vector<uint8_t>(content.begin(), content.begin() + 137880));

  std::vector<ZipModeImage> split_tgt_images;
  std::vector<ZipModeImage> split_src_images;
  std::vector<SortedRangeSet> split_src_ranges;

  ZipModeImage::SplitZipModeImageWithLimit(tgt_image, src_image, &split_tgt_images,
                                           &split_src_images, &split_src_ranges);

  // src_piece 1: a 5 blocks, b 3 blocks
  // src_piece 2: c-0 10 blocks
  // src_piece 3: d 0 block, e-0 10 blocks
  // src_piece 4: e-1 2 blocks; CD 2 blocks
  ASSERT_EQ(split_tgt_images.size(), split_src_images.size());
  ASSERT_EQ(static_cast<size_t>(4), split_tgt_images.size());

  ASSERT_EQ(static_cast<size_t>(1), split_tgt_images[0].NumOfChunks());
  ASSERT_EQ(static_cast<size_t>(12288), split_tgt_images[0][0].DataLengthForPatch());
  ASSERT_EQ("4,0,3,15,20", split_src_ranges[0].ToString());

  ASSERT_EQ(static_cast<size_t>(1), split_tgt_images[1].NumOfChunks());
  ASSERT_EQ(static_cast<size_t>(12288), split_tgt_images[1][0].DataLengthForPatch());
  ASSERT_EQ("2,3,13", split_src_ranges[1].ToString());

  ASSERT_EQ(static_cast<size_t>(1), split_tgt_images[2].NumOfChunks());
  ASSERT_EQ(static_cast<size_t>(40960), split_tgt_images[2][0].DataLengthForPatch());
  ASSERT_EQ("2,20,30", split_src_ranges[2].ToString());

  ASSERT_EQ(static_cast<size_t>(1), split_tgt_images[3].NumOfChunks());
  ASSERT_EQ(static_cast<size_t>(16984), split_tgt_images[3][0].DataLengthForPatch());
  ASSERT_EQ("2,30,34", split_src_ranges[3].ToString());
}

TEST(ImgdiffTest, zip_mode_store_large_apk) {
  // Construct src and tgt zip files with limit = 10 blocks.
  //     src              tgt
  //  12 blocks 'd'    3 blocks  'a'
  //  8 blocks  'c'    3 blocks  'b'
  //  3 blocks  'b'    8 blocks  'c' (exceeds limit)
  //  3 blocks  'a'    12 blocks 'd' (exceeds limit)
  //                   3 blocks  'e'
  TemporaryFile tgt_file;
  FILE* tgt_file_ptr = fdopen(tgt_file.release(), "wb");
  ZipWriter tgt_writer(tgt_file_ptr);
  construct_store_entry(
      { { "a", 3, 'a' }, { "b", 3, 'b' }, { "c", 8, 'c' }, { "d", 12, 'd' }, { "e", 3, 'e' } },
      &tgt_writer);
  ASSERT_EQ(0, tgt_writer.Finish());
  ASSERT_EQ(0, fclose(tgt_file_ptr));

  TemporaryFile src_file;
  FILE* src_file_ptr = fdopen(src_file.release(), "wb");
  ZipWriter src_writer(src_file_ptr);
  construct_store_entry({ { "d", 12, 'd' }, { "c", 8, 'c' }, { "b", 3, 'b' }, { "a", 3, 'a' } },
                        &src_writer);
  ASSERT_EQ(0, src_writer.Finish());
  ASSERT_EQ(0, fclose(src_file_ptr));

  // Compute patch.
  TemporaryFile patch_file;
  TemporaryFile split_info_file;
  TemporaryDir debug_dir;
  std::string split_info_arg = android::base::StringPrintf("--split-info=%s", split_info_file.path);
  std::string debug_dir_arg = android::base::StringPrintf("--debug-dir=%s", debug_dir.path);
  std::vector<const char*> args = {
    "imgdiff", "-z", "--block-limit=10", split_info_arg.c_str(), debug_dir_arg.c_str(),
    src_file.path, tgt_file.path, patch_file.path,
  };
  ASSERT_EQ(0, imgdiff(args.size(), args.data()));

  std::string tgt;
  ASSERT_TRUE(android::base::ReadFileToString(tgt_file.path, &tgt));

  // Expect 4 pieces of patch. (Roughly 3'a',3'b'; 8'c'; 10'd'; 2'd'3'e')
  GenerateAndCheckSplitTarget(debug_dir.path, 4, tgt);
}

TEST(ImgdiffTest, zip_mode_deflate_large_apk) {
  // Src and tgt zip files are constructed as follows.
  //     src               tgt
  //  22 blocks, "d"    4  blocks,  "a"
  //  5 blocks,  "b"    4  blocks,  "b"
  //  3 blocks,  "a"    8  blocks,  "c" (exceeds limit)
  //  1 block,   "g"    20 blocks,  "d" (exceeds limit)
  //  8 blocks,  "c"    2  blocks,  "e"
  //  1 block,   "f"    1  block ,  "f"
  std::string tgt_path = from_testdata_base("deflate_tgt.zip");
  std::string src_path = from_testdata_base("deflate_src.zip");

  ZipModeImage src_image(true, 10 * 4096);
  ZipModeImage tgt_image(false, 10 * 4096);
  ASSERT_TRUE(src_image.Initialize(src_path));
  ASSERT_TRUE(tgt_image.Initialize(tgt_path));
  ASSERT_TRUE(ZipModeImage::CheckAndProcessChunks(&tgt_image, &src_image));

  src_image.DumpChunks();
  tgt_image.DumpChunks();

  std::vector<ZipModeImage> split_tgt_images;
  std::vector<ZipModeImage> split_src_images;
  std::vector<SortedRangeSet> split_src_ranges;
  ZipModeImage::SplitZipModeImageWithLimit(tgt_image, src_image, &split_tgt_images,
                                           &split_src_images, &split_src_ranges);

  // Expected split images with limit = 10 blocks.
  // src_piece 0: a 3 blocks, b 5 blocks
  // src_piece 1: c 8 blocks
  // src_piece 2: d-0 10 block
  // src_piece 3: d-1 10 blocks
  // src_piece 4: e 1 block, CD
  ASSERT_EQ(split_tgt_images.size(), split_src_images.size());
  ASSERT_EQ(static_cast<size_t>(5), split_tgt_images.size());

  ASSERT_EQ(static_cast<size_t>(2), split_src_images[0].NumOfChunks());
  ASSERT_EQ("a", split_src_images[0][0].GetEntryName());
  ASSERT_EQ("b", split_src_images[0][1].GetEntryName());

  ASSERT_EQ(static_cast<size_t>(1), split_src_images[1].NumOfChunks());
  ASSERT_EQ("c", split_src_images[1][0].GetEntryName());

  ASSERT_EQ(static_cast<size_t>(0), split_src_images[2].NumOfChunks());
  ASSERT_EQ(static_cast<size_t>(0), split_src_images[3].NumOfChunks());
  ASSERT_EQ(static_cast<size_t>(0), split_src_images[4].NumOfChunks());

  // Compute patch.
  TemporaryFile patch_file;
  TemporaryFile split_info_file;
  TemporaryDir debug_dir;
  ASSERT_TRUE(ZipModeImage::GeneratePatches(split_tgt_images, split_src_images, split_src_ranges,
                                            patch_file.path, split_info_file.path, debug_dir.path));

  // Verify the content of split info.
  // Expect 5 pieces of patch. ["a","b"; "c"; "d-0"; "d-1"; "e"]
  std::string split_info_string;
  android::base::ReadFileToString(split_info_file.path, &split_info_string);
  std::vector<std::string> info_list =
      android::base::Split(android::base::Trim(split_info_string), "\n");

  ASSERT_EQ(static_cast<size_t>(7), info_list.size());
  ASSERT_EQ("2", android::base::Trim(info_list[0]));
  ASSERT_EQ("5", android::base::Trim(info_list[1]));

  std::string tgt;
  ASSERT_TRUE(android::base::ReadFileToString(tgt_path, &tgt));
  ASSERT_EQ(static_cast<size_t>(160385), tgt.size());
  std::vector<std::string> tgt_file_ranges = {
    "36864 2,22,31", "32768 2,31,40", "40960 2,0,11", "40960 2,11,21", "8833 4,21,22,40,41",
  };

  for (size_t i = 0; i < 5; i++) {
    struct stat st;
    std::string path = android::base::StringPrintf("%s/patch-%zu", debug_dir.path, i);
    ASSERT_EQ(0, stat(path.c_str(), &st));
    ASSERT_EQ(std::to_string(st.st_size) + " " + tgt_file_ranges[i],
              android::base::Trim(info_list[i + 2]));
  }

  GenerateAndCheckSplitTarget(debug_dir.path, 5, tgt);
}

TEST(ImgdiffTest, zip_mode_no_match_source) {
  // Generate 20 blocks of random data.
  std::string random_data;
  random_data.reserve(4096 * 20);
  generate_n(back_inserter(random_data), 4096 * 20, []() { return rand() % 256; });

  TemporaryFile tgt_file;
  FILE* tgt_file_ptr = fdopen(tgt_file.release(), "wb");
  ZipWriter tgt_writer(tgt_file_ptr);

  construct_deflate_entry({ { "a", 0, 4 }, { "b", 5, 5 }, { "c", 11, 5 } }, &tgt_writer,
                          random_data);

  ASSERT_EQ(0, tgt_writer.Finish());
  ASSERT_EQ(0, fclose(tgt_file_ptr));

  // We don't have a matching source entry.
  TemporaryFile src_file;
  FILE* src_file_ptr = fdopen(src_file.release(), "wb");
  ZipWriter src_writer(src_file_ptr);
  construct_store_entry({ { "d", 1, 'd' } }, &src_writer);
  ASSERT_EQ(0, src_writer.Finish());
  ASSERT_EQ(0, fclose(src_file_ptr));

  // Compute patch.
  TemporaryFile patch_file;
  TemporaryFile split_info_file;
  TemporaryDir debug_dir;
  std::string split_info_arg = android::base::StringPrintf("--split-info=%s", split_info_file.path);
  std::string debug_dir_arg = android::base::StringPrintf("--debug-dir=%s", debug_dir.path);
  std::vector<const char*> args = {
    "imgdiff", "-z", "--block-limit=10", debug_dir_arg.c_str(), split_info_arg.c_str(),
    src_file.path, tgt_file.path, patch_file.path,
  };
  ASSERT_EQ(0, imgdiff(args.size(), args.data()));

  std::string tgt;
  ASSERT_TRUE(android::base::ReadFileToString(tgt_file.path, &tgt));

  // Expect 1 pieces of patch due to no matching source entry.
  GenerateAndCheckSplitTarget(debug_dir.path, 1, tgt);
}

TEST(ImgdiffTest, zip_mode_large_enough_limit) {
  // Generate 20 blocks of random data.
  std::string random_data;
  random_data.reserve(4096 * 20);
  generate_n(back_inserter(random_data), 4096 * 20, []() { return rand() % 256; });

  TemporaryFile tgt_file;
  FILE* tgt_file_ptr = fdopen(tgt_file.release(), "wb");
  ZipWriter tgt_writer(tgt_file_ptr);

  construct_deflate_entry({ { "a", 0, 10 }, { "b", 10, 5 } }, &tgt_writer, random_data);

  ASSERT_EQ(0, tgt_writer.Finish());
  ASSERT_EQ(0, fclose(tgt_file_ptr));

  // Construct 10 blocks of source.
  TemporaryFile src_file;
  FILE* src_file_ptr = fdopen(src_file.release(), "wb");
  ZipWriter src_writer(src_file_ptr);
  construct_deflate_entry({ { "a", 1, 10 } }, &src_writer, random_data);
  ASSERT_EQ(0, src_writer.Finish());
  ASSERT_EQ(0, fclose(src_file_ptr));

  // Compute patch with a limit of 20 blocks.
  TemporaryFile patch_file;
  TemporaryFile split_info_file;
  TemporaryDir debug_dir;
  std::string split_info_arg = android::base::StringPrintf("--split-info=%s", split_info_file.path);
  std::string debug_dir_arg = android::base::StringPrintf("--debug-dir=%s", debug_dir.path);
  std::vector<const char*> args = {
    "imgdiff", "-z", "--block-limit=20", split_info_arg.c_str(), debug_dir_arg.c_str(),
    src_file.path, tgt_file.path, patch_file.path,
  };
  ASSERT_EQ(0, imgdiff(args.size(), args.data()));

  std::string tgt;
  ASSERT_TRUE(android::base::ReadFileToString(tgt_file.path, &tgt));

  // Expect 1 piece of patch since limit is larger than the zip file size.
  GenerateAndCheckSplitTarget(debug_dir.path, 1, tgt);
}

TEST(ImgdiffTest, zip_mode_large_apk_small_target_chunk) {
  TemporaryFile tgt_file;
  FILE* tgt_file_ptr = fdopen(tgt_file.release(), "wb");
  ZipWriter tgt_writer(tgt_file_ptr);

  // The first entry is less than 4096 bytes, followed immediately by an entry that has a very
  // large counterpart in the source file. Therefore the first entry will be patched separately.
  std::string small_chunk("a", 2000);
  ASSERT_EQ(0, tgt_writer.StartEntry("a", 0));
  ASSERT_EQ(0, tgt_writer.WriteBytes(small_chunk.data(), small_chunk.size()));
  ASSERT_EQ(0, tgt_writer.FinishEntry());
  construct_store_entry(
      {
          { "b", 12, 'b' }, { "c", 3, 'c' },
      },
      &tgt_writer);
  ASSERT_EQ(0, tgt_writer.Finish());
  ASSERT_EQ(0, fclose(tgt_file_ptr));

  TemporaryFile src_file;
  FILE* src_file_ptr = fdopen(src_file.release(), "wb");
  ZipWriter src_writer(src_file_ptr);
  construct_store_entry({ { "a", 1, 'a' }, { "b", 13, 'b' }, { "c", 1, 'c' } }, &src_writer);
  ASSERT_EQ(0, src_writer.Finish());
  ASSERT_EQ(0, fclose(src_file_ptr));

  // Compute patch.
  TemporaryFile patch_file;
  TemporaryFile split_info_file;
  TemporaryDir debug_dir;
  std::string split_info_arg = android::base::StringPrintf("--split-info=%s", split_info_file.path);
  std::string debug_dir_arg = android::base::StringPrintf("--debug-dir=%s", debug_dir.path);
  std::vector<const char*> args = {
    "imgdiff",     "-z",          "--block-limit=10", split_info_arg.c_str(), debug_dir_arg.c_str(),
    src_file.path, tgt_file.path, patch_file.path,
  };
  ASSERT_EQ(0, imgdiff(args.size(), args.data()));

  std::string tgt;
  ASSERT_TRUE(android::base::ReadFileToString(tgt_file.path, &tgt));

  // Expect three split src images:
  // src_piece 0: a 1 blocks
  // src_piece 1: b-0 10 blocks
  // src_piece 2: b-1 3 blocks, c 1 blocks, CD
  GenerateAndCheckSplitTarget(debug_dir.path, 3, tgt);
}

TEST(ImgdiffTest, zip_mode_large_apk_skipped_small_target_chunk) {
  TemporaryFile tgt_file;
  FILE* tgt_file_ptr = fdopen(tgt_file.release(), "wb");
  ZipWriter tgt_writer(tgt_file_ptr);

  construct_store_entry(
      {
          { "a", 11, 'a' },
      },
      &tgt_writer);

  // Construct a tiny target entry of 1 byte, which will be skipped due to the tail alignment of
  // the previous entry.
  std::string small_chunk("b", 1);
  ASSERT_EQ(0, tgt_writer.StartEntry("b", 0));
  ASSERT_EQ(0, tgt_writer.WriteBytes(small_chunk.data(), small_chunk.size()));
  ASSERT_EQ(0, tgt_writer.FinishEntry());

  ASSERT_EQ(0, tgt_writer.Finish());
  ASSERT_EQ(0, fclose(tgt_file_ptr));

  TemporaryFile src_file;
  FILE* src_file_ptr = fdopen(src_file.release(), "wb");
  ZipWriter src_writer(src_file_ptr);
  construct_store_entry(
      {
          { "a", 11, 'a' }, { "b", 11, 'b' },
      },
      &src_writer);
  ASSERT_EQ(0, src_writer.Finish());
  ASSERT_EQ(0, fclose(src_file_ptr));

  // Compute patch.
  TemporaryFile patch_file;
  TemporaryFile split_info_file;
  TemporaryDir debug_dir;
  std::string split_info_arg = android::base::StringPrintf("--split-info=%s", split_info_file.path);
  std::string debug_dir_arg = android::base::StringPrintf("--debug-dir=%s", debug_dir.path);
  std::vector<const char*> args = {
    "imgdiff",     "-z",          "--block-limit=10", split_info_arg.c_str(), debug_dir_arg.c_str(),
    src_file.path, tgt_file.path, patch_file.path,
  };
  ASSERT_EQ(0, imgdiff(args.size(), args.data()));

  std::string tgt;
  ASSERT_TRUE(android::base::ReadFileToString(tgt_file.path, &tgt));

  // Expect two split src images:
  // src_piece 0: a-0 10 blocks
  // src_piece 1: a-0 1 block, CD
  GenerateAndCheckSplitTarget(debug_dir.path, 2, tgt);
}
