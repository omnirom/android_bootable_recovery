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

#pragma once

#include <stddef.h>

#include <map>
#include <string>
#include <vector>

#include <ziparchive/zip_archive.h>

#include "package.h"
#include "recovery_ui/ui.h"

enum InstallResult {
  INSTALL_SUCCESS,
  INSTALL_ERROR,
  INSTALL_CORRUPT,
  INSTALL_NONE,
  INSTALL_SKIPPED,
  INSTALL_RETRY,
  INSTALL_KEY_INTERRUPTED,
  INSTALL_REBOOT,
};

enum class OtaType {
  AB,
  BLOCK,
  BRICK,
};

// Installs the given update package. The package_id is a string provided by the caller (e.g. the
// package path) to identify the package and log to last_install. This function should also wipe the
// cache partition after a successful installation if |should_wipe_cache| is true or an updater
// command asks to wipe the cache.
InstallResult InstallPackage(Package* package, const std::string_view package_id,
                             bool should_wipe_cache, int retry_count, RecoveryUI* ui);

// Verifies the package by ota keys. Returns true if the package is verified successfully,
// otherwise returns false.
bool verify_package(Package* package, RecoveryUI* ui);

// Reads meta data file of the package; parses each line in the format "key=value"; and writes the
// result to |metadata|. Return true if succeed, otherwise return false.
bool ReadMetadataFromPackage(ZipArchiveHandle zip, std::map<std::string, std::string>* metadata);

// Checks if the metadata in the OTA package has expected values. Mandatory checks: ota-type,
// pre-device and serial number (if presents). A/B OTA specific checks: pre-build version,
// fingerprint, timestamp.
bool CheckPackageMetadata(const std::map<std::string, std::string>& metadata, OtaType ota_type);

// Ensures the path to the update package is mounted. Also set the |should_use_fuse| to true if the
// package stays on a removable media.
bool SetupPackageMount(const std::string& package_path, bool* should_use_fuse);
