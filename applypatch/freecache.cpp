/*
 * Copyright (C) 2010 The Android Open Source Project
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

#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <unistd.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <set>
#include <string>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>

#include "applypatch/applypatch.h"
#include "otautil/paths.h"

static int EliminateOpenFiles(const std::string& dirname, std::set<std::string>* files) {
  std::unique_ptr<DIR, decltype(&closedir)> d(opendir("/proc"), closedir);
  if (!d) {
    PLOG(ERROR) << "Failed to open /proc";
    return -1;
  }
  struct dirent* de;
  while ((de = readdir(d.get())) != 0) {
    unsigned int pid;
    if (!android::base::ParseUint(de->d_name, &pid)) {
        continue;
    }
    std::string path = android::base::StringPrintf("/proc/%s/fd/", de->d_name);

    struct dirent* fdde;
    std::unique_ptr<DIR, decltype(&closedir)> fdd(opendir(path.c_str()), closedir);
    if (!fdd) {
      PLOG(ERROR) << "Failed to open " << path;
      continue;
    }
    while ((fdde = readdir(fdd.get())) != 0) {
      std::string fd_path = path + fdde->d_name;
      char link[FILENAME_MAX];

      int count = readlink(fd_path.c_str(), link, sizeof(link)-1);
      if (count >= 0) {
        link[count] = '\0';
        if (android::base::StartsWith(link, dirname)) {
          if (files->erase(link) > 0) {
            LOG(INFO) << link << " is open by " << de->d_name;
          }
        }
      }
    }
  }
  return 0;
}

static std::vector<std::string> FindExpendableFiles(
    const std::string& dirname, const std::function<bool(const std::string&)>& name_filter) {
  std::unique_ptr<DIR, decltype(&closedir)> d(opendir(dirname.c_str()), closedir);
  if (!d) {
    PLOG(ERROR) << "Failed to open " << dirname;
    return {};
  }

  // Look for regular files in the directory (not in any subdirectories).
  std::set<std::string> files;
  struct dirent* de;
  while ((de = readdir(d.get())) != 0) {
    std::string path = dirname + "/" + de->d_name;

    // We can't delete cache_temp_source; if it's there we might have restarted during
    // installation and could be depending on it to be there.
    if (path == Paths::Get().cache_temp_source()) {
      continue;
    }

    // Do not delete the file if it doesn't have the expected format.
    if (name_filter != nullptr && !name_filter(de->d_name)) {
      continue;
    }

    struct stat st;
    if (stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
      files.insert(path);
    }
  }

  LOG(INFO) << files.size() << " regular files in deletable directory";
  if (EliminateOpenFiles(dirname, &files) < 0) {
    return {};
  }

  return std::vector<std::string>(files.begin(), files.end());
}

// Parses the index of given log file, e.g. 3 for last_log.3; returns max number if the log name
// doesn't have the expected format so that we'll delete these ones first.
static unsigned int GetLogIndex(const std::string& log_name) {
  if (log_name == "last_log" || log_name == "last_kmsg") {
    return 0;
  }

  unsigned int index;
  if (sscanf(log_name.c_str(), "last_log.%u", &index) == 1 ||
      sscanf(log_name.c_str(), "last_kmsg.%u", &index) == 1) {
    return index;
  }

  return std::numeric_limits<unsigned int>::max();
}

// Returns the amount of free space (in bytes) on the filesystem containing filename, or -1 on
// error.
static int64_t FreeSpaceForFile(const std::string& filename) {
  struct statfs sf;
  if (statfs(filename.c_str(), &sf) == -1) {
    PLOG(ERROR) << "Failed to statfs " << filename;
    return -1;
  }

  auto f_bsize = static_cast<int64_t>(sf.f_bsize);
  auto free_space = sf.f_bsize * sf.f_bavail;
  if (f_bsize == 0 || free_space / f_bsize != static_cast<int64_t>(sf.f_bavail)) {
    LOG(ERROR) << "Invalid block size or overflow (sf.f_bsize " << sf.f_bsize << ", sf.f_bavail "
               << sf.f_bavail << ")";
    return -1;
  }
  return free_space;
}

bool CheckAndFreeSpaceOnCache(size_t bytes) {
#ifndef __ANDROID__
  // TODO(xunchang): Implement a heuristic cache size check during host simulation.
  LOG(WARNING) << "Skipped making (" << bytes
               << ") bytes free space on /cache; program is running on host";
  return true;
#endif

  std::vector<std::string> dirs{ "/cache", Paths::Get().cache_log_directory() };
  for (const auto& dirname : dirs) {
    if (RemoveFilesInDirectory(bytes, dirname, FreeSpaceForFile)) {
      return true;
    }
  }

  return false;
}

bool RemoveFilesInDirectory(size_t bytes_needed, const std::string& dirname,
                            const std::function<int64_t(const std::string&)>& space_checker) {
  // The requested size cannot exceed max int64_t.
  if (static_cast<uint64_t>(bytes_needed) >
      static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
    LOG(ERROR) << "Invalid arg of bytes_needed: " << bytes_needed;
    return false;
  }

  struct stat st;
  if (stat(dirname.c_str(), &st) == -1) {
    PLOG(ERROR) << "Failed to stat " << dirname;
    return false;
  }
  if (!S_ISDIR(st.st_mode)) {
    LOG(ERROR) << dirname << " is not a directory";
    return false;
  }

  int64_t free_now = space_checker(dirname);
  if (free_now == -1) {
    LOG(ERROR) << "Failed to check free space for " << dirname;
    return false;
  }
  LOG(INFO) << free_now << " bytes free on " << dirname << " (" << bytes_needed << " needed)";

  if (free_now >= static_cast<int64_t>(bytes_needed)) {
    return true;
  }

  std::vector<std::string> files;
  if (dirname == Paths::Get().cache_log_directory()) {
    // Deletes the log files only.
    auto log_filter = [](const std::string& file_name) {
      return android::base::StartsWith(file_name, "last_log") ||
             android::base::StartsWith(file_name, "last_kmsg");
    };

    files = FindExpendableFiles(dirname, log_filter);

    // Older logs will come to the top of the queue.
    auto comparator = [](const std::string& name1, const std::string& name2) -> bool {
      unsigned int index1 = GetLogIndex(android::base::Basename(name1));
      unsigned int index2 = GetLogIndex(android::base::Basename(name2));
      if (index1 == index2) {
        return name1 < name2;
      }

      return index1 > index2;
    };

    std::sort(files.begin(), files.end(), comparator);
  } else {
    // We're allowed to delete unopened regular files in the directory.
    files = FindExpendableFiles(dirname, nullptr);
  }

  for (const auto& file : files) {
    if (unlink(file.c_str()) == -1) {
      PLOG(ERROR) << "Failed to delete " << file;
      continue;
    }

    free_now = space_checker(dirname);
    if (free_now == -1) {
      LOG(ERROR) << "Failed to check free space for " << dirname;
      return false;
    }
    LOG(INFO) << "Deleted " << file << "; now " << free_now << " bytes free";
    if (free_now >= static_cast<int64_t>(bytes_needed)) {
      return true;
    }
  }

  return false;
}
