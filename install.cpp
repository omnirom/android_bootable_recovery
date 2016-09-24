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
#include <inttypes.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <limits>
#include <map>
#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/parseint.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#include <cutils/properties.h>

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
static constexpr const char* AB_OTA_PAYLOAD_PROPERTIES = "payload_properties.txt";
static constexpr const char* AB_OTA_PAYLOAD = "payload.bin";
#define PUBLIC_KEYS_FILE "/res/keys"
static constexpr const char* METADATA_PATH = "META-INF/com/android/metadata";
static constexpr const char* UNCRYPT_STATUS = "/cache/recovery/uncrypt_status";

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

bool read_metadata_from_package(ZipArchive* zip, std::string* meta_data) {
    const ZipEntry* meta_entry = mzFindZipEntry(zip, METADATA_PATH);
    if (meta_entry == nullptr) {
        LOGE("Failed to find %s in update package.\n", METADATA_PATH);
        return false;
    }

    meta_data->resize(meta_entry->uncompLen, '\0');
    if (!mzReadZipEntry(zip, meta_entry, &(*meta_data)[0], meta_entry->uncompLen)) {
        LOGE("Failed to read metadata in update package.\n");
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

// Extract the update binary from the open zip archive |zip| located at |path|
// and store into |cmd| the command line that should be called. The |status_fd|
// is the file descriptor the child process should use to report back the
// progress of the update.
static int
update_binary_command(const char* path, ZipArchive* zip, int retry_count,
                      int status_fd, std::vector<std::string>* cmd);

#ifdef AB_OTA_UPDATER

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
        LOGE("Package is for product %s but expected %s\n",
             pkg_device.c_str(), value);
        return INSTALL_ERROR;
    }

    // We allow the package to not have any serialno, but if it has a non-empty
    // value it should match.
    property_get("ro.serialno", value, "");
    const std::string& pkg_serial_no = metadata["serialno"];
    if (!pkg_serial_no.empty() && pkg_serial_no != value) {
        LOGE("Package is for serial %s\n", pkg_serial_no.c_str());
        return INSTALL_ERROR;
    }

    if (metadata["ota-type"] != "AB") {
        LOGE("Package is not A/B\n");
        return INSTALL_ERROR;
    }

    // Incremental updates should match the current build.
    property_get("ro.build.version.incremental", value, "");
    const std::string& pkg_pre_build = metadata["pre-build-incremental"];
    if (!pkg_pre_build.empty() && pkg_pre_build != value) {
        LOGE("Package is for source build %s but expected %s\n",
             pkg_pre_build.c_str(), value);
        return INSTALL_ERROR;
    }
    property_get("ro.build.fingerprint", value, "");
    const std::string& pkg_pre_build_fingerprint = metadata["pre-build"];
    if (!pkg_pre_build_fingerprint.empty() &&
        pkg_pre_build_fingerprint != value) {
        LOGE("Package is for source build %s but expected %s\n",
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
            LOGE("Update package is older than the current build, expected a "
                 "build newer than timestamp %" PRIu64 " but package has "
                 "timestamp %" PRIu64 " and downgrade not allowed.\n",
                 build_timestampt, pkg_post_timespampt);
            return INSTALL_ERROR;
        }
        if (pkg_pre_build_fingerprint.empty()) {
            LOGE("Downgrade package must have a pre-build version set, not "
                 "allowed.\n");
            return INSTALL_ERROR;
        }
    }

    return 0;
}

static int
update_binary_command(const char* path, ZipArchive* zip, int retry_count,
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
        LOGE("Can't find %s\n", AB_OTA_PAYLOAD_PROPERTIES);
        return INSTALL_CORRUPT;
    }
    std::vector<unsigned char> payload_properties(
            mzGetZipEntryUncompLen(properties_entry));
    if (!mzExtractZipEntryToBuffer(zip, properties_entry,
                                   payload_properties.data())) {
        LOGE("Can't extract %s\n", AB_OTA_PAYLOAD_PROPERTIES);
        return INSTALL_CORRUPT;
    }

    const ZipEntry* payload_entry = mzFindZipEntry(zip, AB_OTA_PAYLOAD);
    if (!payload_entry) {
        LOGE("Can't find %s\n", AB_OTA_PAYLOAD);
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
    return 0;
}

#else  // !AB_OTA_UPDATER

static int
update_binary_command(const char* path, ZipArchive* zip, int retry_count,
                      int status_fd, std::vector<std::string>* cmd)
{
    // On traditional updates we extract the update binary from the package.
    const ZipEntry* binary_entry =
            mzFindZipEntry(zip, ASSUMED_UPDATE_BINARY_NAME);
    if (binary_entry == NULL) {
        return INSTALL_CORRUPT;
    }

    const char* binary = "/tmp/update_binary";
    unlink(binary);
    int fd = creat(binary, 0755);
    if (fd < 0) {
        LOGE("Can't make %s\n", binary);
        return INSTALL_ERROR;
    }
    bool ok = mzExtractZipEntryToFile(zip, binary_entry, fd);
    close(fd);

    if (!ok) {
        LOGE("Can't copy %s\n", ASSUMED_UPDATE_BINARY_NAME);
        return INSTALL_ERROR;
    }

    *cmd = {
        binary,
        EXPAND(RECOVERY_API_VERSION),   // defined in Android.mk
        std::to_string(status_fd),
        path,
    };
    if (retry_count > 0)
        cmd->push_back("retry");
    return 0;
}
#endif  // !AB_OTA_UPDATER

// If the package contains an update binary, extract it and run it.
static int
try_update_binary(const char* path, ZipArchive* zip, bool* wipe_cache,
                  std::vector<std::string>& log_buffer, int retry_count)
{
    read_source_target_build(zip, log_buffer);

    int pipefd[2];
    pipe(pipefd);

    std::vector<std::string> args;
    int ret = update_binary_command(path, zip, retry_count, pipefd[1], &args);
    mzCloseZipArchive(zip);
    if (ret) {
        close(pipefd[0]);
        close(pipefd[1]);
        return ret;
    }

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

    // Convert the vector to a NULL-terminated char* array suitable for execv.
    const char* chr_args[args.size() + 1];
    chr_args[args.size()] = NULL;
    for (size_t i = 0; i < args.size(); i++) {
        chr_args[i] = args[i].c_str();
    }

    pid_t pid = fork();
    if (pid == 0) {
        umask(022);
        close(pipefd[0]);
        execv(chr_args[0], const_cast<char**>(chr_args));
        fprintf(stdout, "E:Can't run %s (%s)\n", chr_args[0], strerror(errno));
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

    // Verify package.
    if (!verify_package(map.addr, map.length)) {
        log_buffer.push_back(android::base::StringPrintf("error: %d", kZipVerificationFailure));
        sysReleaseMap(&map);
        return INSTALL_CORRUPT;
    }

    // Try to open the package.
    ZipArchive zip;
    int err = mzOpenZipArchive(map.addr, map.length, &zip);
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

    int result;
    std::vector<std::string> log_buffer;
    if (setup_install_mounts() != 0) {
        LOGE("failed to set up expected mounts for install; aborting\n");
        result = INSTALL_ERROR;
    } else {
        result = really_install_package(path, wipe_cache, needs_mount, log_buffer, retry_count);
    }

    // Measure the time spent to apply OTA update in seconds.
    std::chrono::duration<double> duration = std::chrono::system_clock::now() - start;
    int time_total = static_cast<int>(duration.count());

    if (ensure_path_mounted(UNCRYPT_STATUS) != 0) {
        LOGW("Can't mount %s\n", UNCRYPT_STATUS);
    } else {
        std::string uncrypt_status;
        if (!android::base::ReadFileToString(UNCRYPT_STATUS, &uncrypt_status)) {
            LOGW("failed to read uncrypt status: %s\n", strerror(errno));
        } else if (!android::base::StartsWith(uncrypt_status, "uncrypt_")) {
            LOGW("corrupted uncrypt_status: %s: %s\n", uncrypt_status.c_str(), strerror(errno));
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
        LOGE("failed to write %s: %s\n", install_file, strerror(errno));
    }

    // Write a copy into last_log.
    LOGI("%s\n", log_content.c_str());

    return result;
}

bool verify_package(const unsigned char* package_data, size_t package_size) {
    std::vector<Certificate> loadedKeys;
    if (!load_keys(PUBLIC_KEYS_FILE, loadedKeys)) {
        LOGE("Failed to load keys\n");
        return false;
    }
    LOGI("%zu key(s) loaded from %s\n", loadedKeys.size(), PUBLIC_KEYS_FILE);

    // Verify package.
    ui->Print("Verifying update package...\n");
    auto t0 = std::chrono::system_clock::now();
    int err = verify_file(const_cast<unsigned char*>(package_data), package_size, loadedKeys);
    std::chrono::duration<double> duration = std::chrono::system_clock::now() - t0;
    ui->Print("Update package verification took %.1f s (result %d).\n", duration.count(), err);
    if (err != VERIFY_SUCCESS) {
        LOGE("Signature verification failed\n");
        LOGE("error: %d\n", kZipVerificationFailure);
        return false;
    }
    return true;
}
