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
#include <random>
#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/properties.h>
#include <android-base/strings.h>
#include <gtest/gtest.h>
#include <vintf/VintfObjectRecovery.h>
#include <ziparchive/zip_archive.h>
#include <ziparchive/zip_writer.h>

#include "install/install.h"
#include "otautil/paths.h"
#include "private/setup_commands.h"

static void BuildZipArchive(const std::map<std::string, std::string>& file_map, int fd,
                            int compression_type) {
  FILE* zip_file = fdopen(fd, "w");
  ZipWriter writer(zip_file);
  for (const auto& [name, content] : file_map) {
    ASSERT_EQ(0, writer.StartEntry(name.c_str(), compression_type));
    ASSERT_EQ(0, writer.WriteBytes(content.data(), content.size()));
    ASSERT_EQ(0, writer.FinishEntry());
  }
  ASSERT_EQ(0, writer.Finish());
  ASSERT_EQ(0, fclose(zip_file));
}

TEST(InstallTest, verify_package_compatibility_no_entry) {
  TemporaryFile temp_file;
  // The archive must have something to be opened correctly.
  BuildZipArchive({ { "dummy_entry", "" } }, temp_file.release(), kCompressStored);

  // Doesn't contain compatibility zip entry.
  ZipArchiveHandle zip;
  ASSERT_EQ(0, OpenArchive(temp_file.path, &zip));
  ASSERT_TRUE(verify_package_compatibility(zip));
  CloseArchive(zip);
}

TEST(InstallTest, verify_package_compatibility_invalid_entry) {
  TemporaryFile temp_file;
  BuildZipArchive({ { "compatibility.zip", "" } }, temp_file.release(), kCompressStored);

  // Empty compatibility zip entry.
  ZipArchiveHandle zip;
  ASSERT_EQ(0, OpenArchive(temp_file.path, &zip));
  ASSERT_FALSE(verify_package_compatibility(zip));
  CloseArchive(zip);
}

TEST(InstallTest, read_metadata_from_package_smoke) {
  TemporaryFile temp_file;
  const std::string content("abc=defg");
  BuildZipArchive({ { "META-INF/com/android/metadata", content } }, temp_file.release(),
                  kCompressStored);

  ZipArchiveHandle zip;
  ASSERT_EQ(0, OpenArchive(temp_file.path, &zip));
  std::map<std::string, std::string> metadata;
  ASSERT_TRUE(ReadMetadataFromPackage(zip, &metadata));
  ASSERT_EQ("defg", metadata["abc"]);
  CloseArchive(zip);

  TemporaryFile temp_file2;
  BuildZipArchive({ { "META-INF/com/android/metadata", content } }, temp_file2.release(),
                  kCompressDeflated);

  ASSERT_EQ(0, OpenArchive(temp_file2.path, &zip));
  metadata.clear();
  ASSERT_TRUE(ReadMetadataFromPackage(zip, &metadata));
  ASSERT_EQ("defg", metadata["abc"]);
  CloseArchive(zip);
}

TEST(InstallTest, read_metadata_from_package_no_entry) {
  TemporaryFile temp_file;
  BuildZipArchive({ { "dummy_entry", "" } }, temp_file.release(), kCompressStored);

  ZipArchiveHandle zip;
  ASSERT_EQ(0, OpenArchive(temp_file.path, &zip));
  std::map<std::string, std::string> metadata;
  ASSERT_FALSE(ReadMetadataFromPackage(zip, &metadata));
  CloseArchive(zip);
}

TEST(InstallTest, read_wipe_ab_partition_list) {
  std::vector<std::string> partition_list = {
    "/dev/block/bootdevice/by-name/system_a", "/dev/block/bootdevice/by-name/system_b",
    "/dev/block/bootdevice/by-name/vendor_a", "/dev/block/bootdevice/by-name/vendor_b",
    "/dev/block/bootdevice/by-name/userdata", "# Wipe the boot partitions last",
    "/dev/block/bootdevice/by-name/boot_a",   "/dev/block/bootdevice/by-name/boot_b",
  };
  TemporaryFile temp_file;
  BuildZipArchive({ { "recovery.wipe", android::base::Join(partition_list, '\n') } },
                  temp_file.release(), kCompressDeflated);
  std::string wipe_package;
  ASSERT_TRUE(android::base::ReadFileToString(temp_file.path, &wipe_package));

  auto package = Package::CreateMemoryPackage(
      std::vector<uint8_t>(wipe_package.begin(), wipe_package.end()), nullptr);

  auto read_partition_list = GetWipePartitionList(package.get());
  std::vector<std::string> expected = {
    "/dev/block/bootdevice/by-name/system_a", "/dev/block/bootdevice/by-name/system_b",
    "/dev/block/bootdevice/by-name/vendor_a", "/dev/block/bootdevice/by-name/vendor_b",
    "/dev/block/bootdevice/by-name/userdata", "/dev/block/bootdevice/by-name/boot_a",
    "/dev/block/bootdevice/by-name/boot_b",
  };
  ASSERT_EQ(expected, read_partition_list);
}

TEST(InstallTest, verify_package_compatibility_with_libvintf_malformed_xml) {
  TemporaryFile compatibility_zip_file;
  std::string malformed_xml = "malformed";
  BuildZipArchive({ { "system_manifest.xml", malformed_xml } }, compatibility_zip_file.release(),
                  kCompressDeflated);

  TemporaryFile temp_file;
  std::string compatibility_zip_content;
  ASSERT_TRUE(
      android::base::ReadFileToString(compatibility_zip_file.path, &compatibility_zip_content));
  BuildZipArchive({ { "compatibility.zip", compatibility_zip_content } }, temp_file.release(),
                  kCompressStored);

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
  BuildZipArchive({ { "system_manifest.xml", system_manifest_xml_content } },
                  compatibility_zip_file.release(), kCompressDeflated);

  TemporaryFile temp_file;
  std::string compatibility_zip_content;
  ASSERT_TRUE(
      android::base::ReadFileToString(compatibility_zip_file.path, &compatibility_zip_content));
  BuildZipArchive({ { "compatibility.zip", compatibility_zip_content } }, temp_file.release(),
                  kCompressStored);

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

TEST(InstallTest, SetUpNonAbUpdateCommands) {
  TemporaryFile temp_file;
  static constexpr const char* UPDATE_BINARY_NAME = "META-INF/com/google/android/update-binary";
  BuildZipArchive({ { UPDATE_BINARY_NAME, "" } }, temp_file.release(), kCompressStored);

  ZipArchiveHandle zip;
  ASSERT_EQ(0, OpenArchive(temp_file.path, &zip));
  int status_fd = 10;
  std::string package = "/path/to/update.zip";
  TemporaryDir td;
  std::string binary_path = std::string(td.path) + "/update_binary";
  Paths::Get().set_temporary_update_binary(binary_path);
  std::vector<std::string> cmd;
  ASSERT_EQ(0, SetUpNonAbUpdateCommands(package, zip, 0, status_fd, &cmd));
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
  ASSERT_EQ(0, SetUpNonAbUpdateCommands(package, zip, 2, status_fd, &cmd));
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
}

TEST(InstallTest, SetUpNonAbUpdateCommands_MissingUpdateBinary) {
  TemporaryFile temp_file;
  // The archive must have something to be opened correctly.
  BuildZipArchive({ { "dummy_entry", "" } }, temp_file.release(), kCompressStored);

  // Missing update binary.
  ZipArchiveHandle zip;
  ASSERT_EQ(0, OpenArchive(temp_file.path, &zip));
  int status_fd = 10;
  std::string package = "/path/to/update.zip";
  TemporaryDir td;
  Paths::Get().set_temporary_update_binary(std::string(td.path) + "/update_binary");
  std::vector<std::string> cmd;
  ASSERT_EQ(INSTALL_CORRUPT, SetUpNonAbUpdateCommands(package, zip, 0, status_fd, &cmd));
  CloseArchive(zip);
}

static void VerifyAbUpdateCommands(const std::string& serialno, bool success = true) {
  TemporaryFile temp_file;

  const std::string properties = "some_properties";
  std::string device = android::base::GetProperty("ro.product.device", "");
  ASSERT_NE("", device);
  std::string timestamp = android::base::GetProperty("ro.build.date.utc", "");
  ASSERT_NE("", timestamp);

  std::vector<std::string> meta{ "ota-type=AB", "pre-device=" + device,
                                 "post-timestamp=" + timestamp };
  if (!serialno.empty()) {
    meta.push_back("serialno=" + serialno);
  }
  std::string metadata_string = android::base::Join(meta, "\n");

  BuildZipArchive({ { "payload.bin", "" },
                    { "payload_properties.txt", properties },
                    { "META-INF/com/android/metadata", metadata_string } },
                  temp_file.release(), kCompressStored);

  ZipArchiveHandle zip;
  ASSERT_EQ(0, OpenArchive(temp_file.path, &zip));
  ZipString payload_name("payload.bin");
  ZipEntry payload_entry;
  ASSERT_EQ(0, FindEntry(zip, payload_name, &payload_entry));

  std::map<std::string, std::string> metadata;
  ASSERT_TRUE(ReadMetadataFromPackage(zip, &metadata));
  if (success) {
    ASSERT_EQ(0, CheckPackageMetadata(metadata, OtaType::AB));

    int status_fd = 10;
    std::string package = "/path/to/update.zip";
    std::vector<std::string> cmd;
    ASSERT_EQ(0, SetUpAbUpdateCommands(package, zip, status_fd, &cmd));
    ASSERT_EQ(5U, cmd.size());
    ASSERT_EQ("/system/bin/update_engine_sideload", cmd[0]);
    ASSERT_EQ("--payload=file://" + package, cmd[1]);
    ASSERT_EQ("--offset=" + std::to_string(payload_entry.offset), cmd[2]);
    ASSERT_EQ("--headers=" + properties, cmd[3]);
    ASSERT_EQ("--status_fd=" + std::to_string(status_fd), cmd[4]);
  } else {
    ASSERT_EQ(INSTALL_ERROR, CheckPackageMetadata(metadata, OtaType::AB));
  }
  CloseArchive(zip);
}

TEST(InstallTest, SetUpAbUpdateCommands) {
  // Empty serialno will pass the verification.
  VerifyAbUpdateCommands({});
}

TEST(InstallTest, SetUpAbUpdateCommands_MissingPayloadPropertiesTxt) {
  TemporaryFile temp_file;

  std::string device = android::base::GetProperty("ro.product.device", "");
  ASSERT_NE("", device);
  std::string timestamp = android::base::GetProperty("ro.build.date.utc", "");
  ASSERT_NE("", timestamp);
  std::string metadata = android::base::Join(
      std::vector<std::string>{
          "ota-type=AB", "pre-device=" + device, "post-timestamp=" + timestamp,
      },
      "\n");

  BuildZipArchive(
      {
          { "payload.bin", "" },
          { "META-INF/com/android/metadata", metadata },
      },
      temp_file.release(), kCompressStored);

  ZipArchiveHandle zip;
  ASSERT_EQ(0, OpenArchive(temp_file.path, &zip));
  int status_fd = 10;
  std::string package = "/path/to/update.zip";
  std::vector<std::string> cmd;
  ASSERT_EQ(INSTALL_CORRUPT, SetUpAbUpdateCommands(package, zip, status_fd, &cmd));
  CloseArchive(zip);
}

TEST(InstallTest, SetUpAbUpdateCommands_MultipleSerialnos) {
  std::string serialno = android::base::GetProperty("ro.serialno", "");
  ASSERT_NE("", serialno);

  // Single matching serialno will pass the verification.
  VerifyAbUpdateCommands(serialno);

  static constexpr char alphabet[] =
      "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  auto generator = []() { return alphabet[rand() % (sizeof(alphabet) - 1)]; };

  // Generate 900 random serial numbers.
  std::string random_serialno;
  for (size_t i = 0; i < 900; i++) {
    generate_n(back_inserter(random_serialno), serialno.size(), generator);
    random_serialno.append("|");
  }
  // Random serialnos should fail the verification.
  VerifyAbUpdateCommands(random_serialno, false);

  std::string long_serialno = random_serialno + serialno + "|";
  for (size_t i = 0; i < 99; i++) {
    generate_n(back_inserter(long_serialno), serialno.size(), generator);
    long_serialno.append("|");
  }
  // String with the matching serialno should pass the verification.
  VerifyAbUpdateCommands(long_serialno);
}

static void test_check_package_metadata(const std::string& metadata_string, OtaType ota_type,
                                        int exptected_result) {
  TemporaryFile temp_file;
  BuildZipArchive(
      {
          { "META-INF/com/android/metadata", metadata_string },
      },
      temp_file.release(), kCompressStored);

  ZipArchiveHandle zip;
  ASSERT_EQ(0, OpenArchive(temp_file.path, &zip));

  std::map<std::string, std::string> metadata;
  ASSERT_TRUE(ReadMetadataFromPackage(zip, &metadata));
  ASSERT_EQ(exptected_result, CheckPackageMetadata(metadata, ota_type));
  CloseArchive(zip);
}

TEST(InstallTest, CheckPackageMetadata_ota_type) {
  std::string device = android::base::GetProperty("ro.product.device", "");
  ASSERT_NE("", device);

  // ota-type must be present
  std::string metadata = android::base::Join(
      std::vector<std::string>{
          "pre-device=" + device,
          "post-timestamp=" + std::to_string(std::numeric_limits<int64_t>::max()),
      },
      "\n");
  test_check_package_metadata(metadata, OtaType::AB, INSTALL_ERROR);

  // Checks if ota-type matches
  metadata = android::base::Join(
      std::vector<std::string>{
          "ota-type=AB",
          "pre-device=" + device,
          "post-timestamp=" + std::to_string(std::numeric_limits<int64_t>::max()),
      },
      "\n");
  test_check_package_metadata(metadata, OtaType::AB, 0);

  test_check_package_metadata(metadata, OtaType::BRICK, INSTALL_ERROR);
}

TEST(InstallTest, CheckPackageMetadata_device_type) {
  // device type can not be empty
  std::string metadata = android::base::Join(
      std::vector<std::string>{
          "ota-type=BRICK",
      },
      "\n");
  test_check_package_metadata(metadata, OtaType::BRICK, INSTALL_ERROR);

  // device type mismatches
  metadata = android::base::Join(
      std::vector<std::string>{
          "ota-type=BRICK",
          "pre-device=dummy_device_type",
      },
      "\n");
  test_check_package_metadata(metadata, OtaType::BRICK, INSTALL_ERROR);
}

TEST(InstallTest, CheckPackageMetadata_serial_number_smoke) {
  std::string device = android::base::GetProperty("ro.product.device", "");
  ASSERT_NE("", device);

  // Serial number doesn't need to exist
  std::string metadata = android::base::Join(
      std::vector<std::string>{
          "ota-type=BRICK",
          "pre-device=" + device,
      },
      "\n");
  test_check_package_metadata(metadata, OtaType::BRICK, 0);

  // Serial number mismatches
  metadata = android::base::Join(
      std::vector<std::string>{
          "ota-type=BRICK",
          "pre-device=" + device,
          "serialno=dummy_serial",
      },
      "\n");
  test_check_package_metadata(metadata, OtaType::BRICK, INSTALL_ERROR);

  std::string serialno = android::base::GetProperty("ro.serialno", "");
  ASSERT_NE("", serialno);
  metadata = android::base::Join(
      std::vector<std::string>{
          "ota-type=BRICK",
          "pre-device=" + device,
          "serialno=" + serialno,
      },
      "\n");
  test_check_package_metadata(metadata, OtaType::BRICK, 0);
}

TEST(InstallTest, CheckPackageMetadata_multiple_serial_number) {
  std::string device = android::base::GetProperty("ro.product.device", "");
  ASSERT_NE("", device);

  std::string serialno = android::base::GetProperty("ro.serialno", "");
  ASSERT_NE("", serialno);

  std::vector<std::string> serial_numbers;
  // Creates a dummy serial number string.
  for (char c = 'a'; c <= 'z'; c++) {
    serial_numbers.emplace_back(serialno.size(), c);
  }

  // No matched serialno found.
  std::string metadata = android::base::Join(
      std::vector<std::string>{
          "ota-type=BRICK",
          "pre-device=" + device,
          "serialno=" + android::base::Join(serial_numbers, '|'),
      },
      "\n");
  test_check_package_metadata(metadata, OtaType::BRICK, INSTALL_ERROR);

  serial_numbers.emplace_back(serialno);
  std::shuffle(serial_numbers.begin(), serial_numbers.end(), std::default_random_engine());
  metadata = android::base::Join(
      std::vector<std::string>{
          "ota-type=BRICK",
          "pre-device=" + device,
          "serialno=" + android::base::Join(serial_numbers, '|'),
      },
      "\n");
  test_check_package_metadata(metadata, OtaType::BRICK, 0);
}

TEST(InstallTest, CheckPackageMetadata_ab_build_version) {
  std::string device = android::base::GetProperty("ro.product.device", "");
  ASSERT_NE("", device);

  std::string build_version = android::base::GetProperty("ro.build.version.incremental", "");
  ASSERT_NE("", build_version);

  std::string metadata = android::base::Join(
      std::vector<std::string>{
          "ota-type=AB",
          "pre-device=" + device,
          "pre-build-incremental=" + build_version,
          "post-timestamp=" + std::to_string(std::numeric_limits<int64_t>::max()),
      },
      "\n");
  test_check_package_metadata(metadata, OtaType::AB, 0);

  metadata = android::base::Join(
      std::vector<std::string>{
          "ota-type=AB",
          "pre-device=" + device,
          "pre-build-incremental=dummy_build",
          "post-timestamp=" + std::to_string(std::numeric_limits<int64_t>::max()),
      },
      "\n");
  test_check_package_metadata(metadata, OtaType::AB, INSTALL_ERROR);
}

TEST(InstallTest, CheckPackageMetadata_ab_fingerprint) {
  std::string device = android::base::GetProperty("ro.product.device", "");
  ASSERT_NE("", device);

  std::string finger_print = android::base::GetProperty("ro.build.fingerprint", "");
  ASSERT_NE("", finger_print);

  std::string metadata = android::base::Join(
      std::vector<std::string>{
          "ota-type=AB",
          "pre-device=" + device,
          "pre-build=" + finger_print,
          "post-timestamp=" + std::to_string(std::numeric_limits<int64_t>::max()),
      },
      "\n");
  test_check_package_metadata(metadata, OtaType::AB, 0);

  metadata = android::base::Join(
      std::vector<std::string>{
          "ota-type=AB",
          "pre-device=" + device,
          "pre-build=dummy_build_fingerprint",
          "post-timestamp=" + std::to_string(std::numeric_limits<int64_t>::max()),
      },
      "\n");
  test_check_package_metadata(metadata, OtaType::AB, INSTALL_ERROR);
}

TEST(InstallTest, CheckPackageMetadata_ab_post_timestamp) {
  std::string device = android::base::GetProperty("ro.product.device", "");
  ASSERT_NE("", device);

  // post timestamp is required for upgrade.
  std::string metadata = android::base::Join(
      std::vector<std::string>{
          "ota-type=AB",
          "pre-device=" + device,
      },
      "\n");
  test_check_package_metadata(metadata, OtaType::AB, INSTALL_ERROR);

  // post timestamp should be larger than the timestamp on device.
  metadata = android::base::Join(
      std::vector<std::string>{
          "ota-type=AB",
          "pre-device=" + device,
          "post-timestamp=0",
      },
      "\n");
  test_check_package_metadata(metadata, OtaType::AB, INSTALL_ERROR);

  // fingerprint is required for downgrade
  metadata = android::base::Join(
      std::vector<std::string>{
          "ota-type=AB",
          "pre-device=" + device,
          "post-timestamp=0",
          "ota-downgrade=yes",
      },
      "\n");
  test_check_package_metadata(metadata, OtaType::AB, INSTALL_ERROR);

  std::string finger_print = android::base::GetProperty("ro.build.fingerprint", "");
  ASSERT_NE("", finger_print);

  metadata = android::base::Join(
      std::vector<std::string>{
          "ota-type=AB",
          "pre-device=" + device,
          "post-timestamp=0",
          "pre-build=" + finger_print,
          "ota-downgrade=yes",
      },
      "\n");
  test_check_package_metadata(metadata, OtaType::AB, 0);
}
