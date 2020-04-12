/*
 * Copyright (C) 2008 The Android Open Source Project
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
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fts.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fstream>
#include <unordered_set>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>

#include "Process.h"

using android::base::StringPrintf;

namespace android {
namespace vold {

static bool checkMaps(const std::string& path, const std::string& prefix) {
    bool found = false;
    auto file = std::unique_ptr<FILE, decltype(&fclose)>{fopen(path.c_str(), "re"), fclose};
    if (!file) {
        return false;
    }

    char* buf = nullptr;
    size_t len = 0;
    while (getline(&buf, &len, file.get()) != -1) {
        std::string line(buf);
        std::string::size_type pos = line.find('/');
        if (pos != std::string::npos) {
            line = line.substr(pos);
            if (android::base::StartsWith(line, prefix)) {
                LOG(WARNING) << "Found map " << path << " referencing " << line;
                found = true;
                break;
            }
        }
    }
    free(buf);

    return found;
}

static bool checkSymlink(const std::string& path, const std::string& prefix) {
    std::string res;
    if (android::base::Readlink(path, &res)) {
        if (android::base::StartsWith(res, prefix)) {
            LOG(WARNING) << "Found symlink " << path << " referencing " << res;
            return true;
        }
    }
    return false;
}

int KillProcessesWithOpenFiles(const std::string& prefix, int signal) {
    std::unordered_set<pid_t> pids;

    auto proc_d = std::unique_ptr<DIR, int (*)(DIR*)>(opendir("/proc"), closedir);
    if (!proc_d) {
        PLOG(ERROR) << "Failed to open proc";
        return -1;
    }

    struct dirent* proc_de;
    while ((proc_de = readdir(proc_d.get())) != nullptr) {
        // We only care about valid PIDs
        pid_t pid;
        if (proc_de->d_type != DT_DIR) continue;
        if (!android::base::ParseInt(proc_de->d_name, &pid)) continue;

        // Look for references to prefix
        bool found = false;
        auto path = StringPrintf("/proc/%d", pid);
        found |= checkMaps(path + "/maps", prefix);
        found |= checkSymlink(path + "/cwd", prefix);
        found |= checkSymlink(path + "/root", prefix);
        found |= checkSymlink(path + "/exe", prefix);

        auto fd_path = path + "/fd";
        auto fd_d = std::unique_ptr<DIR, int (*)(DIR*)>(opendir(fd_path.c_str()), closedir);
        if (!fd_d) {
            PLOG(WARNING) << "Failed to open " << fd_path;
        } else {
            struct dirent* fd_de;
            while ((fd_de = readdir(fd_d.get())) != nullptr) {
                if (fd_de->d_type != DT_LNK) continue;
                found |= checkSymlink(fd_path + "/" + fd_de->d_name, prefix);
            }
        }

        if (found) {
            pids.insert(pid);
        }
    }
    if (signal != 0) {
        for (const auto& pid : pids) {
            LOG(WARNING) << "Sending " << strsignal(signal) << " to " << pid;
            kill(pid, signal);
        }
    }
    return pids.size();
}

}  // namespace vold
}  // namespace android
