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
#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#include <android-base/unique_fd.h>
#include <bootloader_message/bootloader_message.h>
#include <cutils/android_reboot.h>
#include <cutils/sockets.h>
#include <fs_mgr.h>
#include <fstab/fstab.h>

#include "otautil/error_code.h"

using android::fs_mgr::Fstab;
using android::fs_mgr::ReadDefaultFstab;

static constexpr int WINDOW_SIZE = 5;
static constexpr int FIBMAP_RETRY_LIMIT = 3;

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

static Fstab fstab;

static int write_at_offset(unsigned char* buffer, size_t size, int wfd, off64_t offset) {
    if (TEMP_FAILURE_RETRY(lseek64(wfd, offset, SEEK_SET)) == -1) {
        PLOG(ERROR) << "error seeking to offset " << offset;
        return -1;
    }
    if (!android::base::WriteFully(wfd, buffer, size)) {
        PLOG(ERROR) << "error writing offset " << offset;
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

// Looks for a volume whose mount point is the prefix of path and returns its block device or an
// empty string. Sets encryption flags accordingly.
static std::string FindBlockDevice(const std::string& path, bool* encryptable, bool* encrypted,
                                   bool* f2fs_fs) {
  // Ensure f2fs_fs is set to false first.
  *f2fs_fs = false;

  for (const auto& entry : fstab) {
    if (entry.mount_point.empty()) {
      continue;
    }
    if (android::base::StartsWith(path, entry.mount_point + "/")) {
      *encrypted = false;
      *encryptable = false;
      if (entry.is_encryptable() || entry.fs_mgr_flags.file_encryption) {
        *encryptable = true;
        if (android::base::GetProperty("ro.crypto.state", "") == "encrypted") {
          *encrypted = true;
        }
      }
      if (entry.fs_type == "f2fs") {
        *f2fs_fs = true;
      }
      return entry.blk_device;
    }
  }

  return "";
}

static bool write_status_to_socket(int status, int socket) {
    // If socket equals -1, uncrypt is in debug mode without socket communication.
    // Skip writing and return success.
    if (socket == -1) {
        return true;
    }
    int status_out = htonl(status);
    return android::base::WriteFully(socket, &status_out, sizeof(int));
}

// Parses the given path file to find the update package name.
static bool FindUncryptPackage(const std::string& uncrypt_path_file, std::string* package_name) {
  CHECK(package_name != nullptr);
  std::string uncrypt_path;
  if (!android::base::ReadFileToString(uncrypt_path_file, &uncrypt_path)) {
    PLOG(ERROR) << "failed to open \"" << uncrypt_path_file << "\"";
    return false;
  }

  // Remove the trailing '\n' if present.
  *package_name = android::base::Trim(uncrypt_path);
  return true;
}

static int RetryFibmap(int fd, const std::string& name, int* block, const int head_block) {
  CHECK(block != nullptr);
  for (size_t i = 0; i < FIBMAP_RETRY_LIMIT; i++) {
    if (fsync(fd) == -1) {
      PLOG(ERROR) << "failed to fsync \"" << name << "\"";
      return kUncryptFileSyncError;
    }
    if (ioctl(fd, FIBMAP, block) != 0) {
      PLOG(ERROR) << "failed to find block " << head_block;
      return kUncryptIoctlError;
    }
    if (*block != 0) {
      return kUncryptNoError;
    }
    sleep(1);
  }
  LOG(ERROR) << "fibmap of " << head_block << " always returns 0";
  return kUncryptIoctlError;
}

static int ProductBlockMap(const std::string& path, const std::string& map_file,
                           const std::string& blk_dev, bool encrypted, bool f2fs_fs, int socket) {
  std::string err;
  if (!android::base::RemoveFileIfExists(map_file, &err)) {
    LOG(ERROR) << "failed to remove the existing map file " << map_file << ": " << err;
    return kUncryptFileRemoveError;
  }
  std::string tmp_map_file = map_file + ".tmp";
  android::base::unique_fd mapfd(open(tmp_map_file.c_str(), O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR));
  if (mapfd == -1) {
    PLOG(ERROR) << "failed to open " << tmp_map_file;
    return kUncryptFileOpenError;
  }

  // Make sure we can write to the socket.
  if (!write_status_to_socket(0, socket)) {
    LOG(ERROR) << "failed to write to socket " << socket;
    return kUncryptSocketWriteError;
  }

  struct stat sb;
  if (stat(path.c_str(), &sb) != 0) {
    PLOG(ERROR) << "failed to stat " << path;
    return kUncryptFileStatError;
  }

  LOG(INFO) << " block size: " << sb.st_blksize << " bytes";

  int blocks = ((sb.st_size - 1) / sb.st_blksize) + 1;
  LOG(INFO) << "  file size: " << sb.st_size << " bytes, " << blocks << " blocks";

  std::vector<int> ranges;

  std::string s = android::base::StringPrintf("%s\n%" PRId64 " %" PRId64 "\n", blk_dev.c_str(),
                                              static_cast<int64_t>(sb.st_size),
                                              static_cast<int64_t>(sb.st_blksize));
  if (!android::base::WriteStringToFd(s, mapfd)) {
    PLOG(ERROR) << "failed to write " << tmp_map_file;
    return kUncryptWriteError;
  }

  std::vector<std::vector<unsigned char>> buffers;
  if (encrypted) {
    buffers.resize(WINDOW_SIZE, std::vector<unsigned char>(sb.st_blksize));
  }
  int head_block = 0;
  int head = 0, tail = 0;

  android::base::unique_fd fd(open(path.c_str(), O_RDWR));
  if (fd == -1) {
    PLOG(ERROR) << "failed to open " << path << " for reading";
    return kUncryptFileOpenError;
  }

  android::base::unique_fd wfd;
  if (encrypted) {
    wfd.reset(open(blk_dev.c_str(), O_WRONLY));
    if (wfd == -1) {
      PLOG(ERROR) << "failed to open " << blk_dev << " for writing";
      return kUncryptBlockOpenError;
    }
  }

// F2FS-specific ioctl
// It requires the below kernel commit merged in v4.16-rc1.
//   1ad71a27124c ("f2fs: add an ioctl to disable GC for specific file")
// In android-4.4,
//   56ee1e817908 ("f2fs: updates on v4.16-rc1")
// In android-4.9,
//   2f17e34672a8 ("f2fs: updates on v4.16-rc1")
// In android-4.14,
//   ce767d9a55bc ("f2fs: updates on v4.16-rc1")
#ifndef F2FS_IOC_SET_PIN_FILE
#ifndef F2FS_IOCTL_MAGIC
#define F2FS_IOCTL_MAGIC		0xf5
#endif
#define F2FS_IOC_SET_PIN_FILE	_IOW(F2FS_IOCTL_MAGIC, 13, __u32)
#define F2FS_IOC_GET_PIN_FILE	_IOR(F2FS_IOCTL_MAGIC, 14, __u32)
#endif
    if (f2fs_fs) {
        __u32 set = 1;
        int error = ioctl(fd, F2FS_IOC_SET_PIN_FILE, &set);
        // Don't break the old kernels which don't support it.
        if (error && errno != ENOTTY && errno != ENOTSUP) {
            PLOG(ERROR) << "Failed to set pin_file for f2fs: " << path << " on " << blk_dev;
            return kUncryptIoctlError;
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
            if (ioctl(fd, FIBMAP, &block) != 0) {
                PLOG(ERROR) << "failed to find block " << head_block;
                return kUncryptIoctlError;
            }

            if (block == 0) {
                LOG(ERROR) << "failed to find block " << head_block << ", retrying";
                int error = RetryFibmap(fd, path, &block, head_block);
                if (error != kUncryptNoError) {
                    return error;
                }
            }

            add_block_to_ranges(ranges, block);
            if (encrypted) {
                if (write_at_offset(buffers[head].data(), sb.st_blksize, wfd,
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
            if (!android::base::ReadFully(fd, buffers[tail].data(), to_read)) {
                PLOG(ERROR) << "failed to read " << path;
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
        if (ioctl(fd, FIBMAP, &block) != 0) {
            PLOG(ERROR) << "failed to find block " << head_block;
            return kUncryptIoctlError;
        }

        if (block == 0) {
            LOG(ERROR) << "failed to find block " << head_block << ", retrying";
            int error = RetryFibmap(fd, path, &block, head_block);
            if (error != kUncryptNoError) {
                return error;
            }
        }

        add_block_to_ranges(ranges, block);
        if (encrypted) {
            if (write_at_offset(buffers[head].data(), sb.st_blksize, wfd,
                                static_cast<off64_t>(sb.st_blksize) * block) != 0) {
                return kUncryptWriteError;
            }
        }
        head = (head + 1) % WINDOW_SIZE;
        ++head_block;
    }

    if (!android::base::WriteStringToFd(
            android::base::StringPrintf("%zu\n", ranges.size() / 2), mapfd)) {
        PLOG(ERROR) << "failed to write " << tmp_map_file;
        return kUncryptWriteError;
    }
    for (size_t i = 0; i < ranges.size(); i += 2) {
        if (!android::base::WriteStringToFd(
                android::base::StringPrintf("%d %d\n", ranges[i], ranges[i+1]), mapfd)) {
            PLOG(ERROR) << "failed to write " << tmp_map_file;
            return kUncryptWriteError;
        }
    }

    if (fsync(mapfd) == -1) {
        PLOG(ERROR) << "failed to fsync \"" << tmp_map_file << "\"";
        return kUncryptFileSyncError;
    }
    if (close(mapfd.release()) == -1) {
        PLOG(ERROR) << "failed to close " << tmp_map_file;
        return kUncryptFileCloseError;
    }

    if (encrypted) {
        if (fsync(wfd) == -1) {
            PLOG(ERROR) << "failed to fsync \"" << blk_dev << "\"";
            return kUncryptFileSyncError;
        }
        if (close(wfd.release()) == -1) {
            PLOG(ERROR) << "failed to close " << blk_dev;
            return kUncryptFileCloseError;
        }
    }

    if (rename(tmp_map_file.c_str(), map_file.c_str()) == -1) {
      PLOG(ERROR) << "failed to rename " << tmp_map_file << " to " << map_file;
      return kUncryptFileRenameError;
    }
    // Sync dir to make rename() result written to disk.
    std::string dir_name = android::base::Dirname(map_file);
    android::base::unique_fd dfd(open(dir_name.c_str(), O_RDONLY | O_DIRECTORY));
    if (dfd == -1) {
        PLOG(ERROR) << "failed to open dir " << dir_name;
        return kUncryptFileOpenError;
    }
    if (fsync(dfd) == -1) {
        PLOG(ERROR) << "failed to fsync " << dir_name;
        return kUncryptFileSyncError;
    }
    if (close(dfd.release()) == -1) {
        PLOG(ERROR) << "failed to close " << dir_name;
        return kUncryptFileCloseError;
    }
    return 0;
}

static int Uncrypt(const std::string& input_path, const std::string& map_file, int socket) {
  LOG(INFO) << "update package is \"" << input_path << "\"";

  // Turn the name of the file we're supposed to convert into an absolute path, so we can find what
  // filesystem it's on.
  std::string path;
  if (!android::base::Realpath(input_path, &path)) {
    PLOG(ERROR) << "Failed to convert \"" << input_path << "\" to absolute path";
    return kUncryptRealpathFindError;
  }

  bool encryptable;
  bool encrypted;
  bool f2fs_fs;
  const std::string blk_dev = FindBlockDevice(path, &encryptable, &encrypted, &f2fs_fs);
  if (blk_dev.empty()) {
    LOG(ERROR) << "Failed to find block device for " << path;
    return kUncryptBlockDeviceFindError;
  }

  // If the filesystem it's on isn't encrypted, we only produce the block map, we don't rewrite the
  // file contents (it would be pointless to do so).
  LOG(INFO) << "encryptable: " << (encryptable ? "yes" : "no");
  LOG(INFO) << "  encrypted: " << (encrypted ? "yes" : "no");

  // Recovery supports installing packages from 3 paths: /cache, /data, and /sdcard. (On a
  // particular device, other locations may work, but those are three we actually expect.)
  //
  // On /data we want to convert the file to a block map so that we can read the package without
  // mounting the partition. On /cache and /sdcard we leave the file alone.
  if (android::base::StartsWith(path, "/data/")) {
    LOG(INFO) << "writing block map " << map_file;
    return ProductBlockMap(path, map_file, blk_dev, encrypted, f2fs_fs, socket);
  }

  return 0;
}

static void log_uncrypt_error_code(UncryptErrorCode error_code) {
    if (!android::base::WriteStringToFile(android::base::StringPrintf(
            "uncrypt_error: %d\n", error_code), UNCRYPT_STATUS)) {
        PLOG(WARNING) << "failed to write to " << UNCRYPT_STATUS;
    }
}

static bool uncrypt_wrapper(const char* input_path, const char* map_file, const int socket) {
    // Initialize the uncrypt error to kUncryptErrorPlaceholder.
    log_uncrypt_error_code(kUncryptErrorPlaceholder);

    std::string package;
    if (input_path == nullptr) {
      if (!FindUncryptPackage(UNCRYPT_PATH_FILE, &package)) {
        write_status_to_socket(-1, socket);
        // Overwrite the error message.
        log_uncrypt_error_code(kUncryptPackageMissingError);
        return false;
      }
      input_path = package.c_str();
    }
    CHECK(map_file != nullptr);

    auto start = std::chrono::system_clock::now();
    int status = Uncrypt(input_path, map_file, socket);
    std::chrono::duration<double> duration = std::chrono::system_clock::now() - start;
    int count = static_cast<int>(duration.count());

    std::string uncrypt_message = android::base::StringPrintf("uncrypt_time: %d\n", count);
    if (status != 0) {
        // Log the time cost and error code if uncrypt fails.
        uncrypt_message += android::base::StringPrintf("uncrypt_error: %d\n", status);
        if (!android::base::WriteStringToFile(uncrypt_message, UNCRYPT_STATUS)) {
            PLOG(WARNING) << "failed to write to " << UNCRYPT_STATUS;
        }

        write_status_to_socket(-1, socket);
        return false;
    }

    if (!android::base::WriteStringToFile(uncrypt_message, UNCRYPT_STATUS)) {
        PLOG(WARNING) << "failed to write to " << UNCRYPT_STATUS;
    }

    write_status_to_socket(100, socket);

    return true;
}

static bool clear_bcb(const int socket) {
    std::string err;
    if (!clear_bootloader_message(&err)) {
        LOG(ERROR) << "failed to clear bootloader message: " << err;
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
        PLOG(ERROR) << "failed to read the length";
        return false;
    }
    length = ntohl(length);

    // c7. receive message
    std::string content;
    content.resize(length);
    if (!android::base::ReadFully(socket, &content[0], length)) {
        PLOG(ERROR) << "failed to read the message";
        return false;
    }
    LOG(INFO) << "  received command: [" << content << "] (" << content.size() << ")";
    std::vector<std::string> options = android::base::Split(content, "\n");
    std::string wipe_package;
    for (auto& option : options) {
        if (android::base::StartsWith(option, "--wipe_package=")) {
            std::string path = option.substr(strlen("--wipe_package="));
            if (!android::base::ReadFileToString(path, &wipe_package)) {
                PLOG(ERROR) << "failed to read " << path;
                return false;
            }
            option = android::base::StringPrintf("--wipe_package_size=%zu", wipe_package.size());
        }
    }

    // c8. setup the bcb command
    std::string err;
    if (!write_bootloader_message(options, &err)) {
        LOG(ERROR) << "failed to set bootloader message: " << err;
        write_status_to_socket(-1, socket);
        return false;
    }
    if (!wipe_package.empty() && !write_wipe_package(wipe_package, &err)) {
        PLOG(ERROR) << "failed to set wipe package: " << err;
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
    enum { UNCRYPT, SETUP_BCB, CLEAR_BCB, UNCRYPT_DEBUG } action;
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
        action = UNCRYPT_DEBUG;
    } else {
        usage(argv[0]);
        return 2;
    }

    if (!ReadDefaultFstab(&fstab)) {
        LOG(ERROR) << "failed to read default fstab";
        log_uncrypt_error_code(kUncryptFstabReadError);
        return 1;
    }

    if (action == UNCRYPT_DEBUG) {
        LOG(INFO) << "uncrypt called in debug mode, skip socket communication";
        bool success = uncrypt_wrapper(input_path, map_file, -1);
        if (success) {
            LOG(INFO) << "uncrypt succeeded";
        } else{
            LOG(INFO) << "uncrypt failed";
        }
        return success ? 0 : 1;
    }

    // c3. The socket is created by init when starting the service. uncrypt
    // will use the socket to communicate with its caller.
    android::base::unique_fd service_socket(android_get_control_socket(UNCRYPT_SOCKET.c_str()));
    if (service_socket == -1) {
        PLOG(ERROR) << "failed to open socket \"" << UNCRYPT_SOCKET << "\"";
        log_uncrypt_error_code(kUncryptSocketOpenError);
        return 1;
    }
    fcntl(service_socket, F_SETFD, FD_CLOEXEC);

    if (listen(service_socket, 1) == -1) {
        PLOG(ERROR) << "failed to listen on socket " << service_socket.get();
        log_uncrypt_error_code(kUncryptSocketListenError);
        return 1;
    }

    android::base::unique_fd socket_fd(accept4(service_socket, nullptr, nullptr, SOCK_CLOEXEC));
    if (socket_fd == -1) {
        PLOG(ERROR) << "failed to accept on socket " << service_socket.get();
        log_uncrypt_error_code(kUncryptSocketAcceptError);
        return 1;
    }

    bool success = false;
    switch (action) {
        case UNCRYPT:
            success = uncrypt_wrapper(input_path, map_file, socket_fd);
            break;
        case SETUP_BCB:
            success = setup_bcb(socket_fd);
            break;
        case CLEAR_BCB:
            success = clear_bcb(socket_fd);
            break;
        default:  // Should never happen.
            LOG(ERROR) << "Invalid uncrypt action code: " << action;
            return 1;
    }

    // c13. Read a 4-byte code from the client before uncrypt exits. This is to
    // ensure the client to receive the last status code before the socket gets
    // destroyed.
    int code;
    if (android::base::ReadFully(socket_fd, &code, 4)) {
        LOG(INFO) << "  received " << code << ", exiting now";
    } else {
        PLOG(ERROR) << "failed to read the code";
    }
    return success ? 0 : 1;
}
