/*
 * Copyright (C) 2017 The Android Open Source Project
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

// Private headers exposed for testing purpose only.

#pragma once

#include <string>
#include <vector>

#include <ziparchive/zip_archive.h>

// Extract the update binary from the open zip archive |zip| located at |package| to |binary_path|.
// Store the command line that should be called into |cmd|. The |status_fd| is the file descriptor
// the child process should use to report back the progress of the update.
int update_binary_command(const std::string& package, ZipArchiveHandle zip,
                          const std::string& binary_path, int retry_count, int status_fd,
                          std::vector<std::string>* cmd);
