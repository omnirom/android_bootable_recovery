/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include "recovery_utils/logging.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/klog.h>
#include <sys/types.h>

#include <algorithm>
#include <memory>
#include <string>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/stringprintf.h>
#include <android-base/unique_fd.h>
#include <private/android_filesystem_config.h> /* for AID_SYSTEM */
#include <private/android_logger.h>            /* private pmsg functions */
#include <selinux/label.h>

#include "otautil/dirutil.h"
#include "otautil/paths.h"
#include "recovery_utils/roots.h"

constexpr const char* LOG_FILE = "/cache/recovery/log";
constexpr const char* LAST_INSTALL_FILE = "/cache/recovery/last_install";
constexpr const char* LAST_KMSG_FILE = "/cache/recovery/last_kmsg";
constexpr const char* LAST_LOG_FILE = "/cache/recovery/last_log";

constexpr const char* LAST_KMSG_FILTER = "recovery/last_kmsg";
constexpr const char* LAST_LOG_FILTER = "recovery/last_log";

constexpr const char* CACHE_LOG_DIR = "/cache/recovery";

static struct selabel_handle* logging_sehandle;

void SetLoggingSehandle(selabel_handle* handle) {
  logging_sehandle = handle;
}

// fopen(3)'s the given file, by mounting volumes and making parent dirs as necessary. Returns the
// file pointer, or nullptr on error.
static FILE* fopen_path(const std::string& path, const char* mode, const selabel_handle* sehandle) {
  if (ensure_path_mounted(path) != 0) {
    LOG(ERROR) << "Can't mount " << path;
    return nullptr;
  }

  // When writing, try to create the containing directory, if necessary. Use generous permissions,
  // the system (init.rc) will reset them.
  if (strchr("wa", mode[0])) {
    mkdir_recursively(path, 0777, true, sehandle);
  }
  return fopen(path.c_str(), mode);
}

void check_and_fclose(FILE* fp, const std::string& name) {
  fflush(fp);
  if (fsync(fileno(fp)) == -1) {
    PLOG(ERROR) << "Failed to fsync " << name;
  }
  if (ferror(fp)) {
    PLOG(ERROR) << "Error in " << name;
  }
  fclose(fp);
}

// close a file, log an error if the error indicator is set
ssize_t logbasename(log_id_t /* id */, char /* prio */, const char* filename, const char* /* buf */,
                    size_t len, void* arg) {
  bool* do_rotate = static_cast<bool*>(arg);
  if (std::string(LAST_KMSG_FILTER).find(filename) != std::string::npos ||
      std::string(LAST_LOG_FILTER).find(filename) != std::string::npos) {
    *do_rotate = true;
  }
  return len;
}

ssize_t logrotate(log_id_t id, char prio, const char* filename, const char* buf, size_t len,
                  void* arg) {
  bool* do_rotate = static_cast<bool*>(arg);
  if (!*do_rotate) {
    return __android_log_pmsg_file_write(id, prio, filename, buf, len);
  }

  std::string name(filename);
  size_t dot = name.find_last_of('.');
  std::string sub = name.substr(0, dot);

  if (std::string(LAST_KMSG_FILTER).find(sub) == std::string::npos &&
      std::string(LAST_LOG_FILTER).find(sub) == std::string::npos) {
    return __android_log_pmsg_file_write(id, prio, filename, buf, len);
  }

  // filename rotation
  if (dot == std::string::npos) {
    name += ".1";
  } else {
    std::string number = name.substr(dot + 1);
    if (!isdigit(number[0])) {
      name += ".1";
    } else {
      size_t i;
      if (!android::base::ParseUint(number, &i)) {
        LOG(ERROR) << "failed to parse uint in " << number;
        return -1;
      }
      name = sub + "." + std::to_string(i + 1);
    }
  }

  return __android_log_pmsg_file_write(id, prio, name.c_str(), buf, len);
}

// Rename last_log -> last_log.1 -> last_log.2 -> ... -> last_log.$max.
// Similarly rename last_kmsg -> last_kmsg.1 -> ... -> last_kmsg.$max.
// Overwrite any existing last_log.$max and last_kmsg.$max.
void rotate_logs(const char* last_log_file, const char* last_kmsg_file) {
  // Logs should only be rotated once.
  static bool rotated = false;
  if (rotated) {
    return;
  }
  rotated = true;

  for (int i = KEEP_LOG_COUNT - 1; i >= 0; --i) {
    std::string old_log = android::base::StringPrintf("%s", last_log_file);
    if (i > 0) {
      old_log += "." + std::to_string(i);
    }
    std::string new_log = android::base::StringPrintf("%s.%d", last_log_file, i + 1);
    // Ignore errors if old_log doesn't exist.
    rename(old_log.c_str(), new_log.c_str());

    std::string old_kmsg = android::base::StringPrintf("%s", last_kmsg_file);
    if (i > 0) {
      old_kmsg += "." + std::to_string(i);
    }
    std::string new_kmsg = android::base::StringPrintf("%s.%d", last_kmsg_file, i + 1);
    rename(old_kmsg.c_str(), new_kmsg.c_str());
  }
}

// Writes content to the current pmsg session.
static ssize_t __pmsg_write(const std::string& filename, const std::string& buf) {
  return __android_log_pmsg_file_write(LOG_ID_SYSTEM, ANDROID_LOG_INFO, filename.c_str(),
                                       buf.data(), buf.size());
}

void copy_log_file_to_pmsg(const std::string& source, const std::string& destination) {
  std::string content;
  android::base::ReadFileToString(source, &content);
  __pmsg_write(destination, content);
}

// How much of the temp log we have copied to the copy in cache.
static off_t tmplog_offset = 0;

void reset_tmplog_offset() {
  tmplog_offset = 0;
}

static void copy_log_file(const std::string& source, const std::string& destination, bool append) {
  FILE* dest_fp = fopen_path(destination, append ? "ae" : "we", logging_sehandle);
  if (dest_fp == nullptr) {
    PLOG(ERROR) << "Can't open " << destination;
  } else {
    FILE* source_fp = fopen(source.c_str(), "re");
    if (source_fp != nullptr) {
      if (append) {
        fseeko(source_fp, tmplog_offset, SEEK_SET);  // Since last write
      }
      char buf[4096];
      size_t bytes;
      while ((bytes = fread(buf, 1, sizeof(buf), source_fp)) != 0) {
        fwrite(buf, 1, bytes, dest_fp);
      }
      if (append) {
        tmplog_offset = ftello(source_fp);
      }
      check_and_fclose(source_fp, source);
    }
    check_and_fclose(dest_fp, destination);
  }
}

void copy_logs(bool save_current_log) {
  // We only rotate and record the log of the current session if explicitly requested. This usually
  // happens after wipes, installation from BCB or menu selections. This is to avoid unnecessary
  // rotation (and possible deletion) of log files, if it does not do anything loggable.
  if (!save_current_log) {
    return;
  }

  // Always write to pmsg, this allows the OTA logs to be caught in `logcat -L`.
  copy_log_file_to_pmsg(Paths::Get().temporary_log_file(), LAST_LOG_FILE);
  copy_log_file_to_pmsg(Paths::Get().temporary_install_file(), LAST_INSTALL_FILE);

  // We can do nothing for now if there's no /cache partition.
  if (!HasCache()) {
    return;
  }

  ensure_path_mounted(LAST_LOG_FILE);
  ensure_path_mounted(LAST_KMSG_FILE);
  rotate_logs(LAST_LOG_FILE, LAST_KMSG_FILE);

  // Copy logs to cache so the system can find out what happened.
  copy_log_file(Paths::Get().temporary_log_file(), LOG_FILE, true);
  copy_log_file(Paths::Get().temporary_log_file(), LAST_LOG_FILE, false);
  copy_log_file(Paths::Get().temporary_install_file(), LAST_INSTALL_FILE, false);
  save_kernel_log(LAST_KMSG_FILE);
  chmod(LOG_FILE, 0600);
  chown(LOG_FILE, AID_SYSTEM, AID_SYSTEM);
  chmod(LAST_KMSG_FILE, 0600);
  chown(LAST_KMSG_FILE, AID_SYSTEM, AID_SYSTEM);
  chmod(LAST_LOG_FILE, 0640);
  chmod(LAST_INSTALL_FILE, 0644);
  chown(LAST_INSTALL_FILE, AID_SYSTEM, AID_SYSTEM);
  sync();
}

// Read from kernel log into buffer and write out to file.
void save_kernel_log(const char* destination) {
  int klog_buf_len = klogctl(KLOG_SIZE_BUFFER, 0, 0);
  if (klog_buf_len <= 0) {
    PLOG(ERROR) << "Error getting klog size";
    return;
  }

  std::string buffer(klog_buf_len, 0);
  int n = klogctl(KLOG_READ_ALL, &buffer[0], klog_buf_len);
  if (n == -1) {
    PLOG(ERROR) << "Error in reading klog";
    return;
  }
  buffer.resize(n);
  android::base::WriteStringToFile(buffer, destination);
}

std::vector<saved_log_file> ReadLogFilesToMemory() {
  ensure_path_mounted("/cache");

  struct dirent* de;
  std::unique_ptr<DIR, decltype(&closedir)> d(opendir(CACHE_LOG_DIR), closedir);
  if (!d) {
    if (errno != ENOENT) {
      PLOG(ERROR) << "Failed to opendir " << CACHE_LOG_DIR;
    }
    return {};
  }

  std::vector<saved_log_file> log_files;
  while ((de = readdir(d.get())) != nullptr) {
    if (strncmp(de->d_name, "last_", 5) == 0 || strcmp(de->d_name, "log") == 0) {
      std::string path = android::base::StringPrintf("%s/%s", CACHE_LOG_DIR, de->d_name);

      struct stat sb;
      if (stat(path.c_str(), &sb) != 0) {
        PLOG(ERROR) << "Failed to stat " << path;
        continue;
      }
      // Truncate files to 512kb
      size_t read_size = std::min<size_t>(sb.st_size, 1 << 19);
      std::string data(read_size, '\0');

      android::base::unique_fd log_fd(TEMP_FAILURE_RETRY(open(path.c_str(), O_RDONLY)));
      if (log_fd == -1 || !android::base::ReadFully(log_fd, data.data(), read_size)) {
        PLOG(ERROR) << "Failed to read log file " << path;
        continue;
      }

      log_files.emplace_back(saved_log_file{ path, sb, data });
    }
  }

  return log_files;
}

bool RestoreLogFilesAfterFormat(const std::vector<saved_log_file>& log_files) {
  // Re-create the log dir and write back the log entries.
  if (ensure_path_mounted(CACHE_LOG_DIR) != 0) {
    PLOG(ERROR) << "Failed to mount " << CACHE_LOG_DIR;
    return false;
  }

  if (mkdir_recursively(CACHE_LOG_DIR, 0777, false, logging_sehandle) != 0) {
    PLOG(ERROR) << "Failed to create " << CACHE_LOG_DIR;
    return false;
  }

  for (const auto& log : log_files) {
    if (!android::base::WriteStringToFile(log.data, log.name, log.sb.st_mode, log.sb.st_uid,
                                          log.sb.st_gid)) {
      PLOG(ERROR) << "Failed to write to " << log.name;
    }
  }

  // Any part of the log we'd copied to cache is now gone.
  // Reset the pointer so we copy from the beginning of the temp
  // log.
  reset_tmplog_offset();
  copy_logs(true /* save_current_log */);

  return true;
}
