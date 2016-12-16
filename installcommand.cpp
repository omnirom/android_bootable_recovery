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
#include <map>
#include <android-base/parseint.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#endif
#include <cutils/properties.h>

#include "common.h"
#include "installcommand.h"
#include "minzip/SysUtil.h"
#include "minzip/Zip.h"
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

bool read_metadata_from_package(ZipArchive* zip, std::string* meta_data) {
    const ZipEntry* meta_entry = mzFindZipEntry(zip, METADATA_PATH);
    if (meta_entry == nullptr) {
        printf("Failed to find %s in update package.\n", METADATA_PATH);
        return false;
    }

    meta_data->resize(meta_entry->uncompLen, '\0');
    if (!mzReadZipEntry(zip, meta_entry, &(*meta_data)[0], meta_entry->uncompLen)) {
        printf("Failed to read metadata in update package.\n");
        return false;
    }
    return true;
}

// Read the build.version.incremental of src/tgt from the metadata and log it to last_install.
static void read_source_target_build(ZipArchive* zip, std::vector<std::string>& log_buffer) {
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
                log_buffer.push_back(android::base::StringPrintf("source_build: %d",
                        source_build));
            }
        } else if (android::base::StartsWith(str, "post-build-incremental")) {
            int target_build = parse_build_number(str);
            if (target_build != -1) {
                log_buffer.push_back(android::base::StringPrintf("target_build: %d",
                        target_build));
            }
        }
    }
}

// Parses the metadata of the OTA package in |zip| and checks whether we are
// allowed to accept this A/B package. Downgrading is not allowed unless
// explicitly enabled in the package and only for incremental packages.
static int check_newer_ab_build(ZipArchive* zip)
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

    property_get("ro.product.device", value, "");
    const std::string& pkg_device = metadata["pre-device"];
    if (pkg_device != value || pkg_device.empty()) {
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

    // Check for downgrade version.
    int64_t build_timestampt = property_get_int64(
            "ro.build.date.utc", std::numeric_limits<int64_t>::max());
    int64_t pkg_post_timespampt = 0;
    // We allow to full update to the same version we are running, in case there
    // is a problem with the current copy of that version.
    if (metadata["post-timestamp"].empty() ||
        !android::base::ParseInt(metadata["post-timestamp"].c_str(),
                                 &pkg_post_timespampt) ||
        pkg_post_timespampt < build_timestampt) {
        if (metadata["ota-downgrade"] != "yes") {
            printf("Update package is older than the current build, expected a "
                 "build newer than timestamp %" PRIu64 " but package has "
                 "timestamp %" PRIu64 " and downgrade not allowed.\n",
                 build_timestampt, pkg_post_timespampt);
            return INSTALL_ERROR;
        }
        if (pkg_pre_build_fingerprint.empty()) {
            printf("Downgrade package must have a pre-build version set, not "
                 "allowed.\n");
            return INSTALL_ERROR;
        }
    }

    return 0;
}

int
abupdate_binary_command(const char* path, ZipArchive* zip, int retry_count,
                      int status_fd, std::vector<std::string>* cmd)
{
    int ret = check_newer_ab_build(zip);
    if (ret) {
        return ret;
    }

    // For A/B updates we extract the payload properties to a buffer and obtain
    // the RAW payload offset in the zip file.
    const ZipEntry* properties_entry =
            mzFindZipEntry(zip, AB_OTA_PAYLOAD_PROPERTIES);
    if (!properties_entry) {
        printf("Can't find %s\n", AB_OTA_PAYLOAD_PROPERTIES);
        return INSTALL_CORRUPT;
    }
    std::vector<unsigned char> payload_properties(
            mzGetZipEntryUncompLen(properties_entry));
    if (!mzExtractZipEntryToBuffer(zip, properties_entry,
                                   payload_properties.data())) {
        printf("Can't extract %s\n", AB_OTA_PAYLOAD_PROPERTIES);
        return INSTALL_CORRUPT;
    }

    const ZipEntry* payload_entry = mzFindZipEntry(zip, AB_OTA_PAYLOAD);
    if (!payload_entry) {
        printf("Can't find %s\n", AB_OTA_PAYLOAD);
        return INSTALL_CORRUPT;
    }
    long payload_offset = mzGetZipEntryOffset(payload_entry);
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

int
abupdate_binary_command(const char* path, ZipArchive* zip, int retry_count,
                      int status_fd, std::vector<std::string>* cmd)
{
    printf("No support for AB OTA zips included\n");
    return INSTALL_CORRUPT;
}

#endif

int
update_binary_command(const char* path, ZipArchive* zip, int retry_count,
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
