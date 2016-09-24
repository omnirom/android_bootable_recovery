/*
 * Copyright (C) 2014 The Android Open Source Project
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

// This program takes a file on an ext4 filesystem and produces a list
// of the blocks that file occupies, which enables the file contents
// to be read directly from the block device without mounting the
// filesystem.
//
// If the filesystem is using an encrypted block device, it will also
// read the file and rewrite it to the same blocks of the underlying
// (unencrypted) block device, so the file contents can be read
// without the need for the decryption key.
//
// The output of this program is a "block map" which looks like this:
//
//     /dev/block/platform/msm_sdcc.1/by-name/userdata     # block device
//     49652 4096                        # file size in bytes, block size
//     3                                 # count of block ranges
//     1000 1008                         # block range 0
//     2100 2102                         # ... block range 1
//     30 33                             # ... block range 2
//
// Each block range represents a half-open interval; the line "30 33"
// reprents the blocks [30, 31, 32].
//
// Recovery can take this block map file and retrieve the underlying
// file data to use as an update package.

/**
 * In addition to the uncrypt work, uncrypt also takes care of setting and
 * clearing the bootloader control block (BCB) at /misc partition.
 *
 * uncrypt is triggered as init services on demand. It uses socket to
 * communicate with its caller (i.e. system_server). The socket is managed by
 * init (i.e. created prior to the service starts, and destroyed when uncrypt
 * exits).
 *
 * Below is the uncrypt protocol.
 *
 *    a. caller                 b. init                    c. uncrypt
 * ---------------            ------------               --------------
 *  a1. ctl.start:
 *    setup-bcb /
 *    clear-bcb /
 *    uncrypt
 *
 *                         b2. create socket at
 *                           /dev/socket/uncrypt
 *
 *                                                   c3. listen and accept
 *
 *  a4. send a 4-byte int
 *    (message length)
 *                                                   c5. receive message length
 *  a6. send message
 *                                                   c7. receive message
 *                                                   c8. <do the work; may send
 *                                                      the progress>
 *  a9. <may handle progress>
 *                                                   c10. <upon finishing>
 *                                                     send "100" or "-1"
 *
 *  a11. receive status code
 *  a12. send a 4-byte int to
 *    ack the receive of the
 *    final status code
 *                                                   c13. receive and exit
 *
 *                          b14. destroy the socket
 *
 * Note that a12 and c13 are necessary to ensure a11 happens before the socket
 * gets destroyed in b14.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <libgen.h>
#include <linux/fs.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <memory>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#include <bootloader_message/bootloader_message.h>
#include <cutils/android_reboot.h>
#include <cutils/properties.h>
#include <cutils/sockets.h>
#include <fs_mgr.h>

#define LOG_TAG "uncrypt"
#include <log/log.h>

#include "error_code.h"
#include "unique_fd.h"

#define WINDOW_SIZE 5

// uncrypt provides three services: SETUP_BCB, CLEAR_BCB and UNCRYPT.
//
// SETUP_BCB and CLEAR_BCB services use socket communication and do not rely
// on /cache partitions. They will handle requests to reboot into recovery
// (for applying updates for non-A/B devices, or factory resets for all
// devices).
//
// UNCRYPT service still needs files on /cache partition (UNCRYPT_PATH_FILE
// and CACHE_BLOCK_MAP). It will be working (and needed) only for non-A/B
// devices, on which /cache partitions always exist.
static const std::string CACHE_BLOCK_MAP = "/cache/recovery/block.map";
static const std::string UNCRYPT_PATH_FILE = "/cache/recovery/uncrypt_file";
static const std::string UNCRYPT_STATUS = "/cache/recovery/uncrypt_status";
static const std::string UNCRYPT_SOCKET = "uncrypt";

static struct fstab* fstab = nullptr;

static int write_at_offset(unsigned char* buffer, size_t size, int wfd, off64_t offset) {
    if (TEMP_FAILURE_RETRY(lseek64(wfd, offset, SEEK_SET)) == -1) {
        ALOGE("error seeking to offset %" PRId64 ": %s", offset, strerror(errno));
        return -1;
    }
    if (!android::base::WriteFully(wfd, buffer, size)) {
        ALOGE("error writing offset %" PRId64 ": %s", offset, strerror(errno));
        return -1;
    }
    return 0;
}

static void add_block_to_ranges(std::vector<int>& ranges, int new_block) {
    if (!ranges.empty() && new_block == ranges.back()) {
        // If the new block comes immediately after the current range,
        // all we have to do is extend the current range.
        ++ranges.back();
    } else {
        // We need to start a new range.
        ranges.push_back(new_block);
        ranges.push_back(new_block + 1);
    }
}

static struct fstab* read_fstab() {
    fstab = NULL;

    // The fstab path is always "/fstab.${ro.hardware}".
    char fstab_path[PATH_MAX+1] = "/fstab.";
    if (!property_get("ro.hardware", fstab_path+strlen(fstab_path), "")) {
        ALOGE("failed to get ro.hardware");
        return NULL;
    }

    fstab = fs_mgr_read_fstab(fstab_path);
    if (!fstab) {
        ALOGE("failed to read %s", fstab_path);
        return NULL;
    }

    return fstab;
}

static const char* find_block_device(const char* path, bool* encryptable, bool* encrypted) {
    // Look for a volume whose mount point is the prefix of path and
    // return its block device.  Set encrypted if it's currently
    // encrypted.
    for (int i = 0; i < fstab->num_entries; ++i) {
        struct fstab_rec* v = &fstab->recs[i];
        if (!v->mount_point) {
            continue;
        }
        int len = strlen(v->mount_point);
        if (strncmp(path, v->mount_point, len) == 0 &&
            (path[len] == '/' || path[len] == 0)) {
            *encrypted = false;
            *encryptable = false;
            if (fs_mgr_is_encryptable(v) || fs_mgr_is_file_encrypted(v)) {
                *encryptable = true;
                char buffer[PROPERTY_VALUE_MAX+1];
                if (property_get("ro.crypto.state", buffer, "") &&
                    strcmp(buffer, "encrypted") == 0) {
                    *encrypted = true;
                }
            }
            return v->blk_device;
        }
    }

    return NULL;
}

static bool write_status_to_socket(int status, int socket) {
    int status_out = htonl(status);
    return android::base::WriteFully(socket, &status_out, sizeof(int));
}

// Parse uncrypt_file to find the update package name.
static bool find_uncrypt_package(const std::string& uncrypt_path_file, std::string* package_name) {
    CHECK(package_name != nullptr);
    std::string uncrypt_path;
    if (!android::base::ReadFileToString(uncrypt_path_file, &uncrypt_path)) {
        ALOGE("failed to open \"%s\": %s", uncrypt_path_file.c_str(), strerror(errno));
        return false;
    }

    // Remove the trailing '\n' if present.
    *package_name = android::base::Trim(uncrypt_path);
    return true;
}

static int produce_block_map(const char* path, const char* map_file, const char* blk_dev,
                             bool encrypted, int socket) {
    std::string err;
    if (!android::base::RemoveFileIfExists(map_file, &err)) {
        ALOGE("failed to remove the existing map file %s: %s", map_file, err.c_str());
        return kUncryptFileRemoveError;
    }
    std::string tmp_map_file = std::string(map_file) + ".tmp";
    unique_fd mapfd(open(tmp_map_file.c_str(), O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR));
    if (!mapfd) {
        ALOGE("failed to open %s: %s\n", tmp_map_file.c_str(), strerror(errno));
        return kUncryptFileOpenError;
    }

    // Make sure we can write to the socket.
    if (!write_status_to_socket(0, socket)) {
        ALOGE("failed to write to socket %d\n", socket);
        return kUncryptSocketWriteError;
    }

    struct stat sb;
    if (stat(path, &sb) != 0) {
        ALOGE("failed to stat %s", path);
        return kUncryptFileStatError;
    }

    ALOGI(" block size: %ld bytes", static_cast<long>(sb.st_blksize));

    int blocks = ((sb.st_size-1) / sb.st_blksize) + 1;
    ALOGI("  file size: %" PRId64 " bytes, %d blocks", sb.st_size, blocks);

    std::vector<int> ranges;

    std::string s = android::base::StringPrintf("%s\n%" PRId64 " %ld\n",
                       blk_dev, sb.st_size, static_cast<long>(sb.st_blksize));
    if (!android::base::WriteStringToFd(s, mapfd.get())) {
        ALOGE("failed to write %s: %s", tmp_map_file.c_str(), strerror(errno));
        return kUncryptWriteError;
    }

    std::vector<std::vector<unsigned char>> buffers;
    if (encrypted) {
        buffers.resize(WINDOW_SIZE, std::vector<unsigned char>(sb.st_blksize));
    }
    int head_block = 0;
    int head = 0, tail = 0;

    unique_fd fd(open(path, O_RDONLY));
    if (!fd) {
        ALOGE("failed to open %s for reading: %s", path, strerror(errno));
        return kUncryptFileOpenError;
    }

    unique_fd wfd(-1);
    if (encrypted) {
        wfd = open(blk_dev, O_WRONLY);
        if (!wfd) {
            ALOGE("failed to open fd for writing: %s", strerror(errno));
            return kUncryptBlockOpenError;
        }
    }

    off64_t pos = 0;
    int last_progress = 0;
    while (pos < sb.st_size) {
        // Update the status file, progress must be between [0, 99].
        int progress = static_cast<int>(100 * (double(pos) / double(sb.st_size)));
        if (progress > last_progress) {
            last_progress = progress;
            write_status_to_socket(progress, socket);
        }

        if ((tail+1) % WINDOW_SIZE == head) {
            // write out head buffer
            int block = head_block;
            if (ioctl(fd.get(), FIBMAP, &block) != 0) {
                ALOGE("failed to find block %d", head_block);
                return kUncryptIoctlError;
            }
            add_block_to_ranges(ranges, block);
            if (encrypted) {
                if (write_at_offset(buffers[head].data(), sb.st_blksize, wfd.get(),
                        static_cast<off64_t>(sb.st_blksize) * block) != 0) {
                    return kUncryptWriteError;
                }
            }
            head = (head + 1) % WINDOW_SIZE;
            ++head_block;
        }

        // read next block to tail
        if (encrypted) {
            size_t to_read = static_cast<size_t>(
                    std::min(static_cast<off64_t>(sb.st_blksize), sb.st_size - pos));
            if (!android::base::ReadFully(fd.get(), buffers[tail].data(), to_read)) {
                ALOGE("failed to read: %s", strerror(errno));
                return kUncryptReadError;
            }
            pos += to_read;
        } else {
            // If we're not encrypting; we don't need to actually read
            // anything, just skip pos forward as if we'd read a
            // block.
            pos += sb.st_blksize;
        }
        tail = (tail+1) % WINDOW_SIZE;
    }

    while (head != tail) {
        // write out head buffer
        int block = head_block;
        if (ioctl(fd.get(), FIBMAP, &block) != 0) {
            ALOGE("failed to find block %d", head_block);
            return kUncryptIoctlError;
        }
        add_block_to_ranges(ranges, block);
        if (encrypted) {
            if (write_at_offset(buffers[head].data(), sb.st_blksize, wfd.get(),
                    static_cast<off64_t>(sb.st_blksize) * block) != 0) {
                return kUncryptWriteError;
            }
        }
        head = (head + 1) % WINDOW_SIZE;
        ++head_block;
    }

    if (!android::base::WriteStringToFd(
            android::base::StringPrintf("%zu\n", ranges.size() / 2), mapfd.get())) {
        ALOGE("failed to write %s: %s", tmp_map_file.c_str(), strerror(errno));
        return kUncryptWriteError;
    }
    for (size_t i = 0; i < ranges.size(); i += 2) {
        if (!android::base::WriteStringToFd(
                android::base::StringPrintf("%d %d\n", ranges[i], ranges[i+1]), mapfd.get())) {
            ALOGE("failed to write %s: %s", tmp_map_file.c_str(), strerror(errno));
            return kUncryptWriteError;
        }
    }

    if (fsync(mapfd.get()) == -1) {
        ALOGE("failed to fsync \"%s\": %s", tmp_map_file.c_str(), strerror(errno));
        return kUncryptFileSyncError;
    }
    if (close(mapfd.get()) == -1) {
        ALOGE("failed to close %s: %s", tmp_map_file.c_str(), strerror(errno));
        return kUncryptFileCloseError;
    }
    mapfd = -1;

    if (encrypted) {
        if (fsync(wfd.get()) == -1) {
            ALOGE("failed to fsync \"%s\": %s", blk_dev, strerror(errno));
            return kUncryptFileSyncError;
        }
        if (close(wfd.get()) == -1) {
            ALOGE("failed to close %s: %s", blk_dev, strerror(errno));
            return kUncryptFileCloseError;
        }
        wfd = -1;
    }

    if (rename(tmp_map_file.c_str(), map_file) == -1) {
        ALOGE("failed to rename %s to %s: %s", tmp_map_file.c_str(), map_file, strerror(errno));
        return kUncryptFileRenameError;
    }
    // Sync dir to make rename() result written to disk.
    std::string file_name = map_file;
    std::string dir_name = dirname(&file_name[0]);
    unique_fd dfd(open(dir_name.c_str(), O_RDONLY | O_DIRECTORY));
    if (!dfd) {
        ALOGE("failed to open dir %s: %s", dir_name.c_str(), strerror(errno));
        return kUncryptFileOpenError;
    }
    if (fsync(dfd.get()) == -1) {
        ALOGE("failed to fsync %s: %s", dir_name.c_str(), strerror(errno));
        return kUncryptFileSyncError;
    }
    if (close(dfd.get()) == -1) {
        ALOGE("failed to close %s: %s", dir_name.c_str(), strerror(errno));
        return kUncryptFileCloseError;
    }
    dfd = -1;
    return 0;
}

static int uncrypt(const char* input_path, const char* map_file, const int socket) {
    ALOGI("update package is \"%s\"", input_path);

    // Turn the name of the file we're supposed to convert into an
    // absolute path, so we can find what filesystem it's on.
    char path[PATH_MAX+1];
    if (realpath(input_path, path) == NULL) {
        ALOGE("failed to convert \"%s\" to absolute path: %s", input_path, strerror(errno));
        return 1;
    }

    bool encryptable;
    bool encrypted;
    const char* blk_dev = find_block_device(path, &encryptable, &encrypted);
    if (blk_dev == NULL) {
        ALOGE("failed to find block device for %s", path);
        return 1;
    }

    // If the filesystem it's on isn't encrypted, we only produce the
    // block map, we don't rewrite the file contents (it would be
    // pointless to do so).
    ALOGI("encryptable: %s", encryptable ? "yes" : "no");
    ALOGI("  encrypted: %s", encrypted ? "yes" : "no");

    // Recovery supports installing packages from 3 paths: /cache,
    // /data, and /sdcard.  (On a particular device, other locations
    // may work, but those are three we actually expect.)
    //
    // On /data we want to convert the file to a block map so that we
    // can read the package without mounting the partition.  On /cache
    // and /sdcard we leave the file alone.
    if (strncmp(path, "/data/", 6) == 0) {
        ALOGI("writing block map %s", map_file);
        return produce_block_map(path, map_file, blk_dev, encrypted, socket);
    }

    return 0;
}

static void log_uncrypt_error_code(UncryptErrorCode error_code) {
    if (!android::base::WriteStringToFile(android::base::StringPrintf(
            "uncrypt_error: %d\n", error_code), UNCRYPT_STATUS)) {
        ALOGW("failed to write to %s: %s", UNCRYPT_STATUS.c_str(), strerror(errno));
    }
}

static bool uncrypt_wrapper(const char* input_path, const char* map_file, const int socket) {
    // Initialize the uncrypt error to kUncryptErrorPlaceholder.
    log_uncrypt_error_code(kUncryptErrorPlaceholder);

    std::string package;
    if (input_path == nullptr) {
        if (!find_uncrypt_package(UNCRYPT_PATH_FILE, &package)) {
            write_status_to_socket(-1, socket);
            // Overwrite the error message.
            log_uncrypt_error_code(kUncryptPackageMissingError);
            return false;
        }
        input_path = package.c_str();
    }
    CHECK(map_file != nullptr);

    auto start = std::chrono::system_clock::now();
    int status = uncrypt(input_path, map_file, socket);
    std::chrono::duration<double> duration = std::chrono::system_clock::now() - start;
    int count = static_cast<int>(duration.count());

    std::string uncrypt_message = android::base::StringPrintf("uncrypt_time: %d\n", count);
    if (status != 0) {
        // Log the time cost and error code if uncrypt fails.
        uncrypt_message += android::base::StringPrintf("uncrypt_error: %d\n", status);
        if (!android::base::WriteStringToFile(uncrypt_message, UNCRYPT_STATUS)) {
            ALOGW("failed to write to %s: %s", UNCRYPT_STATUS.c_str(), strerror(errno));
        }

        write_status_to_socket(-1, socket);
        return false;
    }

    if (!android::base::WriteStringToFile(uncrypt_message, UNCRYPT_STATUS)) {
        ALOGW("failed to write to %s: %s", UNCRYPT_STATUS.c_str(), strerror(errno));
    }

    write_status_to_socket(100, socket);

    return true;
}

static bool clear_bcb(const int socket) {
    std::string err;
    if (!clear_bootloader_message(&err)) {
        ALOGE("failed to clear bootloader message: %s", err.c_str());
        write_status_to_socket(-1, socket);
        return false;
    }
    write_status_to_socket(100, socket);
    return true;
}

static bool setup_bcb(const int socket) {
    // c5. receive message length
    int length;
    if (!android::base::ReadFully(socket, &length, 4)) {
        ALOGE("failed to read the length: %s", strerror(errno));
        return false;
    }
    length = ntohl(length);

    // c7. receive message
    std::string content;
    content.resize(length);
    if (!android::base::ReadFully(socket, &content[0], length)) {
        ALOGE("failed to read the length: %s", strerror(errno));
        return false;
    }
    ALOGI("  received command: [%s] (%zu)", content.c_str(), content.size());
    std::vector<std::string> options = android::base::Split(content, "\n");
    std::string wipe_package;
    for (auto& option : options) {
        if (android::base::StartsWith(option, "--wipe_package=")) {
            std::string path = option.substr(strlen("--wipe_package="));
            if (!android::base::ReadFileToString(path, &wipe_package)) {
                ALOGE("failed to read %s: %s", path.c_str(), strerror(errno));
                return false;
            }
            option = android::base::StringPrintf("--wipe_package_size=%zu", wipe_package.size());
        }
    }

    // c8. setup the bcb command
    std::string err;
    if (!write_bootloader_message(options, &err)) {
        ALOGE("failed to set bootloader message: %s", err.c_str());
        write_status_to_socket(-1, socket);
        return false;
    }
    if (!wipe_package.empty() && !write_wipe_package(wipe_package, &err)) {
        ALOGE("failed to set wipe package: %s", err.c_str());
        write_status_to_socket(-1, socket);
        return false;
    }
    // c10. send "100" status
    write_status_to_socket(100, socket);
    return true;
}

static void usage(const char* exename) {
    fprintf(stderr, "Usage of %s:\n", exename);
    fprintf(stderr, "%s [<package_path> <map_file>]  Uncrypt ota package.\n", exename);
    fprintf(stderr, "%s --clear-bcb  Clear BCB data in misc partition.\n", exename);
    fprintf(stderr, "%s --setup-bcb  Setup BCB data by command file.\n", exename);
}

int main(int argc, char** argv) {
    enum { UNCRYPT, SETUP_BCB, CLEAR_BCB } action;
    const char* input_path = nullptr;
    const char* map_file = CACHE_BLOCK_MAP.c_str();

    if (argc == 2 && strcmp(argv[1], "--clear-bcb") == 0) {
        action = CLEAR_BCB;
    } else if (argc == 2 && strcmp(argv[1], "--setup-bcb") == 0) {
        action = SETUP_BCB;
    } else if (argc == 1) {
        action = UNCRYPT;
    } else if (argc == 3) {
        input_path = argv[1];
        map_file = argv[2];
        action = UNCRYPT;
    } else {
        usage(argv[0]);
        return 2;
    }

    if ((fstab = read_fstab()) == nullptr) {
        log_uncrypt_error_code(kUncryptFstabReadError);
        return 1;
    }

    // c3. The socket is created by init when starting the service. uncrypt
    // will use the socket to communicate with its caller.
    unique_fd service_socket(android_get_control_socket(UNCRYPT_SOCKET.c_str()));
    if (!service_socket) {
        ALOGE("failed to open socket \"%s\": %s", UNCRYPT_SOCKET.c_str(), strerror(errno));
        log_uncrypt_error_code(kUncryptSocketOpenError);
        return 1;
    }
    fcntl(service_socket.get(), F_SETFD, FD_CLOEXEC);

    if (listen(service_socket.get(), 1) == -1) {
        ALOGE("failed to listen on socket %d: %s", service_socket.get(), strerror(errno));
        log_uncrypt_error_code(kUncryptSocketListenError);
        return 1;
    }

    unique_fd socket_fd(accept4(service_socket.get(), nullptr, nullptr, SOCK_CLOEXEC));
    if (!socket_fd) {
        ALOGE("failed to accept on socket %d: %s", service_socket.get(), strerror(errno));
        log_uncrypt_error_code(kUncryptSocketAcceptError);
        return 1;
    }

    bool success = false;
    switch (action) {
        case UNCRYPT:
            success = uncrypt_wrapper(input_path, map_file, socket_fd.get());
            break;
        case SETUP_BCB:
            success = setup_bcb(socket_fd.get());
            break;
        case CLEAR_BCB:
            success = clear_bcb(socket_fd.get());
            break;
        default:  // Should never happen.
            ALOGE("Invalid uncrypt action code: %d", action);
            return 1;
    }

    // c13. Read a 4-byte code from the client before uncrypt exits. This is to
    // ensure the client to receive the last status code before the socket gets
    // destroyed.
    int code;
    if (android::base::ReadFully(socket_fd.get(), &code, 4)) {
        ALOGI("  received %d, exiting now", code);
    } else {
        ALOGE("failed to read the code: %s", strerror(errno));
    }
    return success ? 0 : 1;
}
