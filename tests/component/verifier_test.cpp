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
#include <android-base/unique_fd.h>
#include <gtest/gtest.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/nid.h>
#include <ziparchive/zip_writer.h>

#include "common/test_constants.h"
#include "install/package.h"
#include "install/verifier.h"
#include "otautil/sysutil.h"

using namespace std::string_literals;

static void LoadKeyFromFile(const std::string& file_name, Certificate* cert) {
  std::string testkey_string;
  ASSERT_TRUE(android::base::ReadFileToString(file_name, &testkey_string));
  ASSERT_TRUE(LoadCertificateFromBuffer(
      std::vector<uint8_t>(testkey_string.begin(), testkey_string.end()), cert));
}

static void VerifyFile(const std::string& content, const std::vector<Certificate>& keys,
                       int expected) {
  auto package =
      Package::CreateMemoryPackage(std::vector<uint8_t>(content.begin(), content.end()), nullptr);
  ASSERT_NE(nullptr, package);

  ASSERT_EQ(expected, verify_file(package.get(), keys));
}

static void VerifyPackageWithCertificates(const std::string& name,
                                          const std::vector<Certificate>& certs) {
  std::string path = from_testdata_base(name);
  auto package = Package::CreateMemoryPackage(path, nullptr);
  ASSERT_NE(nullptr, package);

  ASSERT_EQ(VERIFY_SUCCESS, verify_file(package.get(), certs));
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

TEST(VerifierTest, LoadCertificateFromBuffer_sha256_rsa4096_bits) {
  Certificate cert(0, Certificate::KEY_TYPE_RSA, nullptr, nullptr);
  LoadKeyFromFile(from_testdata_base("testkey_4096bits.x509.pem"), &cert);

  ASSERT_EQ(SHA256_DIGEST_LENGTH, cert.hash_len);
  ASSERT_EQ(Certificate::KEY_TYPE_RSA, cert.key_type);
  ASSERT_EQ(nullptr, cert.ec);

  VerifyPackageWithSingleCertificate("otasigned_4096bits.zip", std::move(cert));
}

TEST(VerifierTest, LoadCertificateFromBuffer_check_rsa_keys) {
  std::unique_ptr<RSA, RSADeleter> rsa(RSA_new());
  std::unique_ptr<BIGNUM, decltype(&BN_free)> exponent(BN_new(), BN_free);
  BN_set_word(exponent.get(), 3);
  RSA_generate_key_ex(rsa.get(), 2048, exponent.get(), nullptr);
  ASSERT_TRUE(CheckRSAKey(rsa));

  // Exponent is expected to be 3 or 65537
  BN_set_word(exponent.get(), 17);
  RSA_generate_key_ex(rsa.get(), 2048, exponent.get(), nullptr);
  ASSERT_FALSE(CheckRSAKey(rsa));

  // Modulus is expected to be 2048.
  BN_set_word(exponent.get(), 3);
  RSA_generate_key_ex(rsa.get(), 1024, exponent.get(), nullptr);
  ASSERT_FALSE(CheckRSAKey(rsa));
}

TEST(VerifierTest, LoadCertificateFromBuffer_check_ec_keys) {
  std::unique_ptr<EC_KEY, ECKEYDeleter> ec(EC_KEY_new_by_curve_name(NID_X9_62_prime256v1));
  ASSERT_EQ(1, EC_KEY_generate_key(ec.get()));
  ASSERT_TRUE(CheckECKey(ec));

  // Expects 256-bit EC key with curve NIST P-256
  ec.reset(EC_KEY_new_by_curve_name(NID_secp224r1));
  ASSERT_EQ(1, EC_KEY_generate_key(ec.get()));
  ASSERT_FALSE(CheckECKey(ec));
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
    std::string path = from_testdata_base(args[0]);
    memory_package_ = Package::CreateMemoryPackage(path, nullptr);
    ASSERT_NE(nullptr, memory_package_);
    file_package_ = Package::CreateFilePackage(path, nullptr);
    ASSERT_NE(nullptr, file_package_);

    for (auto it = ++args.cbegin(); it != args.cend(); ++it) {
      std::string public_key_file = from_testdata_base("testkey_" + *it + ".x509.pem");
      certs_.emplace_back(0, Certificate::KEY_TYPE_RSA, nullptr, nullptr);
      LoadKeyFromFile(public_key_file, &certs_.back());
    }
  }

  std::unique_ptr<Package> memory_package_;
  std::unique_ptr<Package> file_package_;
  std::vector<Certificate> certs_;
};

class VerifierSuccessTest : public VerifierTest {
};

class VerifierFailureTest : public VerifierTest {
};

TEST(VerifierTest, BadPackage_AlteredFooter) {
  std::vector<Certificate> certs;
  certs.emplace_back(0, Certificate::KEY_TYPE_RSA, nullptr, nullptr);
  LoadKeyFromFile(from_testdata_base("testkey_v3.x509.pem"), &certs.back());

  std::string package;
  ASSERT_TRUE(android::base::ReadFileToString(from_testdata_base("otasigned_v3.zip"), &package));
  ASSERT_EQ(std::string("\xc0\x06\xff\xff\xd2\x06", 6), package.substr(package.size() - 6, 6));

  // Alter the footer.
  package[package.size() - 5] = '\x05';
  VerifyFile(package, certs, VERIFY_FAILURE);
}

TEST(VerifierTest, BadPackage_AlteredContent) {
  std::vector<Certificate> certs;
  certs.emplace_back(0, Certificate::KEY_TYPE_RSA, nullptr, nullptr);
  LoadKeyFromFile(from_testdata_base("testkey_v3.x509.pem"), &certs.back());

  std::string package;
  ASSERT_TRUE(android::base::ReadFileToString(from_testdata_base("otasigned_v3.zip"), &package));
  ASSERT_GT(package.size(), static_cast<size_t>(100));

  // Alter the content.
  std::string altered1(package);
  altered1[50] += 1;
  VerifyFile(altered1, certs, VERIFY_FAILURE);

  std::string altered2(package);
  altered2[10] += 1;
  VerifyFile(altered2, certs, VERIFY_FAILURE);
}

TEST(VerifierTest, BadPackage_SignatureStartOutOfBounds) {
  std::vector<Certificate> certs;
  certs.emplace_back(0, Certificate::KEY_TYPE_RSA, nullptr, nullptr);
  LoadKeyFromFile(from_testdata_base("testkey_v3.x509.pem"), &certs.back());

  // Signature start is 65535 (0xffff) while comment size is 0 (Bug: 31914369).
  std::string package = "\x50\x4b\x05\x06"s + std::string(12, '\0') + "\xff\xff\xff\xff\x00\x00"s;
  VerifyFile(package, certs, VERIFY_FAILURE);
}

TEST_P(VerifierSuccessTest, VerifySucceed) {
  ASSERT_EQ(VERIFY_SUCCESS, verify_file(memory_package_.get(), certs_));
  ASSERT_EQ(VERIFY_SUCCESS, verify_file(file_package_.get(), certs_));
}

TEST_P(VerifierFailureTest, VerifyFailure) {
  ASSERT_EQ(VERIFY_FAILURE, verify_file(memory_package_.get(), certs_));
  ASSERT_EQ(VERIFY_FAILURE, verify_file(file_package_.get(), certs_));
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
