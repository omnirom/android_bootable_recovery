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

#include <android-base/parseint.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>

#include "common.h"
#include "error_code.h"
#include "install.h"
#include "minui/minui.h"
#include "minzip/SysUtil.h"
#include "minzip/Zip.h"
#include "mtdutils/mounts.h"
#include "mtdutils/mtdutils.h"
#include "roots.h"
#include "ui.h"
#include "verifier.h"

extern RecoveryUI* ui;

#define ASSUMED_UPDATE_BINARY_NAME  "META-INF/com/google/android/update-binary"
#define PUBLIC_KEYS_FILE "/res/keys"
static constexpr const char* METADATA_PATH = "META-INF/com/android/metadata";

// Default allocation of progress bar segments to operations
static const int VERIFICATION_PROGRESS_TIME = 60;
static const float VERIFICATION_PROGRESS_FRACTION = 0.25;
static const float DEFAULT_FILES_PROGRESS_FRACTION = 0.4;
static const float DEFAULT_IMAGE_PROGRESS_FRACTION = 0.1;

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

    LOGE("Failed to parse build number in %s.\n", str.c_str());
    return -1;
}

// Read the build.version.incremental of src/tgt from the metadata and log it to last_install.
static void read_source_target_build(ZipArchive* zip, std::vector<std::string>& log_buffer) {
    const ZipEntry* meta_entry = mzFindZipEntry(zip, METADATA_PATH);
    if (meta_entry == nullptr) {
        LOGE("Failed to find %s in update package.\n", METADATA_PATH);
        return;
    }

    std::string meta_data(meta_entry->uncompLen, '\0');
    if (!mzReadZipEntry(zip, meta_entry, &meta_data[0], meta_entry->uncompLen)) {
        LOGE("Failed to read metadata in update package.\n");
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
try_update_binary(const char* path, ZipArchive* zip, bool* wipe_cache,
                  std::vector<std::string>& log_buffer, int retry_count)
{
    read_source_target_build(zip, log_buffer);

    const ZipEntry* binary_entry =
            mzFindZipEntry(zip, ASSUMED_UPDATE_BINARY_NAME);
    if (binary_entry == NULL) {
        mzCloseZipArchive(zip);
        return INSTALL_CORRUPT;
    }

    const char* binary = "/tmp/update_binary";
    unlink(binary);
    int fd = creat(binary, 0755);
    if (fd < 0) {
        mzCloseZipArchive(zip);
        LOGE("Can't make %s\n", binary);
        return INSTALL_ERROR;
    }
    bool ok = mzExtractZipEntryToFile(zip, binary_entry, fd);
    close(fd);
    mzCloseZipArchive(zip);

    if (!ok) {
        LOGE("Can't copy %s\n", ASSUMED_UPDATE_BINARY_NAME);
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

    const char** args = (const char**)malloc(sizeof(char*) * 6);
    args[0] = binary;
    args[1] = EXPAND(RECOVERY_API_VERSION);   // defined in Android.mk
    char* temp = (char*)malloc(10);
    sprintf(temp, "%d", pipefd[1]);
    args[2] = temp;
    args[3] = (char*)path;
    args[4] = retry_count > 0 ? "retry" : NULL;
    args[5] = NULL;

    pid_t pid = fork();
    if (pid == 0) {
        umask(022);
        close(pipefd[0]);
        execv(binary, (char* const*)args);
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
            LOGE("unknown command [%s]\n", command);
        }
    }
    fclose(from_child);

    int status;
    waitpid(pid, &status, 0);
    if (retry_update) {
        return INSTALL_RETRY;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        LOGE("Error in %s\n(Status %d)\n", path, WEXITSTATUS(status));
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
    LOGI("Update location: %s\n", path);

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
        LOGE("failed to map file\n");
        return INSTALL_CORRUPT;
    }

    // Load keys.
    std::vector<Certificate> loadedKeys;
    if (!load_keys(PUBLIC_KEYS_FILE, loadedKeys)) {
        LOGE("Failed to load keys\n");
        return INSTALL_CORRUPT;
    }
    LOGI("%zu key(s) loaded from %s\n", loadedKeys.size(), PUBLIC_KEYS_FILE);

    // Verify package.
    ui->Print("Verifying update package...\n");
    auto t0 = std::chrono::system_clock::now();
    int err = verify_file(map.addr, map.length, loadedKeys);
    std::chrono::duration<double> duration = std::chrono::system_clock::now() - t0;
    ui->Print("Update package verification took %.1f s (result %d).\n", duration.count(), err);
    if (err != VERIFY_SUCCESS) {
        LOGE("signature verification failed\n");
        log_buffer.push_back(android::base::StringPrintf("error: %d", kZipVerificationFailure));

        sysReleaseMap(&map);
        return INSTALL_CORRUPT;
    }

    // Try to open the package.
    ZipArchive zip;
    err = mzOpenZipArchive(map.addr, map.length, &zip);
    if (err != 0) {
        LOGE("Can't open %s\n(%s)\n", path, err != -1 ? strerror(err) : "bad");
        log_buffer.push_back(android::base::StringPrintf("error: %d", kZipOpenFailure));

        sysReleaseMap(&map);
        return INSTALL_CORRUPT;
    }

    // Verify and install the contents of the package.
    ui->Print("Installing update...\n");
    if (retry_count > 0) {
        ui->Print("Retry attempt: %d\n", retry_count);
    }
    ui->SetEnableReboot(false);
    int result = try_update_binary(path, &zip, wipe_cache, log_buffer, retry_count);
    ui->SetEnableReboot(true);
    ui->Print("\n");

    sysReleaseMap(&map);

    return result;
}

int
install_package(const char* path, bool* wipe_cache, const char* install_file,
                bool needs_mount, int retry_count)
{
    modified_flash = true;
    auto start = std::chrono::system_clock::now();

    FILE* install_log = fopen_path(install_file, "w");
    if (install_log) {
        fputs(path, install_log);
        fputc('\n', install_log);
    } else {
        LOGE("failed to open last_install: %s\n", strerror(errno));
    }
    int result;
    std::vector<std::string> log_buffer;
    if (setup_install_mounts() != 0) {
        LOGE("failed to set up expected mounts for install; aborting\n");
        result = INSTALL_ERROR;
    } else {
        result = really_install_package(path, wipe_cache, needs_mount, log_buffer, retry_count);
    }
    if (install_log != nullptr) {
        fputc(result == INSTALL_SUCCESS ? '1' : '0', install_log);
        fputc('\n', install_log);
        std::chrono::duration<double> duration = std::chrono::system_clock::now() - start;
        int count = static_cast<int>(duration.count());
        // Report the time spent to apply OTA update in seconds.
        fprintf(install_log, "time_total: %d\n", count);
        fprintf(install_log, "retry: %d\n", retry_count);

        for (const auto& s : log_buffer) {
            fprintf(install_log, "%s\n", s.c_str());
        }

        fclose(install_log);
    }
    return result;
}
