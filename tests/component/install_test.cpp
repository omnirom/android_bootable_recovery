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
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/properties.h>
#include <android-base/strings.h>
#include <android-base/test_utils.h>
#include <gtest/gtest.h>
#include <vintf/VintfObjectRecovery.h>
#include <ziparchive/zip_archive.h>
#include <ziparchive/zip_writer.h>

#include "install.h"
#include "private/install.h"

TEST(InstallTest, verify_package_compatibility_no_entry) {
  TemporaryFile temp_file;
  FILE* zip_file = fdopen(temp_file.release(), "w");
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
  FILE* zip_file = fdopen(temp_file.release(), "w");
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

TEST(InstallTest, read_metadata_from_package_smoke) {
  TemporaryFile temp_file;
  FILE* zip_file = fdopen(temp_file.release(), "w");
  ZipWriter writer(zip_file);
  ASSERT_EQ(0, writer.StartEntry("META-INF/com/android/metadata", kCompressStored));
  const std::string content("abcdefg");
  ASSERT_EQ(0, writer.WriteBytes(content.data(), content.size()));
  ASSERT_EQ(0, writer.FinishEntry());
  ASSERT_EQ(0, writer.Finish());
  ASSERT_EQ(0, fclose(zip_file));

  ZipArchiveHandle zip;
  ASSERT_EQ(0, OpenArchive(temp_file.path, &zip));
  std::string metadata;
  ASSERT_TRUE(read_metadata_from_package(zip, &metadata));
  ASSERT_EQ(content, metadata);
  CloseArchive(zip);

  TemporaryFile temp_file2;
  FILE* zip_file2 = fdopen(temp_file2.release(), "w");
  ZipWriter writer2(zip_file2);
  ASSERT_EQ(0, writer2.StartEntry("META-INF/com/android/metadata", kCompressDeflated));
  ASSERT_EQ(0, writer2.WriteBytes(content.data(), content.size()));
  ASSERT_EQ(0, writer2.FinishEntry());
  ASSERT_EQ(0, writer2.Finish());
  ASSERT_EQ(0, fclose(zip_file2));

  ASSERT_EQ(0, OpenArchive(temp_file2.path, &zip));
  metadata.clear();
  ASSERT_TRUE(read_metadata_from_package(zip, &metadata));
  ASSERT_EQ(content, metadata);
  CloseArchive(zip);
}

TEST(InstallTest, read_metadata_from_package_no_entry) {
  TemporaryFile temp_file;
  FILE* zip_file = fdopen(temp_file.release(), "w");
  ZipWriter writer(zip_file);
  ASSERT_EQ(0, writer.StartEntry("dummy_entry", kCompressStored));
  ASSERT_EQ(0, writer.FinishEntry());
  ASSERT_EQ(0, writer.Finish());
  ASSERT_EQ(0, fclose(zip_file));

  ZipArchiveHandle zip;
  ASSERT_EQ(0, OpenArchive(temp_file.path, &zip));
  std::string metadata;
  ASSERT_FALSE(read_metadata_from_package(zip, &metadata));
  CloseArchive(zip);
}

TEST(InstallTest, verify_package_compatibility_with_libvintf_malformed_xml) {
  TemporaryFile compatibility_zip_file;
  FILE* compatibility_zip = fdopen(compatibility_zip_file.release(), "w");
  ZipWriter compatibility_zip_writer(compatibility_zip);
  ASSERT_EQ(0, compatibility_zip_writer.StartEntry("system_manifest.xml", kCompressDeflated));
  std::string malformed_xml = "malformed";
  ASSERT_EQ(0, compatibility_zip_writer.WriteBytes(malformed_xml.data(), malformed_xml.size()));
  ASSERT_EQ(0, compatibility_zip_writer.FinishEntry());
  ASSERT_EQ(0, compatibility_zip_writer.Finish());
  ASSERT_EQ(0, fclose(compatibility_zip));

  TemporaryFile temp_file;
  FILE* zip_file = fdopen(temp_file.release(), "w");
  ZipWriter writer(zip_file);
  ASSERT_EQ(0, writer.StartEntry("compatibility.zip", kCompressStored));
  std::string compatibility_zip_content;
  ASSERT_TRUE(
      android::base::ReadFileToString(compatibility_zip_file.path, &compatibility_zip_content));
  ASSERT_EQ(0,
            writer.WriteBytes(compatibility_zip_content.data(), compatibility_zip_content.size()));
  ASSERT_EQ(0, writer.FinishEntry());
  ASSERT_EQ(0, writer.Finish());
  ASSERT_EQ(0, fclose(zip_file));

  ZipArchiveHandle zip;
  ASSERT_EQ(0, OpenArchive(temp_file.path, &zip));
  std::vector<std::string> compatibility_info;
  compatibility_info.push_back(malformed_xml);
  // Malformed compatibility zip is expected to be rejected by libvintf. But we defer that to
  // libvintf.
  std::string err;
  bool result =
      android::vintf::VintfObjectRecovery::CheckCompatibility(compatibility_info, &err) == 0;
  ASSERT_EQ(result, verify_package_compatibility(zip));
  CloseArchive(zip);
}

TEST(InstallTest, verify_package_compatibility_with_libvintf_system_manifest_xml) {
  static constexpr const char* system_manifest_xml_path = "/system/manifest.xml";
  if (access(system_manifest_xml_path, R_OK) == -1) {
    GTEST_LOG_(INFO) << "Test skipped on devices w/o /system/manifest.xml.";
    return;
  }
  std::string system_manifest_xml_content;
  ASSERT_TRUE(
      android::base::ReadFileToString(system_manifest_xml_path, &system_manifest_xml_content));
  TemporaryFile compatibility_zip_file;
  FILE* compatibility_zip = fdopen(compatibility_zip_file.release(), "w");
  ZipWriter compatibility_zip_writer(compatibility_zip);
  ASSERT_EQ(0, compatibility_zip_writer.StartEntry("system_manifest.xml", kCompressDeflated));
  ASSERT_EQ(0, compatibility_zip_writer.WriteBytes(system_manifest_xml_content.data(),
                                                   system_manifest_xml_content.size()));
  ASSERT_EQ(0, compatibility_zip_writer.FinishEntry());
  ASSERT_EQ(0, compatibility_zip_writer.Finish());
  ASSERT_EQ(0, fclose(compatibility_zip));

  TemporaryFile temp_file;
  FILE* zip_file = fdopen(temp_file.release(), "w");
  ZipWriter writer(zip_file);
  ASSERT_EQ(0, writer.StartEntry("compatibility.zip", kCompressStored));
  std::string compatibility_zip_content;
  ASSERT_TRUE(
      android::base::ReadFileToString(compatibility_zip_file.path, &compatibility_zip_content));
  ASSERT_EQ(0,
            writer.WriteBytes(compatibility_zip_content.data(), compatibility_zip_content.size()));
  ASSERT_EQ(0, writer.FinishEntry());
  ASSERT_EQ(0, writer.Finish());
  ASSERT_EQ(0, fclose(zip_file));

  ZipArchiveHandle zip;
  ASSERT_EQ(0, OpenArchive(temp_file.path, &zip));
  std::vector<std::string> compatibility_info;
  compatibility_info.push_back(system_manifest_xml_content);
  std::string err;
  bool result =
      android::vintf::VintfObjectRecovery::CheckCompatibility(compatibility_info, &err) == 0;
  // Make sure the result is consistent with libvintf library.
  ASSERT_EQ(result, verify_package_compatibility(zip));
  CloseArchive(zip);
}

#ifdef AB_OTA_UPDATER
static void VerifyAbUpdateBinaryCommand(const std::string& serialno, bool success = true) {
  TemporaryFile temp_file;
  FILE* zip_file = fdopen(temp_file.release(), "w");
  ZipWriter writer(zip_file);
  ASSERT_EQ(0, writer.StartEntry("payload.bin", kCompressStored));
  ASSERT_EQ(0, writer.FinishEntry());
  ASSERT_EQ(0, writer.StartEntry("payload_properties.txt", kCompressStored));
  const std::string properties = "some_properties";
  ASSERT_EQ(0, writer.WriteBytes(properties.data(), properties.size()));
  ASSERT_EQ(0, writer.FinishEntry());
  // A metadata entry is mandatory.
  ASSERT_EQ(0, writer.StartEntry("META-INF/com/android/metadata", kCompressStored));
  std::string device = android::base::GetProperty("ro.product.device", "");
  ASSERT_NE("", device);
  std::string timestamp = android::base::GetProperty("ro.build.date.utc", "");
  ASSERT_NE("", timestamp);

  std::vector<std::string> meta{ "ota-type=AB", "pre-device=" + device,
                                 "post-timestamp=" + timestamp };
  if (!serialno.empty()) {
    meta.push_back("serialno=" + serialno);
  }
  std::string metadata = android::base::Join(meta, "\n");
  ASSERT_EQ(0, writer.WriteBytes(metadata.data(), metadata.size()));
  ASSERT_EQ(0, writer.FinishEntry());
  ASSERT_EQ(0, writer.Finish());
  ASSERT_EQ(0, fclose(zip_file));

  ZipArchiveHandle zip;
  ASSERT_EQ(0, OpenArchive(temp_file.path, &zip));
  ZipString payload_name("payload.bin");
  ZipEntry payload_entry;
  ASSERT_EQ(0, FindEntry(zip, payload_name, &payload_entry));
  int status_fd = 10;
  std::string package = "/path/to/update.zip";
  std::string binary_path = "/sbin/update_engine_sideload";
  std::vector<std::string> cmd;
  if (success) {
    ASSERT_EQ(0, update_binary_command(package, zip, binary_path, 0, status_fd, &cmd));
    ASSERT_EQ(5U, cmd.size());
    ASSERT_EQ(binary_path, cmd[0]);
    ASSERT_EQ("--payload=file://" + package, cmd[1]);
    ASSERT_EQ("--offset=" + std::to_string(payload_entry.offset), cmd[2]);
    ASSERT_EQ("--headers=" + properties, cmd[3]);
    ASSERT_EQ("--status_fd=" + std::to_string(status_fd), cmd[4]);
  } else {
    ASSERT_EQ(INSTALL_ERROR, update_binary_command(package, zip, binary_path, 0, status_fd, &cmd));
  }
  CloseArchive(zip);
}
#endif  // AB_OTA_UPDATER

TEST(InstallTest, update_binary_command_smoke) {
#ifdef AB_OTA_UPDATER
  // Empty serialno will pass the verification.
  VerifyAbUpdateBinaryCommand({});
#else
  TemporaryFile temp_file;
  FILE* zip_file = fdopen(temp_file.release(), "w");
  ZipWriter writer(zip_file);
  static constexpr const char* UPDATE_BINARY_NAME = "META-INF/com/google/android/update-binary";
  ASSERT_EQ(0, writer.StartEntry(UPDATE_BINARY_NAME, kCompressStored));
  ASSERT_EQ(0, writer.FinishEntry());
  ASSERT_EQ(0, writer.Finish());
  ASSERT_EQ(0, fclose(zip_file));

  ZipArchiveHandle zip;
  ASSERT_EQ(0, OpenArchive(temp_file.path, &zip));
  int status_fd = 10;
  std::string package = "/path/to/update.zip";
  TemporaryDir td;
  std::string binary_path = std::string(td.path) + "/update_binary";
  std::vector<std::string> cmd;
  ASSERT_EQ(0, update_binary_command(package, zip, binary_path, 0, status_fd, &cmd));
  ASSERT_EQ(4U, cmd.size());
  ASSERT_EQ(binary_path, cmd[0]);
  ASSERT_EQ("3", cmd[1]);  // RECOVERY_API_VERSION
  ASSERT_EQ(std::to_string(status_fd), cmd[2]);
  ASSERT_EQ(package, cmd[3]);
  struct stat sb;
  ASSERT_EQ(0, stat(binary_path.c_str(), &sb));
  ASSERT_EQ(static_cast<mode_t>(0755), sb.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO));

  // With non-zero retry count. update_binary will be removed automatically.
  cmd.clear();
  ASSERT_EQ(0, update_binary_command(package, zip, binary_path, 2, status_fd, &cmd));
  ASSERT_EQ(5U, cmd.size());
  ASSERT_EQ(binary_path, cmd[0]);
  ASSERT_EQ("3", cmd[1]);  // RECOVERY_API_VERSION
  ASSERT_EQ(std::to_string(status_fd), cmd[2]);
  ASSERT_EQ(package, cmd[3]);
  ASSERT_EQ("retry", cmd[4]);
  sb = {};
  ASSERT_EQ(0, stat(binary_path.c_str(), &sb));
  ASSERT_EQ(static_cast<mode_t>(0755), sb.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO));

  CloseArchive(zip);
#endif  // AB_OTA_UPDATER
}

TEST(InstallTest, update_binary_command_invalid) {
#ifdef AB_OTA_UPDATER
  TemporaryFile temp_file;
  FILE* zip_file = fdopen(temp_file.release(), "w");
  ZipWriter writer(zip_file);
  // Missing payload_properties.txt.
  ASSERT_EQ(0, writer.StartEntry("payload.bin", kCompressStored));
  ASSERT_EQ(0, writer.FinishEntry());
  // A metadata entry is mandatory.
  ASSERT_EQ(0, writer.StartEntry("META-INF/com/android/metadata", kCompressStored));
  std::string device = android::base::GetProperty("ro.product.device", "");
  ASSERT_NE("", device);
  std::string timestamp = android::base::GetProperty("ro.build.date.utc", "");
  ASSERT_NE("", timestamp);
  std::string metadata = android::base::Join(
      std::vector<std::string>{
          "ota-type=AB", "pre-device=" + device, "post-timestamp=" + timestamp,
      },
      "\n");
  ASSERT_EQ(0, writer.WriteBytes(metadata.data(), metadata.size()));
  ASSERT_EQ(0, writer.FinishEntry());
  ASSERT_EQ(0, writer.Finish());
  ASSERT_EQ(0, fclose(zip_file));

  ZipArchiveHandle zip;
  ASSERT_EQ(0, OpenArchive(temp_file.path, &zip));
  int status_fd = 10;
  std::string package = "/path/to/update.zip";
  std::string binary_path = "/sbin/update_engine_sideload";
  std::vector<std::string> cmd;
  ASSERT_EQ(INSTALL_CORRUPT, update_binary_command(package, zip, binary_path, 0, status_fd, &cmd));
  CloseArchive(zip);
#else
  TemporaryFile temp_file;
  FILE* zip_file = fdopen(temp_file.release(), "w");
  ZipWriter writer(zip_file);
  // The archive must have something to be opened correctly.
  ASSERT_EQ(0, writer.StartEntry("dummy_entry", 0));
  ASSERT_EQ(0, writer.FinishEntry());
  ASSERT_EQ(0, writer.Finish());
  ASSERT_EQ(0, fclose(zip_file));

  // Missing update binary.
  ZipArchiveHandle zip;
  ASSERT_EQ(0, OpenArchive(temp_file.path, &zip));
  int status_fd = 10;
  std::string package = "/path/to/update.zip";
  TemporaryDir td;
  std::string binary_path = std::string(td.path) + "/update_binary";
  std::vector<std::string> cmd;
  ASSERT_EQ(INSTALL_CORRUPT, update_binary_command(package, zip, binary_path, 0, status_fd, &cmd));
  CloseArchive(zip);
#endif  // AB_OTA_UPDATER
}

#ifdef AB_OTA_UPDATER
TEST(InstallTest, update_binary_command_multiple_serialno) {
  std::string serialno = android::base::GetProperty("ro.serialno", "");
  ASSERT_NE("", serialno);

  // Single matching serialno will pass the verification.
  VerifyAbUpdateBinaryCommand(serialno);

  static constexpr char alphabet[] =
      "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  auto generator = []() { return alphabet[rand() % (sizeof(alphabet) - 1)]; };

  // Generate 900 random serial numbers.
  std::string random_serial;
  for (size_t i = 0; i < 900; i++) {
    generate_n(back_inserter(random_serial), serialno.size(), generator);
    random_serial.append("|");
  }
  // Random serialnos should fail the verification.
  VerifyAbUpdateBinaryCommand(random_serial, false);

  std::string long_serial = random_serial + serialno + "|";
  for (size_t i = 0; i < 99; i++) {
    generate_n(back_inserter(long_serial), serialno.size(), generator);
    long_serial.append("|");
  }
  // String with the matching serialno should pass the verification.
  VerifyAbUpdateBinaryCommand(long_serial);
}
#endif  // AB_OTA_UPDATER
