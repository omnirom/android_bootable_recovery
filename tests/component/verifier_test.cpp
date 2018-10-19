/*
 * Copyright (C) 2009 The Android Open Source Project
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

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#include <android-base/test_utils.h>
#include <android-base/unique_fd.h>
#include <gtest/gtest.h>
#include <ziparchive/zip_writer.h>

#include "common/test_constants.h"
#include "otautil/sysutil.h"
#include "verifier.h"

using namespace std::string_literals;

static void LoadKeyFromFile(const std::string& file_name, Certificate* cert) {
  std::string testkey_string;
  ASSERT_TRUE(android::base::ReadFileToString(file_name, &testkey_string));
  ASSERT_TRUE(LoadCertificateFromBuffer(
      std::vector<uint8_t>(testkey_string.begin(), testkey_string.end()), cert));
}

static void VerifyPackageWithCertificates(const std::string& name,
                                          const std::vector<Certificate>& certs) {
  std::string package = from_testdata_base(name);
  MemMapping memmap;
  if (!memmap.MapFile(package)) {
    FAIL() << "Failed to mmap " << package << ": " << strerror(errno) << "\n";
  }

  ASSERT_EQ(VERIFY_SUCCESS, verify_file(memmap.addr, memmap.length, certs));
}

static void VerifyPackageWithSingleCertificate(const std::string& name, Certificate&& cert) {
  std::vector<Certificate> certs;
  certs.emplace_back(std::move(cert));
  VerifyPackageWithCertificates(name, certs);
}

static void BuildCertificateArchive(const std::vector<std::string>& file_names, int fd) {
  FILE* zip_file_ptr = fdopen(fd, "wb");
  ZipWriter zip_writer(zip_file_ptr);

  for (const auto& name : file_names) {
    std::string content;
    ASSERT_TRUE(android::base::ReadFileToString(name, &content));

    // Makes sure the zip entry name has the correct suffix.
    std::string entry_name = name;
    if (!android::base::EndsWith(entry_name, "x509.pem")) {
      entry_name += "x509.pem";
    }
    ASSERT_EQ(0, zip_writer.StartEntry(entry_name.c_str(), ZipWriter::kCompress));
    ASSERT_EQ(0, zip_writer.WriteBytes(content.data(), content.size()));
    ASSERT_EQ(0, zip_writer.FinishEntry());
  }

  ASSERT_EQ(0, zip_writer.Finish());
  ASSERT_EQ(0, fclose(zip_file_ptr));
}

TEST(VerifierTest, LoadCertificateFromBuffer_failure) {
  Certificate cert(0, Certificate::KEY_TYPE_RSA, nullptr, nullptr);
  std::string testkey_string;
  ASSERT_TRUE(
      android::base::ReadFileToString(from_testdata_base("testkey_v1.txt"), &testkey_string));
  ASSERT_FALSE(LoadCertificateFromBuffer(
      std::vector<uint8_t>(testkey_string.begin(), testkey_string.end()), &cert));
}

TEST(VerifierTest, LoadCertificateFromBuffer_sha1_exponent3) {
  Certificate cert(0, Certificate::KEY_TYPE_RSA, nullptr, nullptr);
  LoadKeyFromFile(from_testdata_base("testkey_v1.x509.pem"), &cert);

  ASSERT_EQ(SHA_DIGEST_LENGTH, cert.hash_len);
  ASSERT_EQ(Certificate::KEY_TYPE_RSA, cert.key_type);
  ASSERT_EQ(nullptr, cert.ec);

  VerifyPackageWithSingleCertificate("otasigned_v1.zip", std::move(cert));
}

TEST(VerifierTest, LoadCertificateFromBuffer_sha1_exponent65537) {
  Certificate cert(0, Certificate::KEY_TYPE_RSA, nullptr, nullptr);
  LoadKeyFromFile(from_testdata_base("testkey_v2.x509.pem"), &cert);

  ASSERT_EQ(SHA_DIGEST_LENGTH, cert.hash_len);
  ASSERT_EQ(Certificate::KEY_TYPE_RSA, cert.key_type);
  ASSERT_EQ(nullptr, cert.ec);

  VerifyPackageWithSingleCertificate("otasigned_v2.zip", std::move(cert));
}

TEST(VerifierTest, LoadCertificateFromBuffer_sha256_exponent3) {
  Certificate cert(0, Certificate::KEY_TYPE_RSA, nullptr, nullptr);
  LoadKeyFromFile(from_testdata_base("testkey_v3.x509.pem"), &cert);

  ASSERT_EQ(SHA256_DIGEST_LENGTH, cert.hash_len);
  ASSERT_EQ(Certificate::KEY_TYPE_RSA, cert.key_type);
  ASSERT_EQ(nullptr, cert.ec);

  VerifyPackageWithSingleCertificate("otasigned_v3.zip", std::move(cert));
}

TEST(VerifierTest, LoadCertificateFromBuffer_sha256_exponent65537) {
  Certificate cert(0, Certificate::KEY_TYPE_RSA, nullptr, nullptr);
  LoadKeyFromFile(from_testdata_base("testkey_v4.x509.pem"), &cert);

  ASSERT_EQ(SHA256_DIGEST_LENGTH, cert.hash_len);
  ASSERT_EQ(Certificate::KEY_TYPE_RSA, cert.key_type);
  ASSERT_EQ(nullptr, cert.ec);

  VerifyPackageWithSingleCertificate("otasigned_v4.zip", std::move(cert));
}

TEST(VerifierTest, LoadCertificateFromBuffer_sha256_ec256bits) {
  Certificate cert(0, Certificate::KEY_TYPE_RSA, nullptr, nullptr);
  LoadKeyFromFile(from_testdata_base("testkey_v5.x509.pem"), &cert);

  ASSERT_EQ(SHA256_DIGEST_LENGTH, cert.hash_len);
  ASSERT_EQ(Certificate::KEY_TYPE_EC, cert.key_type);
  ASSERT_EQ(nullptr, cert.rsa);

  VerifyPackageWithSingleCertificate("otasigned_v5.zip", std::move(cert));
}

TEST(VerifierTest, LoadKeysFromZipfile_empty_archive) {
  TemporaryFile otacerts;
  BuildCertificateArchive({}, otacerts.release());
  std::vector<Certificate> certs = LoadKeysFromZipfile(otacerts.path);
  ASSERT_TRUE(certs.empty());
}

TEST(VerifierTest, LoadKeysFromZipfile_single_key) {
  TemporaryFile otacerts;
  BuildCertificateArchive({ from_testdata_base("testkey_v1.x509.pem") }, otacerts.release());
  std::vector<Certificate> certs = LoadKeysFromZipfile(otacerts.path);
  ASSERT_EQ(1, certs.size());

  VerifyPackageWithCertificates("otasigned_v1.zip", certs);
}

TEST(VerifierTest, LoadKeysFromZipfile_corrupted_key) {
  TemporaryFile corrupted_key;
  std::string content;
  ASSERT_TRUE(android::base::ReadFileToString(from_testdata_base("testkey_v1.x509.pem"), &content));
  content = "random-contents" + content;
  ASSERT_TRUE(android::base::WriteStringToFd(content, corrupted_key.release()));

  TemporaryFile otacerts;
  BuildCertificateArchive({ from_testdata_base("testkey_v2.x509.pem"), corrupted_key.path },
                          otacerts.release());
  std::vector<Certificate> certs = LoadKeysFromZipfile(otacerts.path);
  ASSERT_EQ(0, certs.size());
}

TEST(VerifierTest, LoadKeysFromZipfile_multiple_key) {
  TemporaryFile otacerts;
  BuildCertificateArchive(
      {
          from_testdata_base("testkey_v3.x509.pem"),
          from_testdata_base("testkey_v4.x509.pem"),
          from_testdata_base("testkey_v5.x509.pem"),

      },
      otacerts.release());
  std::vector<Certificate> certs = LoadKeysFromZipfile(otacerts.path);
  ASSERT_EQ(3, certs.size());

  VerifyPackageWithCertificates("otasigned_v3.zip", certs);
  VerifyPackageWithCertificates("otasigned_v4.zip", certs);
  VerifyPackageWithCertificates("otasigned_v5.zip", certs);
}

class VerifierTest : public testing::TestWithParam<std::vector<std::string>> {
 protected:
  void SetUp() override {
    std::vector<std::string> args = GetParam();
    std::string package = from_testdata_base(args[0]);
    if (!memmap.MapFile(package)) {
      FAIL() << "Failed to mmap " << package << ": " << strerror(errno) << "\n";
    }

    for (auto it = ++args.cbegin(); it != args.cend(); ++it) {
      std::string public_key_file = from_testdata_base("testkey_" + *it + ".txt");
      ASSERT_TRUE(load_keys(public_key_file.c_str(), certs));
    }
  }

  MemMapping memmap;
  std::vector<Certificate> certs;
};

class VerifierSuccessTest : public VerifierTest {
};

class VerifierFailureTest : public VerifierTest {
};

TEST(VerifierTest, load_keys_multiple_keys) {
  std::string testkey_v4;
  ASSERT_TRUE(android::base::ReadFileToString(from_testdata_base("testkey_v4.txt"), &testkey_v4));

  std::string testkey_v3;
  ASSERT_TRUE(android::base::ReadFileToString(from_testdata_base("testkey_v3.txt"), &testkey_v3));

  std::string keys = testkey_v4 + "," + testkey_v3 + "," + testkey_v4;
  TemporaryFile key_file1;
  ASSERT_TRUE(android::base::WriteStringToFile(keys, key_file1.path));
  std::vector<Certificate> certs;
  ASSERT_TRUE(load_keys(key_file1.path, certs));
  ASSERT_EQ(3U, certs.size());
}

TEST(VerifierTest, load_keys_invalid_keys) {
  std::vector<Certificate> certs;
  ASSERT_FALSE(load_keys("/doesntexist", certs));

  // Empty file.
  TemporaryFile key_file1;
  ASSERT_FALSE(load_keys(key_file1.path, certs));

  // Invalid contents.
  ASSERT_TRUE(android::base::WriteStringToFile("invalid", key_file1.path));
  ASSERT_FALSE(load_keys(key_file1.path, certs));

  std::string testkey_v4;
  ASSERT_TRUE(android::base::ReadFileToString(from_testdata_base("testkey_v4.txt"), &testkey_v4));

  // Invalid key version: "v4 ..." => "v6 ...".
  std::string invalid_key2(testkey_v4);
  invalid_key2[1] = '6';
  TemporaryFile key_file2;
  ASSERT_TRUE(android::base::WriteStringToFile(invalid_key2, key_file2.path));
  ASSERT_FALSE(load_keys(key_file2.path, certs));

  // Invalid key content: inserted extra bytes ",2209831334".
  std::string invalid_key3(testkey_v4);
  invalid_key3.insert(invalid_key2.size() - 2, ",2209831334");
  TemporaryFile key_file3;
  ASSERT_TRUE(android::base::WriteStringToFile(invalid_key3, key_file3.path));
  ASSERT_FALSE(load_keys(key_file3.path, certs));

  // Invalid key: the last key must not end with an extra ','.
  std::string invalid_key4 = testkey_v4 + ",";
  TemporaryFile key_file4;
  ASSERT_TRUE(android::base::WriteStringToFile(invalid_key4, key_file4.path));
  ASSERT_FALSE(load_keys(key_file4.path, certs));

  // Invalid key separator.
  std::string invalid_key5 = testkey_v4 + ";" + testkey_v4;
  TemporaryFile key_file5;
  ASSERT_TRUE(android::base::WriteStringToFile(invalid_key5, key_file5.path));
  ASSERT_FALSE(load_keys(key_file5.path, certs));
}

TEST(VerifierTest, BadPackage_AlteredFooter) {
  std::string testkey_v3;
  ASSERT_TRUE(android::base::ReadFileToString(from_testdata_base("testkey_v3.txt"), &testkey_v3));
  TemporaryFile key_file1;
  ASSERT_TRUE(android::base::WriteStringToFile(testkey_v3, key_file1.path));
  std::vector<Certificate> certs;
  ASSERT_TRUE(load_keys(key_file1.path, certs));

  std::string package;
  ASSERT_TRUE(android::base::ReadFileToString(from_testdata_base("otasigned_v3.zip"), &package));
  ASSERT_EQ(std::string("\xc0\x06\xff\xff\xd2\x06", 6), package.substr(package.size() - 6, 6));

  // Alter the footer.
  package[package.size() - 5] = '\x05';
  ASSERT_EQ(VERIFY_FAILURE,
            verify_file(reinterpret_cast<const unsigned char*>(package.data()), package.size(),
                        certs));
}

TEST(VerifierTest, BadPackage_AlteredContent) {
  std::string testkey_v3;
  ASSERT_TRUE(android::base::ReadFileToString(from_testdata_base("testkey_v3.txt"), &testkey_v3));
  TemporaryFile key_file1;
  ASSERT_TRUE(android::base::WriteStringToFile(testkey_v3, key_file1.path));
  std::vector<Certificate> certs;
  ASSERT_TRUE(load_keys(key_file1.path, certs));

  std::string package;
  ASSERT_TRUE(android::base::ReadFileToString(from_testdata_base("otasigned_v3.zip"), &package));
  ASSERT_GT(package.size(), static_cast<size_t>(100));

  // Alter the content.
  std::string altered1(package);
  altered1[50] += 1;
  ASSERT_EQ(VERIFY_FAILURE,
            verify_file(reinterpret_cast<const unsigned char*>(altered1.data()), altered1.size(),
                        certs));

  std::string altered2(package);
  altered2[10] += 1;
  ASSERT_EQ(VERIFY_FAILURE,
            verify_file(reinterpret_cast<const unsigned char*>(altered2.data()), altered2.size(),
                        certs));
}

TEST(VerifierTest, BadPackage_SignatureStartOutOfBounds) {
  std::string testkey_v3;
  ASSERT_TRUE(android::base::ReadFileToString(from_testdata_base("testkey_v3.txt"), &testkey_v3));

  TemporaryFile key_file;
  ASSERT_TRUE(android::base::WriteStringToFile(testkey_v3, key_file.path));
  std::vector<Certificate> certs;
  ASSERT_TRUE(load_keys(key_file.path, certs));

  // Signature start is 65535 (0xffff) while comment size is 0 (Bug: 31914369).
  std::string package = "\x50\x4b\x05\x06"s + std::string(12, '\0') + "\xff\xff\xff\xff\x00\x00"s;
  ASSERT_EQ(VERIFY_FAILURE, verify_file(reinterpret_cast<const unsigned char*>(package.data()),
                                        package.size(), certs));
}

TEST_P(VerifierSuccessTest, VerifySucceed) {
  ASSERT_EQ(verify_file(memmap.addr, memmap.length, certs, nullptr), VERIFY_SUCCESS);
}

TEST_P(VerifierFailureTest, VerifyFailure) {
  ASSERT_EQ(verify_file(memmap.addr, memmap.length, certs, nullptr), VERIFY_FAILURE);
}

INSTANTIATE_TEST_CASE_P(SingleKeySuccess, VerifierSuccessTest,
    ::testing::Values(
      std::vector<std::string>({"otasigned_v1.zip", "v1"}),
      std::vector<std::string>({"otasigned_v2.zip", "v2"}),
      std::vector<std::string>({"otasigned_v3.zip", "v3"}),
      std::vector<std::string>({"otasigned_v4.zip", "v4"}),
      std::vector<std::string>({"otasigned_v5.zip", "v5"})));

INSTANTIATE_TEST_CASE_P(MultiKeySuccess, VerifierSuccessTest,
    ::testing::Values(
      std::vector<std::string>({"otasigned_v1.zip", "v1", "v2"}),
      std::vector<std::string>({"otasigned_v2.zip", "v5", "v2"}),
      std::vector<std::string>({"otasigned_v3.zip", "v5", "v1", "v3"}),
      std::vector<std::string>({"otasigned_v4.zip", "v5", "v1", "v4"}),
      std::vector<std::string>({"otasigned_v5.zip", "v4", "v1", "v5"})));

INSTANTIATE_TEST_CASE_P(WrongKey, VerifierFailureTest,
    ::testing::Values(
      std::vector<std::string>({"otasigned_v1.zip", "v2"}),
      std::vector<std::string>({"otasigned_v2.zip", "v1"}),
      std::vector<std::string>({"otasigned_v3.zip", "v5"}),
      std::vector<std::string>({"otasigned_v4.zip", "v5"}),
      std::vector<std::string>({"otasigned_v5.zip", "v3"})));

INSTANTIATE_TEST_CASE_P(WrongHash, VerifierFailureTest,
    ::testing::Values(
      std::vector<std::string>({"otasigned_v1.zip", "v3"}),
      std::vector<std::string>({"otasigned_v2.zip", "v4"}),
      std::vector<std::string>({"otasigned_v3.zip", "v1"}),
      std::vector<std::string>({"otasigned_v4.zip", "v2"})));

INSTANTIATE_TEST_CASE_P(BadPackage, VerifierFailureTest,
    ::testing::Values(
      std::vector<std::string>({"random.zip", "v1"}),
      std::vector<std::string>({"fake-eocd.zip", "v1"})));
