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

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#include <ziparchive/zip_archive.h>

#include "common.h"
#include "error_code.h"
#include "install.h"
#include "minui/minui.h"
#include "otautil/SysUtil.h"
#include "roots.h"
#include "ui.h"
#include "verifier.h"

#define ASSUMED_UPDATE_BINARY_NAME  "META-INF/com/google/android/update-binary"
#define PUBLIC_KEYS_FILE "/res/keys"
static constexpr const char* METADATA_PATH = "META-INF/com/android/metadata";
static constexpr const char* UNCRYPT_STATUS = "/cache/recovery/uncrypt_status";

// Default allocation of progress bar segments to operations
static const int VERIFICATION_PROGRESS_TIME = 60;
static const float VERIFICATION_PROGRESS_FRACTION = 0.25;
static const float DEFAULT_FILES_PROGRESS_FRACTION = 0.4;
static const float DEFAULT_IMAGE_PROGRESS_FRACTION = 0.1;

// This function parses and returns the build.version.incremental
static int parse_build_number(const std::string& str) {
    size_t pos = str.find('=');
    if (pos != std::string::npos) {
        std::string num_string = android::base::Trim(str.substr(pos+1));
        int build_number;
        if (android::base::ParseInt(num_string.c_str(), &build_number, 0)) {
            return build_number;
        }
    }

    LOG(ERROR) << "Failed to parse build number in " << str;
    return -1;
}

bool read_metadata_from_package(ZipArchiveHandle zip, std::string* meta_data) {
    ZipString metadata_path(METADATA_PATH);
    ZipEntry meta_entry;
    if (meta_data == nullptr) {
        LOG(ERROR) << "string* meta_data can't be nullptr";
        return false;
    }
    if (FindEntry(zip, metadata_path, &meta_entry) != 0) {
        LOG(ERROR) << "Failed to find " << METADATA_PATH << " in update package";
        return false;
    }

    meta_data->resize(meta_entry.uncompressed_length, '\0');
    if (ExtractToMemory(zip, &meta_entry, reinterpret_cast<uint8_t*>(&(*meta_data)[0]),
                        meta_entry.uncompressed_length) != 0) {
        LOG(ERROR) << "Failed to read metadata in update package";
        return false;
    }
    return true;
}

// Read the build.version.incremental of src/tgt from the metadata and log it to last_install.
static void read_source_target_build(ZipArchiveHandle zip, std::vector<std::string>& log_buffer) {
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

// If the package contains an update binary, extract it and run it.
static int
try_update_binary(const char* path, ZipArchiveHandle zip, bool* wipe_cache,
                  std::vector<std::string>& log_buffer, int retry_count)
{
    read_source_target_build(zip, log_buffer);

    ZipString binary_name(ASSUMED_UPDATE_BINARY_NAME);
    ZipEntry binary_entry;
    if (FindEntry(zip, binary_name, &binary_entry) != 0) {
        return INSTALL_CORRUPT;
    }

    const char* binary = "/tmp/update_binary";
    unlink(binary);
    int fd = creat(binary, 0755);
    if (fd < 0) {
        PLOG(ERROR) << "Can't make " << binary;
        return INSTALL_ERROR;
    }
    int error = ExtractEntryToFile(zip, &binary_entry, fd);
    close(fd);

    if (error != 0) {
        LOG(ERROR) << "Can't copy " << ASSUMED_UPDATE_BINARY_NAME
                   << " : " << ErrorCodeString(error);
        return INSTALL_ERROR;
    }

    int pipefd[2];
    pipe(pipefd);

    // When executing the update binary contained in the package, the
    // arguments passed are:
    //
    //   - the version number for this interface
    //
    //   - an fd to which the program can write in order to update the
    //     progress bar.  The program can write single-line commands:
    //
    //        progress <frac> <secs>
    //            fill up the next <frac> part of of the progress bar
    //            over <secs> seconds.  If <secs> is zero, use
    //            set_progress commands to manually control the
    //            progress of this segment of the bar.
    //
    //        set_progress <frac>
    //            <frac> should be between 0.0 and 1.0; sets the
    //            progress bar within the segment defined by the most
    //            recent progress command.
    //
    //        firmware <"hboot"|"radio"> <filename>
    //            arrange to install the contents of <filename> in the
    //            given partition on reboot.
    //
    //            (API v2: <filename> may start with "PACKAGE:" to
    //            indicate taking a file from the OTA package.)
    //
    //            (API v3: this command no longer exists.)
    //
    //        ui_print <string>
    //            display <string> on the screen.
    //
    //        wipe_cache
    //            a wipe of cache will be performed following a successful
    //            installation.
    //
    //        clear_display
    //            turn off the text display.
    //
    //        enable_reboot
    //            packages can explicitly request that they want the user
    //            to be able to reboot during installation (useful for
    //            debugging packages that don't exit).
    //
    //   - the name of the package zip file.
    //
    //   - an optional argument "retry" if this update is a retry of a failed
    //   update attempt.
    //

    const char* args[6];
    args[0] = binary;
    args[1] = EXPAND(RECOVERY_API_VERSION);   // defined in Android.mk
    char temp[16];
    snprintf(temp, sizeof(temp), "%d", pipefd[1]);
    args[2] = temp;
    args[3] = path;
    args[4] = retry_count > 0 ? "retry" : NULL;
    args[5] = NULL;

    pid_t pid = fork();
    if (pid == 0) {
        umask(022);
        close(pipefd[0]);
        execv(binary, const_cast<char**>(args));
        fprintf(stdout, "E:Can't run %s (%s)\n", binary, strerror(errno));
        _exit(-1);
    }
    close(pipefd[1]);

    *wipe_cache = false;
    bool retry_update = false;

    char buffer[1024];
    FILE* from_child = fdopen(pipefd[0], "r");
    while (fgets(buffer, sizeof(buffer), from_child) != NULL) {
        char* command = strtok(buffer, " \n");
        if (command == NULL) {
            continue;
        } else if (strcmp(command, "progress") == 0) {
            char* fraction_s = strtok(NULL, " \n");
            char* seconds_s = strtok(NULL, " \n");

            float fraction = strtof(fraction_s, NULL);
            int seconds = strtol(seconds_s, NULL, 10);

            ui->ShowProgress(fraction * (1-VERIFICATION_PROGRESS_FRACTION), seconds);
        } else if (strcmp(command, "set_progress") == 0) {
            char* fraction_s = strtok(NULL, " \n");
            float fraction = strtof(fraction_s, NULL);
            ui->SetProgress(fraction);
        } else if (strcmp(command, "ui_print") == 0) {
            char* str = strtok(NULL, "\n");
            if (str) {
                ui->PrintOnScreenOnly("%s", str);
            } else {
                ui->PrintOnScreenOnly("\n");
            }
            fflush(stdout);
        } else if (strcmp(command, "wipe_cache") == 0) {
            *wipe_cache = true;
        } else if (strcmp(command, "clear_display") == 0) {
            ui->SetBackground(RecoveryUI::NONE);
        } else if (strcmp(command, "enable_reboot") == 0) {
            // packages can explicitly request that they want the user
            // to be able to reboot during installation (useful for
            // debugging packages that don't exit).
            ui->SetEnableReboot(true);
        } else if (strcmp(command, "retry_update") == 0) {
            retry_update = true;
        } else if (strcmp(command, "log") == 0) {
            // Save the logging request from updater and write to
            // last_install later.
            log_buffer.push_back(std::string(strtok(NULL, "\n")));
        } else {
            LOG(ERROR) << "unknown command [" << command << "]";
        }
    }
    fclose(from_child);

    int status;
    waitpid(pid, &status, 0);
    if (retry_update) {
        return INSTALL_RETRY;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        LOG(ERROR) << "Error in " << path << " (Status " << WEXITSTATUS(status) << ")";
        return INSTALL_ERROR;
    }

    return INSTALL_SUCCESS;
}

static int
really_install_package(const char *path, bool* wipe_cache, bool needs_mount,
                       std::vector<std::string>& log_buffer, int retry_count)
{
    ui->SetBackground(RecoveryUI::INSTALLING_UPDATE);
    ui->Print("Finding update package...\n");
    // Give verification half the progress bar...
    ui->SetProgressType(RecoveryUI::DETERMINATE);
    ui->ShowProgress(VERIFICATION_PROGRESS_FRACTION, VERIFICATION_PROGRESS_TIME);
    LOG(INFO) << "Update location: " << path;

    // Map the update package into memory.
    ui->Print("Opening update package...\n");

    if (path && needs_mount) {
        if (path[0] == '@') {
            ensure_path_mounted(path+1);
        } else {
            ensure_path_mounted(path);
        }
    }

    MemMapping map;
    if (sysMapFile(path, &map) != 0) {
        LOG(ERROR) << "failed to map file";
        return INSTALL_CORRUPT;
    }

    // Verify package.
    if (!verify_package(map.addr, map.length)) {
        log_buffer.push_back(android::base::StringPrintf("error: %d", kZipVerificationFailure));
        sysReleaseMap(&map);
        return INSTALL_CORRUPT;
    }

    // Try to open the package.
    ZipArchiveHandle zip;
    int err = OpenArchiveFromMemory(map.addr, map.length, path, &zip);
    if (err != 0) {
        LOG(ERROR) << "Can't open " << path << " : " << ErrorCodeString(err);
        log_buffer.push_back(android::base::StringPrintf("error: %d", kZipOpenFailure));

        sysReleaseMap(&map);
        CloseArchive(zip);
        return INSTALL_CORRUPT;
    }

    // Verify and install the contents of the package.
    ui->Print("Installing update...\n");
    if (retry_count > 0) {
        ui->Print("Retry attempt: %d\n", retry_count);
    }
    ui->SetEnableReboot(false);
    int result = try_update_binary(path, zip, wipe_cache, log_buffer, retry_count);
    ui->SetEnableReboot(true);
    ui->Print("\n");

    sysReleaseMap(&map);
    CloseArchive(zip);
    return result;
}

int
install_package(const char* path, bool* wipe_cache, const char* install_file,
                bool needs_mount, int retry_count)
{
    modified_flash = true;
    auto start = std::chrono::system_clock::now();

    int result;
    std::vector<std::string> log_buffer;
    if (setup_install_mounts() != 0) {
        LOG(ERROR) << "failed to set up expected mounts for install; aborting";
        result = INSTALL_ERROR;
    } else {
        result = really_install_package(path, wipe_cache, needs_mount, log_buffer, retry_count);
    }

    // Measure the time spent to apply OTA update in seconds.
    std::chrono::duration<double> duration = std::chrono::system_clock::now() - start;
    int time_total = static_cast<int>(duration.count());

    if (ensure_path_mounted(UNCRYPT_STATUS) != 0) {
        LOG(WARNING) << "Can't mount " << UNCRYPT_STATUS;
    } else {
        std::string uncrypt_status;
        if (!android::base::ReadFileToString(UNCRYPT_STATUS, &uncrypt_status)) {
            PLOG(WARNING) << "failed to read uncrypt status";
        } else if (!android::base::StartsWith(uncrypt_status, "uncrypt_")) {
            LOG(WARNING) << "corrupted uncrypt_status: " << uncrypt_status;
        } else {
            log_buffer.push_back(android::base::Trim(uncrypt_status));
        }
    }

    // The first two lines need to be the package name and install result.
    std::vector<std::string> log_header = {
        path,
        result == INSTALL_SUCCESS ? "1" : "0",
        "time_total: " + std::to_string(time_total),
        "retry: " + std::to_string(retry_count),
    };
    std::string log_content = android::base::Join(log_header, "\n") + "\n" +
            android::base::Join(log_buffer, "\n");
    if (!android::base::WriteStringToFile(log_content, install_file)) {
        PLOG(ERROR) << "failed to write " << install_file;
    }

    // Write a copy into last_log.
    LOG(INFO) << log_content;

    return result;
}

bool verify_package(const unsigned char* package_data, size_t package_size) {
    std::vector<Certificate> loadedKeys;
    if (!load_keys(PUBLIC_KEYS_FILE, loadedKeys)) {
        LOG(ERROR) << "Failed to load keys";
        return false;
    }
    LOG(INFO) << loadedKeys.size() << " key(s) loaded from " << PUBLIC_KEYS_FILE;

    // Verify package.
    ui->Print("Verifying update package...\n");
    auto t0 = std::chrono::system_clock::now();
    int err = verify_file(const_cast<unsigned char*>(package_data), package_size, loadedKeys);
    std::chrono::duration<double> duration = std::chrono::system_clock::now() - t0;
    ui->Print("Update package verification took %.1f s (result %d).\n", duration.count(), err);
    if (err != VERIFY_SUCCESS) {
        LOG(ERROR) << "Signature verification failed";
        LOG(ERROR) << "error: " << kZipVerificationFailure;
        return false;
    }
    return true;
}
