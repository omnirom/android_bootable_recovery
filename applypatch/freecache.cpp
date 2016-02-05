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

#include <errno.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <unistd.h>
#include <dirent.h>
#include <ctype.h>

#include <memory>
#include <set>
#include <string>

#include <android-base/parseint.h>
#include <android-base/stringprintf.h>

#include "applypatch.h"

static int EliminateOpenFiles(std::set<std::string>* files) {
  std::unique_ptr<DIR, decltype(&closedir)> d(opendir("/proc"), closedir);
  if (!d) {
    printf("error opening /proc: %s\n", strerror(errno));
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
      printf("error opening %s: %s\n", path.c_str(), strerror(errno));
      continue;
    }
    while ((fdde = readdir(fdd.get())) != 0) {
      std::string fd_path = path + fdde->d_name;
      char link[FILENAME_MAX];

      int count = readlink(fd_path.c_str(), link, sizeof(link)-1);
      if (count >= 0) {
        link[count] = '\0';
        if (strncmp(link, "/cache/", 7) == 0) {
          if (files->erase(link) > 0) {
            printf("%s is open by %s\n", link, de->d_name);
          }
        }
      }
    }
  }
  return 0;
}

static std::set<std::string> FindExpendableFiles() {
  std::set<std::string> files;
  // We're allowed to delete unopened regular files in any of these
  // directories.
  const char* dirs[2] = {"/cache", "/cache/recovery/otatest"};

  for (size_t i = 0; i < sizeof(dirs)/sizeof(dirs[0]); ++i) {
    std::unique_ptr<DIR, decltype(&closedir)> d(opendir(dirs[i]), closedir);
    if (!d) {
      printf("error opening %s: %s\n", dirs[i], strerror(errno));
      continue;
    }

    // Look for regular files in the directory (not in any subdirectories).
    struct dirent* de;
    while ((de = readdir(d.get())) != 0) {
      std::string path = std::string(dirs[i]) + "/" + de->d_name;

      // We can't delete CACHE_TEMP_SOURCE; if it's there we might have
      // restarted during installation and could be depending on it to
      // be there.
      if (path == CACHE_TEMP_SOURCE) {
        continue;
      }

      struct stat st;
      if (stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
        files.insert(path);
      }
    }
  }

  printf("%zu regular files in deletable directories\n", files.size());
  if (EliminateOpenFiles(&files) < 0) {
    return std::set<std::string>();
  }
  return files;
}

int MakeFreeSpaceOnCache(size_t bytes_needed) {
  size_t free_now = FreeSpaceForFile("/cache");
  printf("%zu bytes free on /cache (%zu needed)\n", free_now, bytes_needed);

  if (free_now >= bytes_needed) {
    return 0;
  }
  std::set<std::string> files = FindExpendableFiles();
  if (files.empty()) {
    // nothing we can delete to free up space!
    printf("no files can be deleted to free space on /cache\n");
    return -1;
  }

  // We could try to be smarter about which files to delete:  the
  // biggest ones?  the smallest ones that will free up enough space?
  // the oldest?  the newest?
  //
  // Instead, we'll be dumb.

  for (const auto& file : files) {
    unlink(file.c_str());
    free_now = FreeSpaceForFile("/cache");
    printf("deleted %s; now %zu bytes free\n", file.c_str(), free_now);
    if (free_now < bytes_needed) {
        break;
    }
  }
  return (free_now >= bytes_needed) ? 0 : -1;
}
