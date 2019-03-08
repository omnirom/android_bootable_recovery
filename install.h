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

#ifndef RECOVERY_INSTALL_H_
#define RECOVERY_INSTALL_H_

#include <stddef.h>

#include <map>
#include <string>
#include <vector>

#include <ziparchive/zip_archive.h>

enum InstallResult {
  INSTALL_SUCCESS,
  INSTALL_ERROR,
  INSTALL_CORRUPT,
  INSTALL_NONE,
  INSTALL_SKIPPED,
  INSTALL_RETRY,
  INSTALL_KEY_INTERRUPTED
};

enum class OtaType {
  AB,
  BLOCK,
  BRICK,
};

// Installs the given update package. If INSTALL_SUCCESS is returned and *wipe_cache is true on
// exit, caller should wipe the cache partition.
int install_package(const std::string& package, bool* wipe_cache, bool needs_mount,
                    int retry_count);

// Verify the package by ota keys. Return true if the package is verified successfully,
// otherwise return false.
bool verify_package(const unsigned char* package_data, size_t package_size);

// Reads meta data file of the package; parses each line in the format "key=value"; and writes the
// result to |metadata|. Return true if succeed, otherwise return false.
bool ReadMetadataFromPackage(ZipArchiveHandle zip, std::map<std::string, std::string>* metadata);

// Reads the "recovery.wipe" entry in the zip archive returns a list of partitions to wipe.
std::vector<std::string> GetWipePartitionList(const std::string& wipe_package);

// Verifies the compatibility info in a Treble-compatible package. Returns true directly if the
// entry doesn't exist.
bool verify_package_compatibility(ZipArchiveHandle package_zip);

// Checks if the the metadata in the OTA package has expected values. Returns 0 on success.
// Mandatory checks: ota-type, pre-device and serial number(if presents)
// AB OTA specific checks: pre-build version, fingerprint, timestamp.
int CheckPackageMetadata(const std::map<std::string, std::string>& metadata, OtaType ota_type);

#endif  // RECOVERY_INSTALL_H_
