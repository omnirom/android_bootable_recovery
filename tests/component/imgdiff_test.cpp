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

#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/memory.h>
#include <android-base/test_utils.h>
#include <applypatch/imgdiff.h>
#include <applypatch/imgpatch.h>
#include <gtest/gtest.h>
#include <ziparchive/zip_writer.h>

using android::base::get_unaligned;

static ssize_t MemorySink(const unsigned char* data, ssize_t len, void* token) {
  std::string* s = static_cast<std::string*>(token);
  s->append(reinterpret_cast<const char*>(data), len);
  return len;
}

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

  std::string patched;
  ASSERT_EQ(0, ApplyImagePatch(reinterpret_cast<const unsigned char*>(src.data()), src.size(),
                               reinterpret_cast<const unsigned char*>(patch.data()), patch.size(),
                               MemorySink, &patched));
  ASSERT_EQ(tgt, patched);
}

TEST(ImgdiffTest, zip_mode_smoke_store) {
  // Construct src and tgt zip files.
  TemporaryFile src_file;
  FILE* src_file_ptr = fdopen(src_file.fd, "wb");
  ZipWriter src_writer(src_file_ptr);
  ASSERT_EQ(0, src_writer.StartEntry("file1.txt", 0));  // Store mode.
  const std::string src_content("abcdefg");
  ASSERT_EQ(0, src_writer.WriteBytes(src_content.data(), src_content.size()));
  ASSERT_EQ(0, src_writer.FinishEntry());
  ASSERT_EQ(0, src_writer.Finish());
  ASSERT_EQ(0, fclose(src_file_ptr));

  TemporaryFile tgt_file;
  FILE* tgt_file_ptr = fdopen(tgt_file.fd, "wb");
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

  std::string patched;
  ASSERT_EQ(0, ApplyImagePatch(reinterpret_cast<const unsigned char*>(src.data()), src.size(),
                               reinterpret_cast<const unsigned char*>(patch.data()), patch.size(),
                               MemorySink, &patched));
  ASSERT_EQ(tgt, patched);
}

TEST(ImgdiffTest, zip_mode_smoke_compressed) {
  // Construct src and tgt zip files.
  TemporaryFile src_file;
  FILE* src_file_ptr = fdopen(src_file.fd, "wb");
  ZipWriter src_writer(src_file_ptr);
  ASSERT_EQ(0, src_writer.StartEntry("file1.txt", ZipWriter::kCompress));
  const std::string src_content("abcdefg");
  ASSERT_EQ(0, src_writer.WriteBytes(src_content.data(), src_content.size()));
  ASSERT_EQ(0, src_writer.FinishEntry());
  ASSERT_EQ(0, src_writer.Finish());
  ASSERT_EQ(0, fclose(src_file_ptr));

  TemporaryFile tgt_file;
  FILE* tgt_file_ptr = fdopen(tgt_file.fd, "wb");
  ZipWriter tgt_writer(tgt_file_ptr);
  ASSERT_EQ(0, tgt_writer.StartEntry("file1.txt", ZipWriter::kCompress));
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

  // Expect three entries: CHUNK_RAW (header) + CHUNK_DEFLATE (data) + CHUNK_RAW (footer).
  size_t num_normal;
  size_t num_raw;
  size_t num_deflate;
  verify_patch_header(patch, &num_normal, &num_raw, &num_deflate);
  ASSERT_EQ(0U, num_normal);
  ASSERT_EQ(1U, num_deflate);
  ASSERT_EQ(2U, num_raw);

  std::string patched;
  ASSERT_EQ(0, ApplyImagePatch(reinterpret_cast<const unsigned char*>(src.data()), src.size(),
                               reinterpret_cast<const unsigned char*>(patch.data()), patch.size(),
                               MemorySink, &patched));
  ASSERT_EQ(tgt, patched);
}

TEST(ImgdiffTest, zip_mode_smoke_trailer_zeros) {
  // Construct src and tgt zip files.
  TemporaryFile src_file;
  FILE* src_file_ptr = fdopen(src_file.fd, "wb");
  ZipWriter src_writer(src_file_ptr);
  ASSERT_EQ(0, src_writer.StartEntry("file1.txt", ZipWriter::kCompress));
  const std::string src_content("abcdefg");
  ASSERT_EQ(0, src_writer.WriteBytes(src_content.data(), src_content.size()));
  ASSERT_EQ(0, src_writer.FinishEntry());
  ASSERT_EQ(0, src_writer.Finish());
  ASSERT_EQ(0, fclose(src_file_ptr));

  TemporaryFile tgt_file;
  FILE* tgt_file_ptr = fdopen(tgt_file.fd, "wb");
  ZipWriter tgt_writer(tgt_file_ptr);
  ASSERT_EQ(0, tgt_writer.StartEntry("file1.txt", ZipWriter::kCompress));
  const std::string tgt_content("abcdefgxyz");
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

  std::string patched;
  ASSERT_EQ(0, ApplyImagePatch(reinterpret_cast<const unsigned char*>(src.data()), src.size(),
                               reinterpret_cast<const unsigned char*>(patch.data()), patch.size(),
                               MemorySink, &patched));
  ASSERT_EQ(tgt, patched);
}

TEST(ImgdiffTest, image_mode_simple) {
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

  // Expect three entries: CHUNK_RAW (header) + CHUNK_DEFLATE (data) + CHUNK_RAW (footer).
  size_t num_normal;
  size_t num_raw;
  size_t num_deflate;
  verify_patch_header(patch, &num_normal, &num_raw, &num_deflate);
  ASSERT_EQ(0U, num_normal);
  ASSERT_EQ(1U, num_deflate);
  ASSERT_EQ(2U, num_raw);

  std::string patched;
  ASSERT_EQ(0, ApplyImagePatch(reinterpret_cast<const unsigned char*>(src.data()), src.size(),
                               reinterpret_cast<const unsigned char*>(patch.data()), patch.size(),
                               MemorySink, &patched));
  ASSERT_EQ(tgt, patched);
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
  // src: "abcdefgh" + gzipped "xyz" (echo -n "xyz" | gzip -f | hd).
  const std::vector<char> src_data = { 'a',    'b',    'c',    'd',    'e',    'f',    'g',
                                       'h',    '\x1f', '\x8b', '\x08', '\x00', '\xc4', '\x1e',
                                       '\x53', '\x58', '\x00', '\x03', '\xab', '\xa8', '\xac',
                                       '\x02', '\x00', '\x67', '\xba', '\x8e', '\xeb', '\x03',
                                       '\x00', '\x00', '\x00' };
  const std::string src(src_data.cbegin(), src_data.cend());
  TemporaryFile src_file;
  ASSERT_TRUE(android::base::WriteStringToFile(src, src_file.path));

  // tgt: gzipped "xyz" + "abcdefgh".
  const std::vector<char> tgt_data = {
    '\x1f', '\x8b', '\x08', '\x00', '\x62', '\x1f', '\x53', '\x58', '\x00', '\x03', '\xab', '\xa8',
    '\xa8', '\xac', '\xac', '\xaa', '\x02', '\x00', '\x96', '\x30', '\x06', '\xb7', '\x06', '\x00',
    '\x00', '\x00', 'a',    'b',    'c',    'd',    'e',    'f',    'g',    'x',    'y',    'z'
  };
  const std::string tgt(tgt_data.cbegin(), tgt_data.cend());
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

  std::string patched;
  ASSERT_EQ(0, ApplyImagePatch(reinterpret_cast<const unsigned char*>(src.data()), src.size(),
                               reinterpret_cast<const unsigned char*>(patch.data()), patch.size(),
                               MemorySink, &patched));
  ASSERT_EQ(tgt, patched);
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

  std::string patched;
  ASSERT_EQ(0, ApplyImagePatch(reinterpret_cast<const unsigned char*>(src.data()), src.size(),
                               reinterpret_cast<const unsigned char*>(patch.data()), patch.size(),
                               MemorySink, &patched));
  ASSERT_EQ(tgt, patched);
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

  std::string patched;
  ASSERT_EQ(0, ApplyImagePatch(reinterpret_cast<const unsigned char*>(src.data()), src.size(),
                               reinterpret_cast<const unsigned char*>(patch.data()), patch.size(),
                               MemorySink, &patched));
  ASSERT_EQ(tgt, patched);
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

  std::string patched;
  ASSERT_EQ(0, ApplyImagePatch(reinterpret_cast<const unsigned char*>(src.data()), src.size(),
                               reinterpret_cast<const unsigned char*>(patch.data()), patch.size(),
                               MemorySink, &patched));
  ASSERT_EQ(tgt, patched);
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

  std::string patched;
  ASSERT_EQ(0, ApplyImagePatch(reinterpret_cast<const unsigned char*>(src.data()), src.size(),
                               reinterpret_cast<const unsigned char*>(patch.data()), patch.size(),
                               MemorySink, &patched));
  ASSERT_EQ(tgt, patched);
}
