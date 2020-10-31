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

#ifndef RECOVERY_INSTALL_COMMAND_H_
#define RECOVERY_INSTALL_COMMAND_H_

#define TMP_UPDATER_BINARY_PATH "/tmp/updater"

#include <string>
#include <ziparchive/zip_archive.h>

bool read_metadata_from_package(ZipArchiveHandle zip, std::string* meta_data);

int
abupdate_binary_command(const char* path, int retry_count,
                      int status_fd, std::vector<std::string>* cmd);
int
update_binary_command(const char* path, int retry_count,
                      int status_fd, std::vector<std::string>* cmd);

bool verify_package_compatibility(ZipArchiveHandle package_zip);

void read_source_target_build(ZipArchiveHandle zip/*, std::vector<std::string>& log_buffer*/);

#endif  // RECOVERY_INSTALL_COMMAND_H_
