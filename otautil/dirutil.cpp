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

#include "otautil/dirutil.h"

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <string>

#include <selinux/label.h>
#include <selinux/selinux.h>

enum class DirStatus { DMISSING, DDIR, DILLEGAL };

static DirStatus dir_status(const std::string& path) {
  struct stat sb;
  if (stat(path.c_str(), &sb) == 0) {
    // Something's there; make sure it's a directory.
    if (S_ISDIR(sb.st_mode)) {
      return DirStatus::DDIR;
    }
    errno = ENOTDIR;
    return DirStatus::DILLEGAL;
  } else if (errno != ENOENT) {
    // Something went wrong, or something in the path is bad. Can't do anything in this situation.
    return DirStatus::DILLEGAL;
  }
  return DirStatus::DMISSING;
}

int mkdir_recursively(const std::string& input_path, mode_t mode, bool strip_filename,
                      const selabel_handle* sehnd) {
  // Check for an empty string before we bother making any syscalls.
  if (input_path.empty()) {
    errno = ENOENT;
    return -1;
  }

  // Allocate a path that we can modify; stick a slash on the end to make things easier.
  std::string path = input_path;
  if (strip_filename) {
    // Strip everything after the last slash.
    size_t pos = path.rfind('/');
    if (pos == std::string::npos) {
      errno = ENOENT;
      return -1;
    }
    path.resize(pos + 1);
  } else {
    // Make sure that the path ends in a slash.
    path.push_back('/');
  }

  // See if it already exists.
  DirStatus ds = dir_status(path);
  if (ds == DirStatus::DDIR) {
    return 0;
  } else if (ds == DirStatus::DILLEGAL) {
    return -1;
  }

  // Walk up the path from the root and make each level.
  size_t prev_end = 0;
  while (prev_end < path.size()) {
    size_t next_end = path.find('/', prev_end + 1);
    if (next_end == std::string::npos) {
      break;
    }
    std::string dir_path = path.substr(0, next_end);
    // Check this part of the path and make a new directory if necessary.
    switch (dir_status(dir_path)) {
      case DirStatus::DILLEGAL:
        // Could happen if some other process/thread is messing with the filesystem.
        return -1;
      case DirStatus::DMISSING: {
        char* secontext = nullptr;
        if (sehnd) {
          selabel_lookup(const_cast<selabel_handle*>(sehnd), &secontext, dir_path.c_str(), mode);
          setfscreatecon(secontext);
        }
        int err = mkdir(dir_path.c_str(), mode);
        if (secontext) {
          freecon(secontext);
          setfscreatecon(nullptr);
        }
        if (err != 0) {
          return -1;
        }
        break;
      }
      default:
        // Already exists.
        break;
    }
    prev_end = next_end;
  }
  return 0;
}
