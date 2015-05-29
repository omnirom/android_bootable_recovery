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

#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <base/file.h>
#include <base/strings.h>
#include <cutils/properties.h>
#include <fs_mgr.h>
#define LOG_TAG "uncrypt"
#include <log/log.h>

#define WINDOW_SIZE 5

static const std::string cache_block_map = "/cache/recovery/block.map";
static const std::string status_file = "/cache/recovery/uncrypt_status";
static const std::string uncrypt_file = "/cache/recovery/uncrypt_file";

static struct fstab* fstab = NULL;

static int write_at_offset(unsigned char* buffer, size_t size, int wfd, off64_t offset) {
    if (TEMP_FAILURE_RETRY(lseek64(wfd, offset, SEEK_SET)) == -1) {
        ALOGE("error seeking to offset %lld: %s\n", offset, strerror(errno));
        return -1;
    }
    size_t written = 0;
    while (written < size) {
        ssize_t wrote = TEMP_FAILURE_RETRY(write(wfd, buffer + written, size - written));
        if (wrote == -1) {
            ALOGE("error writing offset %lld: %s\n", (offset + written), strerror(errno));
            return -1;
        }
        written += wrote;
    }
    return 0;
}

static void add_block_to_ranges(int** ranges, int* range_alloc, int* range_used, int new_block) {
    // If the current block start is < 0, set the start to the new
    // block.  (This only happens for the very first block of the very
    // first range.)
    if ((*ranges)[*range_used*2-2] < 0) {
        (*ranges)[*range_used*2-2] = new_block;
        (*ranges)[*range_used*2-1] = new_block;
    }

    if (new_block == (*ranges)[*range_used*2-1]) {
        // If the new block comes immediately after the current range,
        // all we have to do is extend the current range.
        ++(*ranges)[*range_used*2-1];
    } else {
        // We need to start a new range.

        // If there isn't enough room in the array, we need to expand it.
        if (*range_used >= *range_alloc) {
            *range_alloc *= 2;
            *ranges = reinterpret_cast<int*>(realloc(*ranges, *range_alloc * 2 * sizeof(int)));
        }

        ++*range_used;
        (*ranges)[*range_used*2-2] = new_block;
        (*ranges)[*range_used*2-1] = new_block+1;
    }
}

static struct fstab* read_fstab() {
    fstab = NULL;

    // The fstab path is always "/fstab.${ro.hardware}".
    char fstab_path[PATH_MAX+1] = "/fstab.";
    if (!property_get("ro.hardware", fstab_path+strlen(fstab_path), "")) {
        ALOGE("failed to get ro.hardware\n");
        return NULL;
    }

    fstab = fs_mgr_read_fstab(fstab_path);
    if (!fstab) {
        ALOGE("failed to read %s\n", fstab_path);
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
            if (fs_mgr_is_encryptable(v)) {
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

// Parse uncrypt_file to find the update package name.
static bool find_uncrypt_package(std::string& package_name)
{
    if (!android::base::ReadFileToString(uncrypt_file, &package_name)) {
        ALOGE("failed to open \"%s\": %s\n", uncrypt_file.c_str(), strerror(errno));
        return false;
    }

    // Remove the trailing '\n' if present.
    package_name = android::base::Trim(package_name);

    return true;
}

static int produce_block_map(const char* path, const char* map_file, const char* blk_dev,
                             bool encrypted, int status_fd) {
    int mapfd = open(map_file, O_WRONLY | O_CREAT | O_SYNC, S_IRUSR | S_IWUSR);
    if (mapfd == -1) {
        ALOGE("failed to open %s\n", map_file);
        return -1;
    }
    FILE* mapf = fdopen(mapfd, "w");

    // Make sure we can write to the status_file.
    if (!android::base::WriteStringToFd("0\n", status_fd)) {
        ALOGE("failed to update \"%s\"\n", status_file.c_str());
        return -1;
    }

    struct stat sb;
    int ret = stat(path, &sb);
    if (ret != 0) {
        ALOGE("failed to stat %s\n", path);
        return -1;
    }

    ALOGI(" block size: %ld bytes\n", (long)sb.st_blksize);

    int blocks = ((sb.st_size-1) / sb.st_blksize) + 1;
    ALOGI("  file size: %lld bytes, %d blocks\n", (long long)sb.st_size, blocks);

    int range_alloc = 1;
    int range_used = 1;
    int* ranges = reinterpret_cast<int*>(malloc(range_alloc * 2 * sizeof(int)));
    ranges[0] = -1;
    ranges[1] = -1;

    fprintf(mapf, "%s\n%lld %lu\n", blk_dev, (long long)sb.st_size, (unsigned long)sb.st_blksize);

    unsigned char* buffers[WINDOW_SIZE];
    if (encrypted) {
        for (size_t i = 0; i < WINDOW_SIZE; ++i) {
            buffers[i] = reinterpret_cast<unsigned char*>(malloc(sb.st_blksize));
        }
    }
    int head_block = 0;
    int head = 0, tail = 0;
    size_t pos = 0;

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        ALOGE("failed to open fd for reading: %s\n", strerror(errno));
        return -1;
    }

    int wfd = -1;
    if (encrypted) {
        wfd = open(blk_dev, O_WRONLY | O_SYNC);
        if (wfd < 0) {
            ALOGE("failed to open fd for writing: %s\n", strerror(errno));
            return -1;
        }
    }

    int last_progress = 0;
    while (pos < sb.st_size) {
        // Update the status file, progress must be between [0, 99].
        int progress = static_cast<int>(100 * (double(pos) / double(sb.st_size)));
        if (progress > last_progress) {
          last_progress = progress;
          android::base::WriteStringToFd(std::to_string(progress) + "\n", status_fd);
        }

        if ((tail+1) % WINDOW_SIZE == head) {
            // write out head buffer
            int block = head_block;
            ret = ioctl(fd, FIBMAP, &block);
            if (ret != 0) {
                ALOGE("failed to find block %d\n", head_block);
                return -1;
            }
            add_block_to_ranges(&ranges, &range_alloc, &range_used, block);
            if (encrypted) {
                if (write_at_offset(buffers[head], sb.st_blksize, wfd,
                        (off64_t)sb.st_blksize * block) != 0) {
                    return -1;
                }
            }
            head = (head + 1) % WINDOW_SIZE;
            ++head_block;
        }

        // read next block to tail
        if (encrypted) {
            size_t so_far = 0;
            while (so_far < sb.st_blksize && pos < sb.st_size) {
                ssize_t this_read =
                        TEMP_FAILURE_RETRY(read(fd, buffers[tail] + so_far, sb.st_blksize - so_far));
                if (this_read == -1) {
                    ALOGE("failed to read: %s\n", strerror(errno));
                    return -1;
                }
                so_far += this_read;
                pos += this_read;
            }
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
        ret = ioctl(fd, FIBMAP, &block);
        if (ret != 0) {
            ALOGE("failed to find block %d\n", head_block);
            return -1;
        }
        add_block_to_ranges(&ranges, &range_alloc, &range_used, block);
        if (encrypted) {
            if (write_at_offset(buffers[head], sb.st_blksize, wfd,
                    (off64_t)sb.st_blksize * block) != 0) {
                return -1;
            }
        }
        head = (head + 1) % WINDOW_SIZE;
        ++head_block;
    }

    fprintf(mapf, "%d\n", range_used);
    for (int i = 0; i < range_used; ++i) {
        fprintf(mapf, "%d %d\n", ranges[i*2], ranges[i*2+1]);
    }

    if (fsync(mapfd) == -1) {
        ALOGE("failed to fsync \"%s\": %s\n", map_file, strerror(errno));
        return -1;
    }
    fclose(mapf);
    close(fd);
    if (encrypted) {
        if (fsync(wfd) == -1) {
            ALOGE("failed to fsync \"%s\": %s\n", blk_dev, strerror(errno));
            return -1;
        }
        close(wfd);
    }

    return 0;
}

static void wipe_misc() {
    ALOGI("removing old commands from misc");
    for (int i = 0; i < fstab->num_entries; ++i) {
        struct fstab_rec* v = &fstab->recs[i];
        if (!v->mount_point) continue;
        if (strcmp(v->mount_point, "/misc") == 0) {
            int fd = open(v->blk_device, O_WRONLY | O_SYNC);
            uint8_t zeroes[1088];   // sizeof(bootloader_message) from recovery
            memset(zeroes, 0, sizeof(zeroes));

            size_t written = 0;
            size_t size = sizeof(zeroes);
            while (written < size) {
                ssize_t w = TEMP_FAILURE_RETRY(write(fd, zeroes, size-written));
                if (w == -1) {
                    ALOGE("zero write failed: %s\n", strerror(errno));
                    return;
                } else {
                    written += w;
                }
            }
            if (fsync(fd) == -1) {
                ALOGE("failed to fsync \"%s\": %s\n", v->blk_device, strerror(errno));
                close(fd);
                return;
            }
            close(fd);
        }
    }
}

static void reboot_to_recovery() {
    ALOGI("rebooting to recovery");
    property_set("sys.powerctl", "reboot,recovery");
    sleep(10);
    ALOGE("reboot didn't succeed?");
}

int uncrypt(const char* input_path, const char* map_file, int status_fd) {

    ALOGI("update package is \"%s\"", input_path);

    // Turn the name of the file we're supposed to convert into an
    // absolute path, so we can find what filesystem it's on.
    char path[PATH_MAX+1];
    if (realpath(input_path, path) == NULL) {
        ALOGE("failed to convert \"%s\" to absolute path: %s", input_path, strerror(errno));
        return 1;
    }

    if (read_fstab() == NULL) {
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
    ALOGI("encryptable: %s\n", encryptable ? "yes" : "no");
    ALOGI("  encrypted: %s\n", encrypted ? "yes" : "no");

    // Recovery supports installing packages from 3 paths: /cache,
    // /data, and /sdcard.  (On a particular device, other locations
    // may work, but those are three we actually expect.)
    //
    // On /data we want to convert the file to a block map so that we
    // can read the package without mounting the partition.  On /cache
    // and /sdcard we leave the file alone.
    if (strncmp(path, "/data/", 6) == 0) {
        ALOGI("writing block map %s", map_file);
        if (produce_block_map(path, map_file, blk_dev, encrypted, status_fd) != 0) {
            return 1;
        }
    }

    return 0;
}

int main(int argc, char** argv) {
    const char* input_path;
    const char* map_file;

    if (argc != 3 && argc != 1 && (argc == 2 && strcmp(argv[1], "--reboot") != 0)) {
        fprintf(stderr, "usage: %s [--reboot] [<transform_path> <map_file>]\n", argv[0]);
        return 2;
    }

    // When uncrypt is started with "--reboot", it wipes misc and reboots.
    // Otherwise it uncrypts the package and writes the block map.
    if (argc == 2) {
        if (read_fstab() == NULL) {
            return 1;
        }
        wipe_misc();
        reboot_to_recovery();
    } else {
        // The pipe has been created by the system server.
        int status_fd = open(status_file.c_str(), O_WRONLY | O_CREAT | O_SYNC, S_IRUSR | S_IWUSR);
        if (status_fd == -1) {
            ALOGE("failed to open pipe \"%s\": %s\n", status_file.c_str(), strerror(errno));
            return 1;
        }

        if (argc == 3) {
            // when command-line args are given this binary is being used
            // for debugging.
            input_path = argv[1];
            map_file = argv[2];
        } else {
            std::string package;
            if (!find_uncrypt_package(package)) {
                android::base::WriteStringToFd("-1\n", status_fd);
                close(status_fd);
                return 1;
            }
            input_path = package.c_str();
            map_file = cache_block_map.c_str();
        }

        int status = uncrypt(input_path, map_file, status_fd);
        if (status != 0) {
            android::base::WriteStringToFd("-1\n", status_fd);
            close(status_fd);
            return 1;
        }

        android::base::WriteStringToFd("100\n", status_fd);
        close(status_fd);
    }

    return 0;
}
