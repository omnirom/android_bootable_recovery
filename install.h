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

#include <string>
#include <ziparchive/zip_archive.h>

enum { INSTALL_SUCCESS, INSTALL_ERROR, INSTALL_CORRUPT, INSTALL_NONE, INSTALL_SKIPPED,
        INSTALL_RETRY };

// Installs the given update package. If INSTALL_SUCCESS is returned and *wipe_cache is true on
// exit, caller should wipe the cache partition.
int install_package(const std::string& package, bool* wipe_cache, const std::string& install_file,
                    bool needs_mount, int retry_count);

// Verify the package by ota keys. Return true if the package is verified successfully,
// otherwise return false.
bool verify_package(const unsigned char* package_data, size_t package_size);

// Read meta data file of the package, write its content in the string pointed by meta_data.
// Return true if succeed, otherwise return false.
bool read_metadata_from_package(ZipArchiveHandle zip, std::string* metadata);

// Verifies the compatibility info in a Treble-compatible package. Returns true directly if the
// entry doesn't exist.
bool verify_package_compatibility(ZipArchiveHandle package_zip);

#endif  // RECOVERY_INSTALL_H_
