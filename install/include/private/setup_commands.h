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

// Sets up the commands for a non-A/B update. Extracts the updater binary from the open zip archive
// |zip| located at |package|. Stores the command line that should be called into |cmd|. The
// |status_fd| is the file descriptor the child process should use to report back the progress of
// the update.
bool SetUpNonAbUpdateCommands(const std::string& package, ZipArchiveHandle zip, int retry_count,
                              int status_fd, std::vector<std::string>* cmd);

// Sets up the commands for an A/B update. Extracts the needed entries from the open zip archive
// |zip| located at |package|. Stores the command line that should be called into |cmd|. The
// |status_fd| is the file descriptor the child process should use to report back the progress of
// the update. Note that since this applies to the sideloading flow only, it takes one less
// parameter |retry_count| than the non-A/B version.
bool SetUpAbUpdateCommands(const std::string& package, ZipArchiveHandle zip, int status_fd,
                           std::vector<std::string>* cmd);
