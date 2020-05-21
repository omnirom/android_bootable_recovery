/*
 * Copyright (C) 2007 The Android Open Source Project
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

#include <stdlib.h>
#include <string>
#include <vector>

#ifdef AB_OTA_UPDATER
#include <inttypes.h>
#include <map>
#include <android-base/parseint.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#endif
#include <cutils/properties.h>

#include "common.h"
#include "installcommand.h"
#include "zipwrap.hpp"
#ifndef USE_MINZIP
#include <ziparchive/zip_archive.h>
#include <vintf/VintfObjectRecovery.h>
#endif
#ifdef USE_OLD_VERIFIER
#include "verifier24/verifier.h"
#else
#include "verifier.h"
#endif

#ifdef AB_OTA_UPDATER

static constexpr const char* AB_OTA_PAYLOAD_PROPERTIES = "payload_properties.txt";
static constexpr const char* AB_OTA_PAYLOAD = "payload.bin";
static constexpr const char* METADATA_PATH = "META-INF/com/android/metadata";

// This function parses and returns the build.version.incremental
static int parse_build_number(std::string str) {
    size_t pos = str.find("=");
    if (pos != std::string::npos) {
        std::string num_string = android::base::Trim(str.substr(pos+1));
        int build_number;
        if (android::base::ParseInt(num_string.c_str(), &build_number, 0)) {
            return build_number;
        }
    }

    printf("Failed to parse build number in %s.\n", str.c_str());
    return -1;
}

bool read_metadata_from_package(ZipWrap* zip, std::string* meta_data) {
    long size = zip->GetUncompressedSize(METADATA_PATH);
    if (size <= 0)
		return false;

    meta_data->resize(size, '\0');
    if (!zip->ExtractToBuffer(METADATA_PATH, reinterpret_cast<uint8_t*>(&(*meta_data)[0]))) {
        printf("Failed to read metadata in update package.\n");
        return false;
    }
    return true;
}

// Read the build.version.incremental of src/tgt from the metadata and log it to last_install.
void read_source_target_build(ZipWrap* zip/*, std::vector<std::string>& log_buffer*/) {
    std::string meta_data;
    if (!read_metadata_from_package(zip, &meta_data)) {
        return;
    }
    // Examples of the pre-build and post-build strings in metadata:
    // pre-build-incremental=2943039
    // post-build-incremental=2951741
    std::vector<std::string> lines = android::base::Split(meta_data, "\n");
    for (const std::string& line : lines) {
        std::string str = android::base::Trim(line);
        if (android::base::StartsWith(str, "pre-build-incremental")){
            int source_build = parse_build_number(str);
            if (source_build != -1) {
                printf("source_build: %d\n", source_build);
                /*log_buffer.push_back(android::base::StringPrintf("source_build: %d",
                        source_build));*/
            }
        } else if (android::base::StartsWith(str, "post-build-incremental")) {
            int target_build = parse_build_number(str);
            if (target_build != -1) {
                printf("target_build: %d\n", target_build);
                /*log_buffer.push_back(android::base::StringPrintf("target_build: %d",
                        target_build));*/
            }
        }
    }
}

// Parses the metadata of the OTA package in |zip| and checks whether we are
// allowed to accept this A/B package. Downgrading is not allowed unless
// explicitly enabled in the package and only for incremental packages.
static int check_newer_ab_build(ZipWrap* zip)
{
    std::string metadata_str;
    if (!read_metadata_from_package(zip, &metadata_str)) {
        return INSTALL_CORRUPT;
    }
    std::map<std::string, std::string> metadata;
    for (const std::string& line : android::base::Split(metadata_str, "\n")) {
        size_t eq = line.find('=');
        if (eq != std::string::npos) {
            metadata[line.substr(0, eq)] = line.substr(eq + 1);
        }
    }
    char value[PROPERTY_VALUE_MAX];
    char propmodel[PROPERTY_VALUE_MAX];
    char propname[PROPERTY_VALUE_MAX];

    property_get("ro.product.device", value, "");
    property_get("ro.product.model", propmodel, "");
    property_get("ro.product.name", propname, "");
    const std::string& pkg_device = metadata["pre-device"];

    std::vector<std::string> assertResults = android::base::Split(pkg_device, ",");

    bool deviceExists = false;

    for(const std::string& deviceAssert : assertResults)
    {
        std::string assertName = android::base::Trim(deviceAssert);
        if ((assertName == value || assertName == propmodel || assertName == propname ) && !assertName.empty()) {
            deviceExists = true;
            break;
        }
    }

    if (!deviceExists) {
        printf("Package is for product %s but expected %s\n",
            pkg_device.c_str(), value);
        return INSTALL_ERROR;
    }

    // We allow the package to not have any serialno, but if it has a non-empty
    // value it should match.
    property_get("ro.serialno", value, "");
    const std::string& pkg_serial_no = metadata["serialno"];
    if (!pkg_serial_no.empty() && pkg_serial_no != value) {
        printf("Package is for serial %s\n", pkg_serial_no.c_str());
        return INSTALL_ERROR;
    }

    if (metadata["ota-type"] != "AB") {
        printf("Package is not A/B\n");
        return INSTALL_ERROR;
    }

    // Incremental updates should match the current build.
    property_get("ro.build.version.incremental", value, "");
    const std::string& pkg_pre_build = metadata["pre-build-incremental"];
    if (!pkg_pre_build.empty() && pkg_pre_build != value) {
        printf("Package is for source build %s but expected %s\n",
             pkg_pre_build.c_str(), value);
        return INSTALL_ERROR;
    }
    property_get("ro.build.fingerprint", value, "");
    const std::string& pkg_pre_build_fingerprint = metadata["pre-build"];
    if (!pkg_pre_build_fingerprint.empty() &&
        pkg_pre_build_fingerprint != value) {
        printf("Package is for source build %s but expected %s\n",
             pkg_pre_build_fingerprint.c_str(), value);
        return INSTALL_ERROR;
    }

    return 0;
}

int
abupdate_binary_command(const char* path, ZipWrap* zip, int retry_count __unused,
                      int status_fd, std::vector<std::string>* cmd)
{
    read_source_target_build(zip);
    int ret = check_newer_ab_build(zip);
    if (ret) {
        return ret;
    }

    // For A/B updates we extract the payload properties to a buffer and obtain
    // the RAW payload offset in the zip file.
    if (!zip->EntryExists(AB_OTA_PAYLOAD_PROPERTIES)) {
        printf("Can't find %s\n", AB_OTA_PAYLOAD_PROPERTIES);
        return INSTALL_CORRUPT;
    }
    std::vector<unsigned char> payload_properties(
            zip->GetUncompressedSize(AB_OTA_PAYLOAD_PROPERTIES));
    if (!zip->ExtractToBuffer(AB_OTA_PAYLOAD_PROPERTIES, payload_properties.data())) {
        printf("Can't extract %s\n", AB_OTA_PAYLOAD_PROPERTIES);
        return INSTALL_CORRUPT;
    }

    if (!zip->EntryExists(AB_OTA_PAYLOAD)) {
        printf("Can't find %s\n", AB_OTA_PAYLOAD);
        return INSTALL_CORRUPT;
    }
    long payload_offset = zip->GetEntryOffset(AB_OTA_PAYLOAD);
    *cmd = {
        "/sbin/update_engine_sideload",
        android::base::StringPrintf("--payload=file://%s", path),
        android::base::StringPrintf("--offset=%ld", payload_offset),
        "--headers=" + std::string(payload_properties.begin(),
                                   payload_properties.end()),
        android::base::StringPrintf("--status_fd=%d", status_fd),
    };
    return INSTALL_SUCCESS;
}

#else

void read_source_target_build(ZipWrap* zip __unused /*, std::vector<std::string>& log_buffer*/) {return;}

int
abupdate_binary_command(__unused const char* path, __unused ZipWrap* zip, __unused int retry_count,
                      __unused int status_fd, __unused std::vector<std::string>* cmd)
{
    printf("No support for AB OTA zips included\n");
    return INSTALL_CORRUPT;
}

#endif

int
update_binary_command(const char* path, int retry_count,
                      int status_fd, std::vector<std::string>* cmd)
{
    char charfd[16];
    sprintf(charfd, "%i", status_fd);
    cmd->push_back(TMP_UPDATER_BINARY_PATH);
    cmd->push_back(EXPAND(RECOVERY_API_VERSION));
    cmd->push_back(charfd);
    cmd->push_back(path);
    /**cmd = {
        TMP_UPDATER_BINARY_PATH,
        EXPAND(RECOVERY_API_VERSION),   // defined in Android.mk
        charfd,
        path,
    };*/
    if (retry_count > 0)
        cmd->push_back("retry");
    return 0;
}

#ifdef USE_MINZIP
bool verify_package_compatibility(ZipWrap *package_zip) {
  if (package_zip->EntryExists("compatibility.zip"))
    printf("Cannot verify treble package compatibility, must build TWRP in Oreo tree or higher.\n");
  return true;
}
#else
// Verifes the compatibility info in a Treble-compatible package. Returns true directly if the
// entry doesn't exist. Note that the compatibility info is packed in a zip file inside the OTA
// package.
bool verify_package_compatibility(ZipWrap *zw) {
  ZipArchiveHandle package_zip = zw->GetZipArchiveHandle();
  printf("Verifying package compatibility...\n");

  static constexpr const char* COMPATIBILITY_ZIP_ENTRY = "compatibility.zip";
  ZipString compatibility_entry_name(COMPATIBILITY_ZIP_ENTRY);
  ZipEntry compatibility_entry;
  if (FindEntry(package_zip, compatibility_entry_name, &compatibility_entry) != 0) {
    printf("Package doesn't contain %s entry\n", COMPATIBILITY_ZIP_ENTRY);
    return true;
  }

  std::string zip_content(compatibility_entry.uncompressed_length, '\0');
  int32_t ret;
  if ((ret = ExtractToMemory(package_zip, &compatibility_entry,
                             reinterpret_cast<uint8_t*>(&zip_content[0]),
                             compatibility_entry.uncompressed_length)) != 0) {
    printf("Failed to read %s: %s\n", COMPATIBILITY_ZIP_ENTRY, ErrorCodeString(ret));
    return false;
  }

  ZipArchiveHandle zip_handle;
  ret = OpenArchiveFromMemory(static_cast<void*>(const_cast<char*>(zip_content.data())),
                              zip_content.size(), COMPATIBILITY_ZIP_ENTRY, &zip_handle);
  if (ret != 0) {
    printf("Failed to OpenArchiveFromMemory: %s\n", ErrorCodeString(ret));
    return false;
  }

  // Iterate all the entries inside COMPATIBILITY_ZIP_ENTRY and read the contents.
  void* cookie;
  ret = StartIteration(zip_handle, &cookie, nullptr, nullptr);
  if (ret != 0) {
    printf("Failed to start iterating zip entries: %s\n", ErrorCodeString(ret));
    CloseArchive(zip_handle);
    return false;
  }
  std::unique_ptr<void, decltype(&EndIteration)> guard(cookie, EndIteration);

  std::vector<std::string> compatibility_info;
  ZipEntry info_entry;
  ZipString info_name;
  while (Next(cookie, &info_entry, &info_name) == 0) {
    std::string content(info_entry.uncompressed_length, '\0');
    int32_t ret = ExtractToMemory(zip_handle, &info_entry, reinterpret_cast<uint8_t*>(&content[0]),
                                  info_entry.uncompressed_length);
    if (ret != 0) {
      printf("Failed to read %s: %s\n", info_name.name, ErrorCodeString(ret));
      CloseArchive(zip_handle);
      return false;
    }
    compatibility_info.emplace_back(std::move(content));
  }
  CloseArchive(zip_handle);

  // VintfObjectRecovery::CheckCompatibility returns zero on success. TODO THIS CAUSES A WEIRD COMPILE ERROR
  std::string err;
  int result = android::vintf::VintfObjectRecovery::CheckCompatibility(compatibility_info, &err);
  if (result == 0) {
    return true;
  }

  printf("Failed to verify package compatibility (result %i): %s\n", result, err.c_str());
  return false;
}
#endif
