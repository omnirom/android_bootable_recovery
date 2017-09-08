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

#include "install.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <limits>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/parsedouble.h>
#include <android-base/parseint.h>
#include <android-base/properties.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#include <vintf/VintfObjectRecovery.h>
#include <ziparchive/zip_archive.h>

#include "common.h"
#include "error_code.h"
#include "minui/minui.h"
#include "otautil/SysUtil.h"
#include "otautil/ThermalUtil.h"
#include "roots.h"
#include "ui.h"
#include "verifier.h"

using namespace std::chrono_literals;

#define PUBLIC_KEYS_FILE "/res/keys"
static constexpr const char* METADATA_PATH = "META-INF/com/android/metadata";
static constexpr const char* UNCRYPT_STATUS = "/cache/recovery/uncrypt_status";

// Default allocation of progress bar segments to operations
static constexpr int VERIFICATION_PROGRESS_TIME = 60;
static constexpr float VERIFICATION_PROGRESS_FRACTION = 0.25;
static constexpr float DEFAULT_FILES_PROGRESS_FRACTION = 0.4;
static constexpr float DEFAULT_IMAGE_PROGRESS_FRACTION = 0.1;

static std::condition_variable finish_log_temperature;

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

// Extract the update binary from the open zip archive |zip| located at |path| and store into |cmd|
// the command line that should be called. The |status_fd| is the file descriptor the child process
// should use to report back the progress of the update.
int update_binary_command(const std::string& path, ZipArchiveHandle zip, int retry_count,
                          int status_fd, std::vector<std::string>* cmd);

#ifdef AB_OTA_UPDATER

// Parses the metadata of the OTA package in |zip| and checks whether we are
// allowed to accept this A/B package. Downgrading is not allowed unless
// explicitly enabled in the package and only for incremental packages.
static int check_newer_ab_build(ZipArchiveHandle zip) {
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

  std::string value = android::base::GetProperty("ro.product.device", "");
  const std::string& pkg_device = metadata["pre-device"];
  if (pkg_device != value || pkg_device.empty()) {
    LOG(ERROR) << "Package is for product " << pkg_device << " but expected " << value;
    return INSTALL_ERROR;
  }

  // We allow the package to not have any serialno, but if it has a non-empty
  // value it should match.
  value = android::base::GetProperty("ro.serialno", "");
  const std::string& pkg_serial_no = metadata["serialno"];
  if (!pkg_serial_no.empty() && pkg_serial_no != value) {
    LOG(ERROR) << "Package is for serial " << pkg_serial_no;
    return INSTALL_ERROR;
  }

  if (metadata["ota-type"] != "AB") {
    LOG(ERROR) << "Package is not A/B";
    return INSTALL_ERROR;
  }

  // Incremental updates should match the current build.
  value = android::base::GetProperty("ro.build.version.incremental", "");
  const std::string& pkg_pre_build = metadata["pre-build-incremental"];
  if (!pkg_pre_build.empty() && pkg_pre_build != value) {
    LOG(ERROR) << "Package is for source build " << pkg_pre_build << " but expected " << value;
    return INSTALL_ERROR;
  }

  value = android::base::GetProperty("ro.build.fingerprint", "");
  const std::string& pkg_pre_build_fingerprint = metadata["pre-build"];
  if (!pkg_pre_build_fingerprint.empty() && pkg_pre_build_fingerprint != value) {
    LOG(ERROR) << "Package is for source build " << pkg_pre_build_fingerprint << " but expected "
               << value;
    return INSTALL_ERROR;
  }

  // Check for downgrade version.
  int64_t build_timestamp =
      android::base::GetIntProperty("ro.build.date.utc", std::numeric_limits<int64_t>::max());
  int64_t pkg_post_timestamp = 0;
  // We allow to full update to the same version we are running, in case there
  // is a problem with the current copy of that version.
  if (metadata["post-timestamp"].empty() ||
      !android::base::ParseInt(metadata["post-timestamp"].c_str(), &pkg_post_timestamp) ||
      pkg_post_timestamp < build_timestamp) {
    if (metadata["ota-downgrade"] != "yes") {
      LOG(ERROR) << "Update package is older than the current build, expected a build "
                    "newer than timestamp "
                 << build_timestamp << " but package has timestamp " << pkg_post_timestamp
                 << " and downgrade not allowed.";
      return INSTALL_ERROR;
    }
    if (pkg_pre_build_fingerprint.empty()) {
      LOG(ERROR) << "Downgrade package must have a pre-build version set, not allowed.";
      return INSTALL_ERROR;
    }
  }

  return 0;
}

int update_binary_command(const std::string& path, ZipArchiveHandle zip, int retry_count,
                          int status_fd, std::vector<std::string>* cmd) {
  CHECK(cmd != nullptr);
  int ret = check_newer_ab_build(zip);
  if (ret != 0) {
    return ret;
  }

  // For A/B updates we extract the payload properties to a buffer and obtain the RAW payload offset
  // in the zip file.
  static constexpr const char* AB_OTA_PAYLOAD_PROPERTIES = "payload_properties.txt";
  ZipString property_name(AB_OTA_PAYLOAD_PROPERTIES);
  ZipEntry properties_entry;
  if (FindEntry(zip, property_name, &properties_entry) != 0) {
    LOG(ERROR) << "Failed to find " << AB_OTA_PAYLOAD_PROPERTIES;
    return INSTALL_CORRUPT;
  }
  uint32_t properties_entry_length = properties_entry.uncompressed_length;
  std::vector<uint8_t> payload_properties(properties_entry_length);
  int32_t err =
      ExtractToMemory(zip, &properties_entry, payload_properties.data(), properties_entry_length);
  if (err != 0) {
    LOG(ERROR) << "Failed to extract " << AB_OTA_PAYLOAD_PROPERTIES << ": " << ErrorCodeString(err);
    return INSTALL_CORRUPT;
  }

  static constexpr const char* AB_OTA_PAYLOAD = "payload.bin";
  ZipString payload_name(AB_OTA_PAYLOAD);
  ZipEntry payload_entry;
  if (FindEntry(zip, payload_name, &payload_entry) != 0) {
    LOG(ERROR) << "Failed to find " << AB_OTA_PAYLOAD;
    return INSTALL_CORRUPT;
  }
  long payload_offset = payload_entry.offset;
  *cmd = {
    "/sbin/update_engine_sideload",
    "--payload=file://" + path,
    android::base::StringPrintf("--offset=%ld", payload_offset),
    "--headers=" + std::string(payload_properties.begin(), payload_properties.end()),
    android::base::StringPrintf("--status_fd=%d", status_fd),
  };
  return 0;
}

#else  // !AB_OTA_UPDATER

int update_binary_command(const std::string& path, ZipArchiveHandle zip, int retry_count,
                          int status_fd, std::vector<std::string>* cmd) {
  CHECK(cmd != nullptr);

  // On traditional updates we extract the update binary from the package.
  static constexpr const char* UPDATE_BINARY_NAME = "META-INF/com/google/android/update-binary";
  ZipString binary_name(UPDATE_BINARY_NAME);
  ZipEntry binary_entry;
  if (FindEntry(zip, binary_name, &binary_entry) != 0) {
    LOG(ERROR) << "Failed to find update binary " << UPDATE_BINARY_NAME;
    return INSTALL_CORRUPT;
  }

  const char* binary = "/tmp/update_binary";
  unlink(binary);
  int fd = creat(binary, 0755);
  if (fd == -1) {
    PLOG(ERROR) << "Failed to create " << binary;
    return INSTALL_ERROR;
  }

  int32_t error = ExtractEntryToFile(zip, &binary_entry, fd);
  close(fd);
  if (error != 0) {
    LOG(ERROR) << "Failed to extract " << UPDATE_BINARY_NAME << ": " << ErrorCodeString(error);
    return INSTALL_ERROR;
  }

  *cmd = {
    binary,
    EXPAND(RECOVERY_API_VERSION),  // defined in Android.mk
    std::to_string(status_fd),
    path,
  };
  if (retry_count > 0) {
    cmd->push_back("retry");
  }
  return 0;
}
#endif  // !AB_OTA_UPDATER

static void log_max_temperature(int* max_temperature) {
  CHECK(max_temperature != nullptr);
  std::mutex mtx;
  std::unique_lock<std::mutex> lck(mtx);
  while (finish_log_temperature.wait_for(lck, 20s) == std::cv_status::timeout) {
    *max_temperature = std::max(*max_temperature, GetMaxValueFromThermalZone());
  }
}

// If the package contains an update binary, extract it and run it.
static int try_update_binary(const char* path, ZipArchiveHandle zip, bool* wipe_cache,
                             std::vector<std::string>& log_buffer, int retry_count,
                             int* max_temperature) {
  read_source_target_build(zip, log_buffer);

  int pipefd[2];
  pipe(pipefd);

  std::vector<std::string> args;
  int ret = update_binary_command(path, zip, retry_count, pipefd[1], &args);
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
  //   - an FD to which the program can write in order to update the
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
  //        retry_update
  //            updater encounters some issue during the update. It requests
  //            a reboot to retry the same package automatically.
  //
  //        log <string>
  //            updater requests logging the string (e.g. cause of the
  //            failure).
  //
  //   - the name of the package zip file.
  //
  //   - an optional argument "retry" if this update is a retry of a failed
  //   update attempt.
  //

  // Convert the vector to a NULL-terminated char* array suitable for execv.
  const char* chr_args[args.size() + 1];
  chr_args[args.size()] = nullptr;
  for (size_t i = 0; i < args.size(); i++) {
    chr_args[i] = args[i].c_str();
  }

  pid_t pid = fork();

  if (pid == -1) {
    close(pipefd[0]);
    close(pipefd[1]);
    PLOG(ERROR) << "Failed to fork update binary";
    return INSTALL_ERROR;
  }

  if (pid == 0) {
    umask(022);
    close(pipefd[0]);
    execv(chr_args[0], const_cast<char**>(chr_args));
    // Bug: 34769056
    // We shouldn't use LOG/PLOG in the forked process, since they may cause
    // the child process to hang. This deadlock results from an improperly
    // copied mutex in the ui functions.
    fprintf(stdout, "E:Can't run %s (%s)\n", chr_args[0], strerror(errno));
    _exit(EXIT_FAILURE);
  }
  close(pipefd[1]);

  std::thread temperature_logger(log_max_temperature, max_temperature);

  *wipe_cache = false;
  bool retry_update = false;

  char buffer[1024];
  FILE* from_child = fdopen(pipefd[0], "r");
  while (fgets(buffer, sizeof(buffer), from_child) != nullptr) {
    std::string line(buffer);
    size_t space = line.find_first_of(" \n");
    std::string command(line.substr(0, space));
    if (command.empty()) continue;

    // Get rid of the leading and trailing space and/or newline.
    std::string args = space == std::string::npos ? "" : android::base::Trim(line.substr(space));

    if (command == "progress") {
      std::vector<std::string> tokens = android::base::Split(args, " ");
      double fraction;
      int seconds;
      if (tokens.size() == 2 && android::base::ParseDouble(tokens[0].c_str(), &fraction) &&
          android::base::ParseInt(tokens[1], &seconds)) {
        ui->ShowProgress(fraction * (1 - VERIFICATION_PROGRESS_FRACTION), seconds);
      } else {
        LOG(ERROR) << "invalid \"progress\" parameters: " << line;
      }
    } else if (command == "set_progress") {
      std::vector<std::string> tokens = android::base::Split(args, " ");
      double fraction;
      if (tokens.size() == 1 && android::base::ParseDouble(tokens[0].c_str(), &fraction)) {
        ui->SetProgress(fraction);
      } else {
        LOG(ERROR) << "invalid \"set_progress\" parameters: " << line;
      }
    } else if (command == "ui_print") {
      ui->PrintOnScreenOnly("%s\n", args.c_str());
      fflush(stdout);
    } else if (command == "wipe_cache") {
      *wipe_cache = true;
    } else if (command == "clear_display") {
      ui->SetBackground(RecoveryUI::NONE);
    } else if (command == "enable_reboot") {
      // packages can explicitly request that they want the user
      // to be able to reboot during installation (useful for
      // debugging packages that don't exit).
      ui->SetEnableReboot(true);
    } else if (command == "retry_update") {
      retry_update = true;
    } else if (command == "log") {
      if (!args.empty()) {
        // Save the logging request from updater and write to last_install later.
        log_buffer.push_back(args);
      } else {
        LOG(ERROR) << "invalid \"log\" parameters: " << line;
      }
    } else {
      LOG(ERROR) << "unknown command [" << command << "]";
    }
  }
  fclose(from_child);

  int status;
  waitpid(pid, &status, 0);

  finish_log_temperature.notify_one();
  temperature_logger.join();

  if (retry_update) {
    return INSTALL_RETRY;
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    LOG(ERROR) << "Error in " << path << " (Status " << WEXITSTATUS(status) << ")";
    return INSTALL_ERROR;
  }

  return INSTALL_SUCCESS;
}

// Verifes the compatibility info in a Treble-compatible package. Returns true directly if the
// entry doesn't exist. Note that the compatibility info is packed in a zip file inside the OTA
// package.
bool verify_package_compatibility(ZipArchiveHandle package_zip) {
  LOG(INFO) << "Verifying package compatibility...";

  static constexpr const char* COMPATIBILITY_ZIP_ENTRY = "compatibility.zip";
  ZipString compatibility_entry_name(COMPATIBILITY_ZIP_ENTRY);
  ZipEntry compatibility_entry;
  if (FindEntry(package_zip, compatibility_entry_name, &compatibility_entry) != 0) {
    LOG(INFO) << "Package doesn't contain " << COMPATIBILITY_ZIP_ENTRY << " entry";
    return true;
  }

  std::string zip_content(compatibility_entry.uncompressed_length, '\0');
  int32_t ret;
  if ((ret = ExtractToMemory(package_zip, &compatibility_entry,
                             reinterpret_cast<uint8_t*>(&zip_content[0]),
                             compatibility_entry.uncompressed_length)) != 0) {
    LOG(ERROR) << "Failed to read " << COMPATIBILITY_ZIP_ENTRY << ": " << ErrorCodeString(ret);
    return false;
  }

  ZipArchiveHandle zip_handle;
  ret = OpenArchiveFromMemory(static_cast<void*>(const_cast<char*>(zip_content.data())),
                              zip_content.size(), COMPATIBILITY_ZIP_ENTRY, &zip_handle);
  if (ret != 0) {
    LOG(ERROR) << "Failed to OpenArchiveFromMemory: " << ErrorCodeString(ret);
    return false;
  }

  // Iterate all the entries inside COMPATIBILITY_ZIP_ENTRY and read the contents.
  void* cookie;
  ret = StartIteration(zip_handle, &cookie, nullptr, nullptr);
  if (ret != 0) {
    LOG(ERROR) << "Failed to start iterating zip entries: " << ErrorCodeString(ret);
    CloseArchive(zip_handle);
    return false;
  }
  std::unique_ptr<void, decltype(&EndIteration)> guard(cookie, EndIteration);

  std::vector<std::string> compatibility_info;
  ZipEntry info_entry;
  ZipString info_name;
  while (Next(cookie, &info_entry, &info_name) == 0) {
    std::string content(info_entry.uncompressed_length, '\0');
    int32_t ret = ExtractToMemory(zip_handle, &info_entry, reinterpret_cast<uint8_t*>(&content[0]),
                                  info_entry.uncompressed_length);
    if (ret != 0) {
      LOG(ERROR) << "Failed to read " << info_name.name << ": " << ErrorCodeString(ret);
      CloseArchive(zip_handle);
      return false;
    }
    compatibility_info.emplace_back(std::move(content));
  }
  CloseArchive(zip_handle);

  // VintfObjectRecovery::CheckCompatibility returns zero on success.
  std::string err;
  int result = android::vintf::VintfObjectRecovery::CheckCompatibility(compatibility_info, &err);
  if (result == 0) {
    return true;
  }

  LOG(ERROR) << "Failed to verify package compatibility (result " << result << "): " << err;
  return false;
}

static int
really_install_package(const char *path, bool* wipe_cache, bool needs_mount,
                       std::vector<std::string>& log_buffer, int retry_count, int* max_temperature)
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

    // Additionally verify the compatibility of the package.
    if (!verify_package_compatibility(zip)) {
      log_buffer.push_back(android::base::StringPrintf("error: %d", kPackageCompatibilityFailure));
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
    int result = try_update_binary(path, zip, wipe_cache, log_buffer, retry_count, max_temperature);
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

    int start_temperature = GetMaxValueFromThermalZone();
    int max_temperature = start_temperature;

    int result;
    std::vector<std::string> log_buffer;
    if (setup_install_mounts() != 0) {
        LOG(ERROR) << "failed to set up expected mounts for install; aborting";
        result = INSTALL_ERROR;
    } else {
        result = really_install_package(path, wipe_cache, needs_mount, log_buffer, retry_count,
                                        &max_temperature);
    }

    // Measure the time spent to apply OTA update in seconds.
    std::chrono::duration<double> duration = std::chrono::system_clock::now() - start;
    int time_total = static_cast<int>(duration.count());

    bool has_cache = volume_for_path("/cache") != nullptr;
    // Skip logging the uncrypt_status on devices without /cache.
    if (has_cache) {
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
    }

    // The first two lines need to be the package name and install result.
    std::vector<std::string> log_header = {
        path,
        result == INSTALL_SUCCESS ? "1" : "0",
        "time_total: " + std::to_string(time_total),
        "retry: " + std::to_string(retry_count),
    };

    int end_temperature = GetMaxValueFromThermalZone();
    max_temperature = std::max(end_temperature, max_temperature);
    if (start_temperature > 0) {
      log_buffer.push_back("temperature_start: " + std::to_string(start_temperature));
    }
    if (end_temperature > 0) {
      log_buffer.push_back("temperature_end: " + std::to_string(end_temperature));
    }
    if (max_temperature > 0) {
      log_buffer.push_back("temperature_max: " + std::to_string(max_temperature));
    }

    std::string log_content = android::base::Join(log_header, "\n") + "\n" +
            android::base::Join(log_buffer, "\n") + "\n";
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
  int err = verify_file(package_data, package_size, loadedKeys,
                        std::bind(&RecoveryUI::SetProgress, ui, std::placeholders::_1));
  std::chrono::duration<double> duration = std::chrono::system_clock::now() - t0;
  ui->Print("Update package verification took %.1f s (result %d).\n", duration.count(), err);
  if (err != VERIFY_SUCCESS) {
    LOG(ERROR) << "Signature verification failed";
    LOG(ERROR) << "error: " << kZipVerificationFailure;
    return false;
  }
  return true;
}
