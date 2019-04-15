/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "install/wipe_data.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <functional>
#include <memory>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/stringprintf.h>

#include "otautil/dirutil.h"
#include "otautil/logging.h"
#include "otautil/roots.h"
#include "recovery_ui/ui.h"

constexpr const char* CACHE_ROOT = "/cache";
constexpr const char* DATA_ROOT = "/data";
constexpr const char* METADATA_ROOT = "/metadata";

constexpr const char* CACHE_LOG_DIR = "/cache/recovery";

static struct selabel_handle* sehandle;

void SetWipeDataSehandle(selabel_handle* handle) {
  sehandle = handle;
}

struct saved_log_file {
  std::string name;
  struct stat sb;
  std::string data;
};

static bool EraseVolume(const char* volume, RecoveryUI* ui, bool convert_fbe) {
  bool is_cache = (strcmp(volume, CACHE_ROOT) == 0);
  bool is_data = (strcmp(volume, DATA_ROOT) == 0);

  ui->SetBackground(RecoveryUI::ERASING);
  ui->SetProgressType(RecoveryUI::INDETERMINATE);

  std::vector<saved_log_file> log_files;

  if (is_cache) {
    // If we're reformatting /cache, we load any past logs
    // (i.e. "/cache/recovery/last_*") and the current log
    // ("/cache/recovery/log") into memory, so we can restore them after
    // the reformat.

    ensure_path_mounted(volume);

    struct dirent* de;
    std::unique_ptr<DIR, decltype(&closedir)> d(opendir(CACHE_LOG_DIR), closedir);
    if (d) {
      while ((de = readdir(d.get())) != nullptr) {
        if (strncmp(de->d_name, "last_", 5) == 0 || strcmp(de->d_name, "log") == 0) {
          std::string path = android::base::StringPrintf("%s/%s", CACHE_LOG_DIR, de->d_name);

          struct stat sb;
          if (stat(path.c_str(), &sb) == 0) {
            // truncate files to 512kb
            if (sb.st_size > (1 << 19)) {
              sb.st_size = 1 << 19;
            }

            std::string data(sb.st_size, '\0');
            FILE* f = fopen(path.c_str(), "rbe");
            fread(&data[0], 1, data.size(), f);
            fclose(f);

            log_files.emplace_back(saved_log_file{ path, sb, data });
          }
        }
      }
    } else {
      if (errno != ENOENT) {
        PLOG(ERROR) << "Failed to opendir " << CACHE_LOG_DIR;
      }
    }
  }

  ui->Print("Formatting %s...\n", volume);

  ensure_path_unmounted(volume);

  int result;
  if (is_data && convert_fbe) {
    constexpr const char* CONVERT_FBE_DIR = "/tmp/convert_fbe";
    constexpr const char* CONVERT_FBE_FILE = "/tmp/convert_fbe/convert_fbe";
    // Create convert_fbe breadcrumb file to signal init to convert to file based encryption, not
    // full disk encryption.
    if (mkdir(CONVERT_FBE_DIR, 0700) != 0) {
      PLOG(ERROR) << "Failed to mkdir " << CONVERT_FBE_DIR;
      return false;
    }
    FILE* f = fopen(CONVERT_FBE_FILE, "wbe");
    if (!f) {
      PLOG(ERROR) << "Failed to convert to file encryption";
      return false;
    }
    fclose(f);
    result = format_volume(volume, CONVERT_FBE_DIR);
    remove(CONVERT_FBE_FILE);
    rmdir(CONVERT_FBE_DIR);
  } else {
    result = format_volume(volume);
  }

  if (is_cache) {
    // Re-create the log dir and write back the log entries.
    if (ensure_path_mounted(CACHE_LOG_DIR) == 0 &&
        mkdir_recursively(CACHE_LOG_DIR, 0777, false, sehandle) == 0) {
      for (const auto& log : log_files) {
        if (!android::base::WriteStringToFile(log.data, log.name, log.sb.st_mode, log.sb.st_uid,
                                              log.sb.st_gid)) {
          PLOG(ERROR) << "Failed to write to " << log.name;
        }
      }
    } else {
      PLOG(ERROR) << "Failed to mount / create " << CACHE_LOG_DIR;
    }

    // Any part of the log we'd copied to cache is now gone.
    // Reset the pointer so we copy from the beginning of the temp
    // log.
    reset_tmplog_offset();
    copy_logs(true /* save_current_log */, true /* has_cache */, sehandle);
  }

  return (result == 0);
}

bool WipeCache(RecoveryUI* ui, const std::function<bool()>& confirm_func) {
  bool has_cache = volume_for_mount_point("/cache") != nullptr;
  if (!has_cache) {
    ui->Print("No /cache partition found.\n");
    return false;
  }

  if (confirm_func && !confirm_func()) {
    return false;
  }

  ui->Print("\n-- Wiping cache...\n");
  bool success = EraseVolume("/cache", ui, false);
  ui->Print("Cache wipe %s.\n", success ? "complete" : "failed");
  return success;
}

bool WipeData(Device* device, bool convert_fbe) {
  RecoveryUI* ui = device->GetUI();
  ui->Print("\n-- Wiping data...\n");
  bool success = device->PreWipeData();
  if (success) {
    success &= EraseVolume(DATA_ROOT, ui, convert_fbe);
    bool has_cache = volume_for_mount_point("/cache") != nullptr;
    if (has_cache) {
      success &= EraseVolume(CACHE_ROOT, ui, false);
    }
    if (volume_for_mount_point(METADATA_ROOT) != nullptr) {
      success &= EraseVolume(METADATA_ROOT, ui, false);
    }
  }
  if (success) {
    success &= device->PostWipeData();
  }
  ui->Print("Data wipe %s.\n", success ? "complete" : "failed");
  return success;
}