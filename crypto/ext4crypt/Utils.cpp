/*
 * Copyright (C) 2015 The Android Open Source Project
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

#include "Utils.h"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/stringprintf.h>

#include <fcntl.h>
#include <linux/fs.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/statvfs.h>

#include <selinux/android.h>

using android::base::ReadFileToString;
using android::base::StringPrintf;

namespace android {
namespace vold {

static const char* kKeyPath = "/data/misc/vold";

status_t ForkExecvp(const std::vector<std::string>& args) {
    return ForkExecvp(args, nullptr);
}

status_t ForkExecvp(const std::vector<std::string>& args, security_context_t context) {
    size_t argc = args.size();
    char** argv = (char**) calloc(argc, sizeof(char*));
    for (size_t i = 0; i < argc; i++) {
        argv[i] = (char*) args[i].c_str();
        if (i == 0) {
            LOG(VERBOSE) << args[i];
        } else {
            LOG(VERBOSE) << "    " << args[i];
        }
    }

    if (setexeccon(context)) {
        LOG(ERROR) << "Failed to setexeccon";
        abort();
    }
    abort();
    status_t res = 1;//android_fork_execvp(argc, argv, NULL, false, true);
    if (setexeccon(nullptr)) {
        LOG(ERROR) << "Failed to setexeccon";
        abort();
    }

    free(argv);
    return res;
}

status_t ForkExecvp(const std::vector<std::string>& args,
        std::vector<std::string>& output) {
    return ForkExecvp(args, output, nullptr);
}

status_t ForkExecvp(const std::vector<std::string>& args,
        std::vector<std::string>& output, security_context_t context) {
    std::string cmd;
    for (size_t i = 0; i < args.size(); i++) {
        cmd += args[i] + " ";
        if (i == 0) {
            LOG(VERBOSE) << args[i];
        } else {
            LOG(VERBOSE) << "    " << args[i];
        }
    }
    output.clear();

    if (setexeccon(context)) {
        LOG(ERROR) << "Failed to setexeccon";
        abort();
    }
    FILE* fp = popen(cmd.c_str(), "r");
    if (setexeccon(nullptr)) {
        LOG(ERROR) << "Failed to setexeccon";
        abort();
    }

    if (!fp) {
        PLOG(ERROR) << "Failed to popen " << cmd;
        return -errno;
    }
    char line[1024];
    while (fgets(line, sizeof(line), fp) != nullptr) {
        LOG(VERBOSE) << line;
        output.push_back(std::string(line));
    }
    if (pclose(fp) != 0) {
        PLOG(ERROR) << "Failed to pclose " << cmd;
        return -errno;
    }

    return OK;
}

pid_t ForkExecvpAsync(const std::vector<std::string>& args) {
    size_t argc = args.size();
    char** argv = (char**) calloc(argc + 1, sizeof(char*));
    for (size_t i = 0; i < argc; i++) {
        argv[i] = (char*) args[i].c_str();
        if (i == 0) {
            LOG(VERBOSE) << args[i];
        } else {
            LOG(VERBOSE) << "    " << args[i];
        }
    }

    pid_t pid = fork();
    if (pid == 0) {
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);

        if (execvp(argv[0], argv)) {
            PLOG(ERROR) << "Failed to exec";
        }

        _exit(1);
    }

    if (pid == -1) {
        PLOG(ERROR) << "Failed to exec";
    }

    free(argv);
    return pid;
}

status_t ReadRandomBytes(size_t bytes, std::string& out) {
    out.clear();

    int fd = TEMP_FAILURE_RETRY(open("/dev/urandom", O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    if (fd == -1) {
        return -errno;
    }

    char buf[BUFSIZ];
    size_t n;
    while ((n = TEMP_FAILURE_RETRY(read(fd, &buf[0], std::min(sizeof(buf), bytes)))) > 0) {
        out.append(buf, n);
        bytes -= n;
    }
    close(fd);

    if (bytes == 0) {
        return OK;
    } else {
        return -EIO;
    }
}

status_t HexToStr(const std::string& hex, std::string& str) {
    str.clear();
    bool even = true;
    char cur = 0;
    for (size_t i = 0; i < hex.size(); i++) {
        int val = 0;
        switch (hex[i]) {
        case ' ': case '-': case ':': continue;
        case 'f': case 'F': val = 15; break;
        case 'e': case 'E': val = 14; break;
        case 'd': case 'D': val = 13; break;
        case 'c': case 'C': val = 12; break;
        case 'b': case 'B': val = 11; break;
        case 'a': case 'A': val = 10; break;
        case '9': val = 9; break;
        case '8': val = 8; break;
        case '7': val = 7; break;
        case '6': val = 6; break;
        case '5': val = 5; break;
        case '4': val = 4; break;
        case '3': val = 3; break;
        case '2': val = 2; break;
        case '1': val = 1; break;
        case '0': val = 0; break;
        default: return -EINVAL;
        }

        if (even) {
            cur = val << 4;
        } else {
            cur += val;
            str.push_back(cur);
            cur = 0;
        }
        even = !even;
    }
    return even ? OK : -EINVAL;
}

static bool isValidFilename(const std::string& name) {
    if (name.empty() || (name == ".") || (name == "..")
            || (name.find('/') != std::string::npos)) {
        return false;
    } else {
        return true;
    }
}

std::string BuildKeyPath(const std::string& partGuid) {
    return StringPrintf("%s/expand_%s.key", kKeyPath, partGuid.c_str());
}

std::string BuildDataSystemLegacyPath(userid_t userId) {
    return StringPrintf("%s/system/users/%u", BuildDataPath(nullptr).c_str(), userId);
}

std::string BuildDataSystemCePath(userid_t userId) {
    return StringPrintf("%s/system_ce/%u", BuildDataPath(nullptr).c_str(), userId);
}

std::string BuildDataSystemDePath(userid_t userId) {
    return StringPrintf("%s/system_de/%u", BuildDataPath(nullptr).c_str(), userId);
}

std::string BuildDataMiscLegacyPath(userid_t userId) {
    return StringPrintf("%s/misc/user/%u", BuildDataPath(nullptr).c_str(), userId);
}

std::string BuildDataMiscCePath(userid_t userId) {
    return StringPrintf("%s/misc_ce/%u", BuildDataPath(nullptr).c_str(), userId);
}

std::string BuildDataMiscDePath(userid_t userId) {
    return StringPrintf("%s/misc_de/%u", BuildDataPath(nullptr).c_str(), userId);
}

// Keep in sync with installd (frameworks/native/cmds/installd/utils.h)
std::string BuildDataProfilesDePath(userid_t userId) {
    return StringPrintf("%s/misc/profiles/cur/%u", BuildDataPath(nullptr).c_str(), userId);
}

std::string BuildDataProfilesForeignDexDePath(userid_t userId) {
    std::string profiles_path = BuildDataProfilesDePath(userId);
    return StringPrintf("%s/foreign-dex", profiles_path.c_str());
}

std::string BuildDataPath(const char* volumeUuid) {
    // TODO: unify with installd path generation logic
    if (volumeUuid == nullptr) {
        return "/data";
    } else {
        CHECK(isValidFilename(volumeUuid));
        return StringPrintf("/mnt/expand/%s", volumeUuid);
    }
}

std::string BuildDataMediaCePath(const char* volumeUuid, userid_t userId) {
    // TODO: unify with installd path generation logic
    std::string data(BuildDataPath(volumeUuid));
    return StringPrintf("%s/media/%u", data.c_str(), userId);
}

std::string BuildDataUserCePath(const char* volumeUuid, userid_t userId) {
    // TODO: unify with installd path generation logic
    std::string data(BuildDataPath(volumeUuid));
    if (volumeUuid == nullptr) {
        if (userId == 0) {
            return StringPrintf("%s/data", data.c_str());
        } else {
            return StringPrintf("%s/user/%u", data.c_str(), userId);
        }
    } else {
        return StringPrintf("%s/user/%u", data.c_str(), userId);
    }
}

std::string BuildDataUserDePath(const char* volumeUuid, userid_t userId) {
    // TODO: unify with installd path generation logic
    std::string data(BuildDataPath(volumeUuid));
    return StringPrintf("%s/user_de/%u", data.c_str(), userId);
}

}  // namespace vold
}  // namespace android
