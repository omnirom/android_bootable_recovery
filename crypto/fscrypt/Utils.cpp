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

#include "Process.h"
#include "sehandle.h"

#include <android-base/chrono_utils.h>
#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#include <android-base/unique_fd.h>
#include <cutils/fs.h>
#include <logwrap/logwrap.h>
#include <private/android_filesystem_config.h>

#include <dirent.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <mntent.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <list>
#include <mutex>
#include <thread>

#ifndef UMOUNT_NOFOLLOW
#define UMOUNT_NOFOLLOW 0x00000008 /* Don't follow symlink on umount */
#endif

using namespace std::chrono_literals;
using android::base::ReadFileToString;
using android::base::StringPrintf;

namespace android {
namespace vold {

security_context_t sBlkidContext = nullptr;
security_context_t sBlkidUntrustedContext = nullptr;
security_context_t sFsckContext = nullptr;
security_context_t sFsckUntrustedContext = nullptr;
struct selabel_handle* sehandle;

bool sSleepOnUnmount = true;

static const char* kBlkidPath = "/system/bin/blkid";
static const char* kKeyPath = "/data/misc/vold";

static const char* kProcFilesystems = "/proc/filesystems";

// Lock used to protect process-level SELinux changes from racing with each
// other between multiple threads.
static std::mutex kSecurityLock;

status_t CreateDeviceNode(const std::string& path, dev_t dev) {
    std::lock_guard<std::mutex> lock(kSecurityLock);
    const char* cpath = path.c_str();
    status_t res = 0;

    char* secontext = nullptr;
    if (sehandle) {
        if (!selabel_lookup(sehandle, &secontext, cpath, S_IFBLK)) {
            setfscreatecon(secontext);
        }
    }

    mode_t mode = 0660 | S_IFBLK;
    if (mknod(cpath, mode, dev) < 0) {
        if (errno != EEXIST) {
            PLOG(ERROR) << "Failed to create device node for " << major(dev) << ":" << minor(dev)
                        << " at " << path;
            res = -errno;
        }
    }

    if (secontext) {
        setfscreatecon(nullptr);
        freecon(secontext);
    }

    return res;
}

status_t DestroyDeviceNode(const std::string& path) {
    const char* cpath = path.c_str();
    if (TEMP_FAILURE_RETRY(unlink(cpath))) {
        return -errno;
    } else {
        return OK;
    }
}

status_t PrepareDir(const std::string& path, mode_t mode, uid_t uid, gid_t gid) {
    std::lock_guard<std::mutex> lock(kSecurityLock);
    const char* cpath = path.c_str();

    char* secontext = nullptr;
    if (sehandle) {
        if (!selabel_lookup(sehandle, &secontext, cpath, S_IFDIR)) {
            setfscreatecon(secontext);
        }
    }

    int res = fs_prepare_dir(cpath, mode, uid, gid);

    if (secontext) {
        setfscreatecon(nullptr);
        freecon(secontext);
    }

    if (res == 0) {
        return OK;
    } else {
        return -errno;
    }
}

status_t ForceUnmount(const std::string& path) {
    const char* cpath = path.c_str();
    if (!umount2(cpath, UMOUNT_NOFOLLOW) || errno == EINVAL || errno == ENOENT) {
        return OK;
    }
    // Apps might still be handling eject request, so wait before
    // we start sending signals
    if (sSleepOnUnmount) sleep(5);

    KillProcessesWithOpenFiles(path, SIGINT);
    if (sSleepOnUnmount) sleep(5);
    if (!umount2(cpath, UMOUNT_NOFOLLOW) || errno == EINVAL || errno == ENOENT) {
        return OK;
    }

    KillProcessesWithOpenFiles(path, SIGTERM);
    if (sSleepOnUnmount) sleep(5);
    if (!umount2(cpath, UMOUNT_NOFOLLOW) || errno == EINVAL || errno == ENOENT) {
        return OK;
    }

    KillProcessesWithOpenFiles(path, SIGKILL);
    if (sSleepOnUnmount) sleep(5);
    if (!umount2(cpath, UMOUNT_NOFOLLOW) || errno == EINVAL || errno == ENOENT) {
        return OK;
    }

    return -errno;
}

status_t KillProcessesUsingPath(const std::string& path) {
    if (KillProcessesWithOpenFiles(path, SIGINT) == 0) {
        return OK;
    }
    if (sSleepOnUnmount) sleep(5);

    if (KillProcessesWithOpenFiles(path, SIGTERM) == 0) {
        return OK;
    }
    if (sSleepOnUnmount) sleep(5);

    if (KillProcessesWithOpenFiles(path, SIGKILL) == 0) {
        return OK;
    }
    if (sSleepOnUnmount) sleep(5);

    // Send SIGKILL a second time to determine if we've
    // actually killed everyone with open files
    if (KillProcessesWithOpenFiles(path, SIGKILL) == 0) {
        return OK;
    }
    PLOG(ERROR) << "Failed to kill processes using " << path;
    return -EBUSY;
}

status_t BindMount(const std::string& source, const std::string& target) {
    if (UnmountTree(target) < 0) {
        return -errno;
    }
    if (TEMP_FAILURE_RETRY(mount(source.c_str(), target.c_str(), nullptr, MS_BIND, nullptr)) < 0) {
        PLOG(ERROR) << "Failed to bind mount " << source << " to " << target;
        return -errno;
    }
    return OK;
}

status_t Symlink(const std::string& target, const std::string& linkpath) {
    if (Unlink(linkpath) < 0) {
        return -errno;
    }
    if (TEMP_FAILURE_RETRY(symlink(target.c_str(), linkpath.c_str())) < 0) {
        PLOG(ERROR) << "Failed to create symlink " << linkpath << " to " << target;
        return -errno;
    }
    return OK;
}

status_t Unlink(const std::string& linkpath) {
    if (TEMP_FAILURE_RETRY(unlink(linkpath.c_str())) < 0 && errno != EINVAL && errno != ENOENT) {
        PLOG(ERROR) << "Failed to unlink " << linkpath;
        return -errno;
    }
    return OK;
}

status_t CreateDir(const std::string& dir, mode_t mode) {
    struct stat sb;
    if (TEMP_FAILURE_RETRY(stat(dir.c_str(), &sb)) == 0) {
        if (S_ISDIR(sb.st_mode)) {
            return OK;
        } else if (TEMP_FAILURE_RETRY(unlink(dir.c_str())) == -1) {
            PLOG(ERROR) << "Failed to unlink " << dir;
            return -errno;
        }
    } else if (errno != ENOENT) {
        PLOG(ERROR) << "Failed to stat " << dir;
        return -errno;
    }
    if (TEMP_FAILURE_RETRY(mkdir(dir.c_str(), mode)) == -1 && errno != EEXIST) {
        PLOG(ERROR) << "Failed to mkdir " << dir;
        return -errno;
    }
    return OK;
}

bool FindValue(const std::string& raw, const std::string& key, std::string* value) {
    auto qual = key + "=\"";
    size_t start = 0;
    while (true) {
        start = raw.find(qual, start);
        if (start == std::string::npos) return false;
        if (start == 0 || raw[start - 1] == ' ') {
            break;
        }
        start += 1;
    }
    start += qual.length();

    auto end = raw.find("\"", start);
    if (end == std::string::npos) return false;

    *value = raw.substr(start, end - start);
    return true;
}

static status_t readMetadata(const std::string& path, std::string* fsType, std::string* fsUuid,
                             std::string* fsLabel, bool untrusted) {
    fsType->clear();
    fsUuid->clear();
    fsLabel->clear();

    std::vector<std::string> cmd;
    cmd.push_back(kBlkidPath);
    cmd.push_back("-c");
    cmd.push_back("/dev/null");
    cmd.push_back("-s");
    cmd.push_back("TYPE");
    cmd.push_back("-s");
    cmd.push_back("UUID");
    cmd.push_back("-s");
    cmd.push_back("LABEL");
    cmd.push_back(path);

    std::vector<std::string> output;
    status_t res = ForkExecvp(cmd, &output, untrusted ? sBlkidUntrustedContext : sBlkidContext);
    if (res != OK) {
        LOG(WARNING) << "blkid failed to identify " << path;
        return res;
    }

    for (const auto& line : output) {
        // Extract values from blkid output, if defined
        FindValue(line, "TYPE", fsType);
        FindValue(line, "UUID", fsUuid);
        FindValue(line, "LABEL", fsLabel);
    }

    return OK;
}

status_t ReadMetadata(const std::string& path, std::string* fsType, std::string* fsUuid,
                      std::string* fsLabel) {
    return readMetadata(path, fsType, fsUuid, fsLabel, false);
}

status_t ReadMetadataUntrusted(const std::string& path, std::string* fsType, std::string* fsUuid,
                               std::string* fsLabel) {
    return readMetadata(path, fsType, fsUuid, fsLabel, true);
}

static std::vector<const char*> ConvertToArgv(const std::vector<std::string>& args) {
    std::vector<const char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& arg : args) {
        if (argv.empty()) {
            LOG(DEBUG) << arg;
        } else {
            LOG(DEBUG) << "    " << arg;
        }
        argv.emplace_back(arg.data());
    }
    argv.emplace_back(nullptr);
    return argv;
}

static status_t ReadLinesFromFdAndLog(std::vector<std::string>* output,
                                      android::base::unique_fd ufd) {
    std::unique_ptr<FILE, int (*)(FILE*)> fp(android::base::Fdopen(std::move(ufd), "r"), fclose);
    if (!fp) {
        PLOG(ERROR) << "fdopen in ReadLinesFromFdAndLog";
        return -errno;
    }
    if (output) output->clear();
    char line[1024];
    while (fgets(line, sizeof(line), fp.get()) != nullptr) {
        LOG(DEBUG) << line;
        if (output) output->emplace_back(line);
    }
    return OK;
}

status_t ForkExecvp(const std::vector<std::string>& args, std::vector<std::string>* output,
                    security_context_t context) {
    auto argv = ConvertToArgv(args);

    android::base::unique_fd pipe_read, pipe_write;
    if (!android::base::Pipe(&pipe_read, &pipe_write)) {
        PLOG(ERROR) << "Pipe in ForkExecvp";
        return -errno;
    }

    pid_t pid = fork();
    if (pid == 0) {
        if (context) {
            if (setexeccon(context)) {
                LOG(ERROR) << "Failed to setexeccon in ForkExecvp";
                abort();
            }
        }
        pipe_read.reset();
        if (dup2(pipe_write.get(), STDOUT_FILENO) == -1) {
            PLOG(ERROR) << "dup2 in ForkExecvp";
            _exit(EXIT_FAILURE);
        }
        pipe_write.reset();
        execvp(argv[0], const_cast<char**>(argv.data()));
        PLOG(ERROR) << "exec in ForkExecvp" << " cmd: " << argv[0];
        _exit(EXIT_FAILURE);
    }
    if (pid == -1) {
        PLOG(ERROR) << "fork in ForkExecvp";
        return -errno;
    }

    pipe_write.reset();
    auto st = ReadLinesFromFdAndLog(output, std::move(pipe_read));
    if (st != 0) return st;

    int status;
    if (waitpid(pid, &status, 0) == -1) {
        PLOG(ERROR) << "waitpid in ForkExecvp";
        return -errno;
    }
    if (!WIFEXITED(status)) {
        LOG(ERROR) << "Process did not exit normally, status: " << status;
        return -ECHILD;
    }
    if (WEXITSTATUS(status)) {
        LOG(ERROR) << "Process exited with code: " << WEXITSTATUS(status);
        return WEXITSTATUS(status);
    }
    return OK;
}

pid_t ForkExecvpAsync(const std::vector<std::string>& args) {
    auto argv = ConvertToArgv(args);

    pid_t pid = fork();
    if (pid == 0) {
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);

        execvp(argv[0], const_cast<char**>(argv.data()));
        PLOG(ERROR) << "exec in ForkExecvpAsync";
        _exit(EXIT_FAILURE);
    }
    if (pid == -1) {
        PLOG(ERROR) << "fork in ForkExecvpAsync";
        return -1;
    }
    return pid;
}

status_t ReadRandomBytes(size_t bytes, std::string& out) {
    out.resize(bytes);
    return ReadRandomBytes(bytes, &out[0]);
}

status_t ReadRandomBytes(size_t bytes, char* buf) {
    int fd = TEMP_FAILURE_RETRY(open("/dev/urandom", O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    if (fd == -1) {
        return -errno;
    }

    ssize_t n;
    while ((n = TEMP_FAILURE_RETRY(read(fd, &buf[0], bytes))) > 0) {
        bytes -= n;
        buf += n;
    }
    close(fd);

    if (bytes == 0) {
        return OK;
    } else {
        return -EIO;
    }
}

status_t GenerateRandomUuid(std::string& out) {
    status_t res = ReadRandomBytes(16, out);
    if (res == OK) {
        out[6] &= 0x0f; /* clear version        */
        out[6] |= 0x40; /* set to version 4     */
        out[8] &= 0x3f; /* clear variant        */
        out[8] |= 0x80; /* set to IETF variant  */
    }
    return res;
}

status_t HexToStr(const std::string& hex, std::string& str) {
    str.clear();
    bool even = true;
    char cur = 0;
    for (size_t i = 0; i < hex.size(); i++) {
        int val = 0;
        switch (hex[i]) {
            // clang-format off
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
                // clang-format on
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

static const char* kLookup = "0123456789abcdef";

status_t StrToHex(const std::string& str, std::string& hex) {
    hex.clear();
    for (size_t i = 0; i < str.size(); i++) {
        hex.push_back(kLookup[(str[i] & 0xF0) >> 4]);
        hex.push_back(kLookup[str[i] & 0x0F]);
    }
    return OK;
}

status_t StrToHex(const KeyBuffer& str, KeyBuffer& hex) {
    hex.clear();
    for (size_t i = 0; i < str.size(); i++) {
        hex.push_back(kLookup[(str.data()[i] & 0xF0) >> 4]);
        hex.push_back(kLookup[str.data()[i] & 0x0F]);
    }
    return OK;
}

status_t NormalizeHex(const std::string& in, std::string& out) {
    std::string tmp;
    if (HexToStr(in, tmp)) {
        return -EINVAL;
    }
    return StrToHex(tmp, out);
}

status_t GetBlockDevSize(int fd, uint64_t* size) {
    if (ioctl(fd, BLKGETSIZE64, size)) {
        return -errno;
    }

    return OK;
}

status_t GetBlockDevSize(const std::string& path, uint64_t* size) {
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    status_t res = OK;

    if (fd < 0) {
        return -errno;
    }

    res = GetBlockDevSize(fd, size);

    close(fd);

    return res;
}

status_t GetBlockDev512Sectors(const std::string& path, uint64_t* nr_sec) {
    uint64_t size;
    status_t res = GetBlockDevSize(path, &size);

    if (res != OK) {
        return res;
    }

    *nr_sec = size / 512;

    return OK;
}

uint64_t GetFreeBytes(const std::string& path) {
    struct statvfs sb;
    if (statvfs(path.c_str(), &sb) == 0) {
        return (uint64_t)sb.f_bavail * sb.f_frsize;
    } else {
        return -1;
    }
}

// TODO: borrowed from frameworks/native/libs/diskusage/ which should
// eventually be migrated into system/
static int64_t stat_size(struct stat* s) {
    int64_t blksize = s->st_blksize;
    // count actual blocks used instead of nominal file size
    int64_t size = s->st_blocks * 512;

    if (blksize) {
        /* round up to filesystem block size */
        size = (size + blksize - 1) & (~(blksize - 1));
    }

    return size;
}

// TODO: borrowed from frameworks/native/libs/diskusage/ which should
// eventually be migrated into system/
int64_t calculate_dir_size(int dfd) {
    int64_t size = 0;
    struct stat s;
    DIR* d;
    struct dirent* de;

    d = fdopendir(dfd);
    if (d == NULL) {
        close(dfd);
        return 0;
    }

    while ((de = readdir(d))) {
        const char* name = de->d_name;
        if (fstatat(dfd, name, &s, AT_SYMLINK_NOFOLLOW) == 0) {
            size += stat_size(&s);
        }
        if (de->d_type == DT_DIR) {
            int subfd;

            /* always skip "." and ".." */
            if (name[0] == '.') {
                if (name[1] == 0) continue;
                if ((name[1] == '.') && (name[2] == 0)) continue;
            }

            subfd = openat(dfd, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            if (subfd >= 0) {
                size += calculate_dir_size(subfd);
            }
        }
    }
    closedir(d);
    return size;
}

uint64_t GetTreeBytes(const std::string& path) {
    int dirfd = open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dirfd < 0) {
        PLOG(WARNING) << "Failed to open " << path;
        return -1;
    } else {
        return calculate_dir_size(dirfd);
    }
}

bool IsFilesystemSupported(const std::string& fsType) {
    std::string supported;
    if (!ReadFileToString(kProcFilesystems, &supported)) {
        PLOG(ERROR) << "Failed to read supported filesystems";
        return false;
    }
    return supported.find(fsType + "\n") != std::string::npos;
}

status_t WipeBlockDevice(const std::string& path) {
    status_t res = -1;
    const char* c_path = path.c_str();
    uint64_t range[2] = {0, 0};

    int fd = TEMP_FAILURE_RETRY(open(c_path, O_RDWR | O_CLOEXEC));
    if (fd == -1) {
        PLOG(ERROR) << "Failed to open " << path;
        goto done;
    }

    if (GetBlockDevSize(fd, &range[1]) != OK) {
        PLOG(ERROR) << "Failed to determine size of " << path;
        goto done;
    }

    LOG(INFO) << "About to discard " << range[1] << " on " << path;
    if (ioctl(fd, BLKDISCARD, &range) == 0) {
        LOG(INFO) << "Discard success on " << path;
        res = 0;
    } else {
        PLOG(ERROR) << "Discard failure on " << path;
    }

done:
    close(fd);
    return res;
}

static bool isValidFilename(const std::string& name) {
    if (name.empty() || (name == ".") || (name == "..") || (name.find('/') != std::string::npos)) {
        return false;
    } else {
        return true;
    }
}

std::string BuildKeyPath(const std::string& partGuid) {
    return StringPrintf("%s/expand_%s.key", kKeyPath, partGuid.c_str());
}

std::string BuildDataSystemLegacyPath(userid_t userId) {
    return StringPrintf("%s/system/users/%u", BuildDataPath("").c_str(), userId);
}

std::string BuildDataSystemCePath(userid_t userId) {
    return StringPrintf("%s/system_ce/%u", BuildDataPath("").c_str(), userId);
}

std::string BuildDataSystemDePath(userid_t userId) {
    return StringPrintf("%s/system_de/%u", BuildDataPath("").c_str(), userId);
}

std::string BuildDataMiscLegacyPath(userid_t userId) {
    return StringPrintf("%s/misc/user/%u", BuildDataPath("").c_str(), userId);
}

std::string BuildDataMiscCePath(userid_t userId) {
    return StringPrintf("%s/misc_ce/%u", BuildDataPath("").c_str(), userId);
}

std::string BuildDataMiscDePath(userid_t userId) {
    return StringPrintf("%s/misc_de/%u", BuildDataPath("").c_str(), userId);
}

// Keep in sync with installd (frameworks/native/cmds/installd/utils.h)
std::string BuildDataProfilesDePath(userid_t userId) {
    return StringPrintf("%s/misc/profiles/cur/%u", BuildDataPath("").c_str(), userId);
}

std::string BuildDataVendorCePath(userid_t userId) {
    return StringPrintf("%s/vendor_ce/%u", BuildDataPath("").c_str(), userId);
}

std::string BuildDataVendorDePath(userid_t userId) {
    return StringPrintf("%s/vendor_de/%u", BuildDataPath("").c_str(), userId);
}

std::string BuildDataPath(const std::string& volumeUuid) {
    // TODO: unify with installd path generation logic
    if (volumeUuid.empty()) {
        return "/data";
    } else {
        CHECK(isValidFilename(volumeUuid));
        return StringPrintf("/mnt/expand/%s", volumeUuid.c_str());
    }
}

std::string BuildDataMediaCePath(const std::string& volumeUuid, userid_t userId) {
    // TODO: unify with installd path generation logic
    std::string data(BuildDataPath(volumeUuid));
    return StringPrintf("%s/media/%u", data.c_str(), userId);
}

std::string BuildDataUserCePath(const std::string& volumeUuid, userid_t userId) {
    // TODO: unify with installd path generation logic
    std::string data(BuildDataPath(volumeUuid));
    if (volumeUuid.empty() && userId == 0) {
        std::string legacy = StringPrintf("%s/data", data.c_str());
        struct stat sb;
        if (lstat(legacy.c_str(), &sb) == 0 && S_ISDIR(sb.st_mode)) {
            /* /data/data is dir, return /data/data for legacy system */
            return legacy;
        }
    }
    return StringPrintf("%s/user/%u", data.c_str(), userId);
}

std::string BuildDataUserDePath(const std::string& volumeUuid, userid_t userId) {
    // TODO: unify with installd path generation logic
    std::string data(BuildDataPath(volumeUuid));
    return StringPrintf("%s/user_de/%u", data.c_str(), userId);
}

dev_t GetDevice(const std::string& path) {
    struct stat sb;
    if (stat(path.c_str(), &sb)) {
        PLOG(WARNING) << "Failed to stat " << path;
        return 0;
    } else {
        return sb.st_dev;
    }
}

status_t RestoreconRecursive(const std::string& path) {
    LOG(DEBUG) << "Starting restorecon of " << path;

    static constexpr const char* kRestoreconString = "selinux.restorecon_recursive";

    android::base::SetProperty(kRestoreconString, "");
    android::base::SetProperty(kRestoreconString, path);

    android::base::WaitForProperty(kRestoreconString, path);

    LOG(DEBUG) << "Finished restorecon of " << path;
    return OK;
}

bool Readlinkat(int dirfd, const std::string& path, std::string* result) {
    // Shamelessly borrowed from android::base::Readlink()
    result->clear();

    // Most Linux file systems (ext2 and ext4, say) limit symbolic links to
    // 4095 bytes. Since we'll copy out into the string anyway, it doesn't
    // waste memory to just start there. We add 1 so that we can recognize
    // whether it actually fit (rather than being truncated to 4095).
    std::vector<char> buf(4095 + 1);
    while (true) {
        ssize_t size = readlinkat(dirfd, path.c_str(), &buf[0], buf.size());
        // Unrecoverable error?
        if (size == -1) return false;
        // It fit! (If size == buf.size(), it may have been truncated.)
        if (static_cast<size_t>(size) < buf.size()) {
            result->assign(&buf[0], size);
            return true;
        }
        // Double our buffer and try again.
        buf.resize(buf.size() * 2);
    }
}

bool IsRunningInEmulator() {
    return android::base::GetBoolProperty("ro.kernel.qemu", false);
}

static status_t findMountPointsWithPrefix(const std::string& prefix,
                                          std::list<std::string>& mountPoints) {
    // Add a trailing slash if the client didn't provide one so that we don't match /foo/barbaz
    // when the prefix is /foo/bar
    std::string prefixWithSlash(prefix);
    if (prefix.back() != '/') {
        android::base::StringAppendF(&prefixWithSlash, "/");
    }

    std::unique_ptr<FILE, int (*)(FILE*)> mnts(setmntent("/proc/mounts", "re"), endmntent);
    if (!mnts) {
        PLOG(ERROR) << "Unable to open /proc/mounts";
        return -errno;
    }

    // Some volumes can be stacked on each other, so force unmount in
    // reverse order to give us the best chance of success.
    struct mntent* mnt;  // getmntent returns a thread local, so it's safe.
    while ((mnt = getmntent(mnts.get())) != nullptr) {
        auto mountPoint = std::string(mnt->mnt_dir) + "/";
        if (android::base::StartsWith(mountPoint, prefixWithSlash)) {
            mountPoints.push_front(mountPoint);
        }
    }
    return OK;
}

// Unmount all mountpoints that start with prefix. prefix itself doesn't need to be a mountpoint.
status_t UnmountTreeWithPrefix(const std::string& prefix) {
    std::list<std::string> toUnmount;
    status_t result = findMountPointsWithPrefix(prefix, toUnmount);
    if (result < 0) {
        return result;
    }
    for (const auto& path : toUnmount) {
        if (umount2(path.c_str(), MNT_DETACH)) {
            PLOG(ERROR) << "Failed to unmount " << path;
            result = -errno;
        }
    }
    return result;
}

status_t UnmountTree(const std::string& mountPoint) {
    if (TEMP_FAILURE_RETRY(umount2(mountPoint.c_str(), MNT_DETACH)) < 0 && errno != EINVAL &&
        errno != ENOENT) {
        PLOG(ERROR) << "Failed to unmount " << mountPoint;
        return -errno;
    }
    return OK;
}

static status_t delete_dir_contents(DIR* dir) {
    // Shamelessly borrowed from android::installd
    int dfd = dirfd(dir);
    if (dfd < 0) {
        return -errno;
    }

    status_t result = OK;
    struct dirent* de;
    while ((de = readdir(dir))) {
        const char* name = de->d_name;
        if (de->d_type == DT_DIR) {
            /* always skip "." and ".." */
            if (name[0] == '.') {
                if (name[1] == 0) continue;
                if ((name[1] == '.') && (name[2] == 0)) continue;
            }

            android::base::unique_fd subfd(
                openat(dfd, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
            if (subfd.get() == -1) {
                PLOG(ERROR) << "Couldn't openat " << name;
                result = -errno;
                continue;
            }
            std::unique_ptr<DIR, decltype(&closedir)> subdirp(
                android::base::Fdopendir(std::move(subfd)), closedir);
            if (!subdirp) {
                PLOG(ERROR) << "Couldn't fdopendir " << name;
                result = -errno;
                continue;
            }
            result = delete_dir_contents(subdirp.get());
            if (unlinkat(dfd, name, AT_REMOVEDIR) < 0) {
                PLOG(ERROR) << "Couldn't unlinkat " << name;
                result = -errno;
            }
        } else {
            if (unlinkat(dfd, name, 0) < 0) {
                PLOG(ERROR) << "Couldn't unlinkat " << name;
                result = -errno;
            }
        }
    }
    return result;
}

status_t DeleteDirContentsAndDir(const std::string& pathname) {
    status_t res = DeleteDirContents(pathname);
    if (res < 0) {
        return res;
    }
    if (TEMP_FAILURE_RETRY(rmdir(pathname.c_str())) < 0 && errno != ENOENT) {
        PLOG(ERROR) << "rmdir failed on " << pathname;
        return -errno;
    }
    LOG(VERBOSE) << "Success: rmdir on " << pathname;
    return OK;
}

status_t DeleteDirContents(const std::string& pathname) {
    // Shamelessly borrowed from android::installd
    std::unique_ptr<DIR, decltype(&closedir)> dirp(opendir(pathname.c_str()), closedir);
    if (!dirp) {
        if (errno == ENOENT) {
            return OK;
        }
        PLOG(ERROR) << "Failed to opendir " << pathname;
        return -errno;
    }
    return delete_dir_contents(dirp.get());
}

// TODO(118708649): fix duplication with init/util.h
status_t WaitForFile(const char* filename, std::chrono::nanoseconds timeout) {
    android::base::Timer t;
    while (t.duration() < timeout) {
        struct stat sb;
        if (stat(filename, &sb) != -1) {
            LOG(INFO) << "wait for '" << filename << "' took " << t;
            return 0;
        }
        std::this_thread::sleep_for(10ms);
    }
    LOG(WARNING) << "wait for '" << filename << "' timed out and took " << t;
    return -1;
}

bool FsyncDirectory(const std::string& dirname) {
    android::base::unique_fd fd(TEMP_FAILURE_RETRY(open(dirname.c_str(), O_RDONLY | O_CLOEXEC)));
    if (fd == -1) {
        PLOG(ERROR) << "Failed to open " << dirname;
        return false;
    }
    if (fsync(fd) == -1) {
        if (errno == EROFS || errno == EINVAL) {
            PLOG(WARNING) << "Skip fsync " << dirname
                          << " on a file system does not support synchronization";
        } else {
            PLOG(ERROR) << "Failed to fsync " << dirname;
            return false;
        }
    }
    return true;
}

bool writeStringToFile(const std::string& payload, const std::string& filename) {
    android::base::unique_fd fd(TEMP_FAILURE_RETRY(
        open(filename.c_str(), O_WRONLY | O_CREAT | O_NOFOLLOW | O_TRUNC | O_CLOEXEC, 0666)));
    if (fd == -1) {
        PLOG(ERROR) << "Failed to open " << filename;
        return false;
    }
    if (!android::base::WriteStringToFd(payload, fd)) {
        PLOG(ERROR) << "Failed to write to " << filename;
        unlink(filename.c_str());
        return false;
    }
    // fsync as close won't guarantee flush data
    // see close(2), fsync(2) and b/68901441
    if (fsync(fd) == -1) {
        if (errno == EROFS || errno == EINVAL) {
            PLOG(WARNING) << "Skip fsync " << filename
                          << " on a file system does not support synchronization";
        } else {
            PLOG(ERROR) << "Failed to fsync " << filename;
            unlink(filename.c_str());
            return false;
        }
    }
    return true;
}

}  // namespace vold
}  // namespace android
