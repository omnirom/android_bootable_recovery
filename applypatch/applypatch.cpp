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

#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/types.h>
#include <unistd.h>

#include <memory>
#include <string>

#include <android-base/strings.h>

#include "openssl/sha.h"
#include "applypatch.h"
#include "mtdutils/mtdutils.h"
#include "edify/expr.h"
#include "ota_io.h"
#include "print_sha1.h"

static int LoadPartitionContents(const char* filename, FileContents* file);
static ssize_t FileSink(const unsigned char* data, ssize_t len, void* token);
static int GenerateTarget(FileContents* source_file,
                          const Value* source_patch_value,
                          FileContents* copy_file,
                          const Value* copy_patch_value,
                          const char* source_filename,
                          const char* target_filename,
                          const uint8_t target_sha1[SHA_DIGEST_LENGTH],
                          size_t target_size,
                          const Value* bonus_data);

static bool mtd_partitions_scanned = false;

// Read a file into memory; store the file contents and associated
// metadata in *file.
//
// Return 0 on success.
int LoadFileContents(const char* filename, FileContents* file) {
    // A special 'filename' beginning with "MTD:" or "EMMC:" means to
    // load the contents of a partition.
    if (strncmp(filename, "MTD:", 4) == 0 ||
        strncmp(filename, "EMMC:", 5) == 0) {
        return LoadPartitionContents(filename, file);
    }

    if (stat(filename, &file->st) != 0) {
        printf("failed to stat \"%s\": %s\n", filename, strerror(errno));
        return -1;
    }

    std::vector<unsigned char> data(file->st.st_size);
    FILE* f = ota_fopen(filename, "rb");
    if (f == NULL) {
        printf("failed to open \"%s\": %s\n", filename, strerror(errno));
        return -1;
    }

    size_t bytes_read = ota_fread(data.data(), 1, data.size(), f);
    if (bytes_read != data.size()) {
        printf("short read of \"%s\" (%zu bytes of %zd)\n", filename, bytes_read, data.size());
        ota_fclose(f);
        return -1;
    }
    ota_fclose(f);
    file->data = std::move(data);
    SHA1(file->data.data(), file->data.size(), file->sha1);
    return 0;
}

// Load the contents of an MTD or EMMC partition into the provided
// FileContents.  filename should be a string of the form
// "MTD:<partition_name>:<size_1>:<sha1_1>:<size_2>:<sha1_2>:..."  (or
// "EMMC:<partition_device>:...").  The smallest size_n bytes for
// which that prefix of the partition contents has the corresponding
// sha1 hash will be loaded.  It is acceptable for a size value to be
// repeated with different sha1s.  Will return 0 on success.
//
// This complexity is needed because if an OTA installation is
// interrupted, the partition might contain either the source or the
// target data, which might be of different lengths.  We need to know
// the length in order to read from a partition (there is no
// "end-of-file" marker), so the caller must specify the possible
// lengths and the hash of the data, and we'll do the load expecting
// to find one of those hashes.
enum PartitionType { MTD, EMMC };

static int LoadPartitionContents(const char* filename, FileContents* file) {
    std::string copy(filename);
    std::vector<std::string> pieces = android::base::Split(copy, ":");
    if (pieces.size() < 4 || pieces.size() % 2 != 0) {
        printf("LoadPartitionContents called with bad filename (%s)\n", filename);
        return -1;
    }

    enum PartitionType type;
    if (pieces[0] == "MTD") {
        type = MTD;
    } else if (pieces[0] == "EMMC") {
        type = EMMC;
    } else {
        printf("LoadPartitionContents called with bad filename (%s)\n", filename);
        return -1;
    }
    const char* partition = pieces[1].c_str();

    size_t pairs = (pieces.size() - 2) / 2;    // # of (size, sha1) pairs in filename
    std::vector<size_t> index(pairs);
    std::vector<size_t> size(pairs);
    std::vector<std::string> sha1sum(pairs);

    for (size_t i = 0; i < pairs; ++i) {
        size[i] = strtol(pieces[i*2+2].c_str(), NULL, 10);
        if (size[i] == 0) {
            printf("LoadPartitionContents called with bad size (%s)\n", filename);
            return -1;
        }
        sha1sum[i] = pieces[i*2+3].c_str();
        index[i] = i;
    }

    // Sort the index[] array so it indexes the pairs in order of increasing size.
    sort(index.begin(), index.end(),
        [&](const size_t& i, const size_t& j) {
            return (size[i] < size[j]);
        }
    );

    MtdReadContext* ctx = NULL;
    FILE* dev = NULL;

    switch (type) {
        case MTD: {
            if (!mtd_partitions_scanned) {
                mtd_scan_partitions();
                mtd_partitions_scanned = true;
            }

            const MtdPartition* mtd = mtd_find_partition_by_name(partition);
            if (mtd == NULL) {
                printf("mtd partition \"%s\" not found (loading %s)\n", partition, filename);
                return -1;
            }

            ctx = mtd_read_partition(mtd);
            if (ctx == NULL) {
                printf("failed to initialize read of mtd partition \"%s\"\n", partition);
                return -1;
            }
            break;
        }

        case EMMC:
            dev = ota_fopen(partition, "rb");
            if (dev == NULL) {
                printf("failed to open emmc partition \"%s\": %s\n", partition, strerror(errno));
                return -1;
            }
    }

    SHA_CTX sha_ctx;
    SHA1_Init(&sha_ctx);
    uint8_t parsed_sha[SHA_DIGEST_LENGTH];

    // Allocate enough memory to hold the largest size.
    std::vector<unsigned char> data(size[index[pairs-1]]);
    char* p = reinterpret_cast<char*>(data.data());
    size_t data_size = 0;                // # bytes read so far
    bool found = false;

    for (size_t i = 0; i < pairs; ++i) {
        // Read enough additional bytes to get us up to the next size. (Again,
        // we're trying the possibilities in order of increasing size).
        size_t next = size[index[i]] - data_size;
        if (next > 0) {
            size_t read = 0;
            switch (type) {
                case MTD:
                    read = mtd_read_data(ctx, p, next);
                    break;

                case EMMC:
                    read = ota_fread(p, 1, next, dev);
                    break;
            }
            if (next != read) {
                printf("short read (%zu bytes of %zu) for partition \"%s\"\n",
                       read, next, partition);
                return -1;
            }
            SHA1_Update(&sha_ctx, p, read);
            data_size += read;
            p += read;
        }

        // Duplicate the SHA context and finalize the duplicate so we can
        // check it against this pair's expected hash.
        SHA_CTX temp_ctx;
        memcpy(&temp_ctx, &sha_ctx, sizeof(SHA_CTX));
        uint8_t sha_so_far[SHA_DIGEST_LENGTH];
        SHA1_Final(sha_so_far, &temp_ctx);

        if (ParseSha1(sha1sum[index[i]].c_str(), parsed_sha) != 0) {
            printf("failed to parse sha1 %s in %s\n", sha1sum[index[i]].c_str(), filename);
            return -1;
        }

        if (memcmp(sha_so_far, parsed_sha, SHA_DIGEST_LENGTH) == 0) {
            // we have a match.  stop reading the partition; we'll return
            // the data we've read so far.
            printf("partition read matched size %zu sha %s\n",
                   size[index[i]], sha1sum[index[i]].c_str());
            found = true;
            break;
        }
    }

    switch (type) {
        case MTD:
            mtd_read_close(ctx);
            break;

        case EMMC:
            ota_fclose(dev);
            break;
    }


    if (!found) {
        // Ran off the end of the list of (size,sha1) pairs without finding a match.
        printf("contents of partition \"%s\" didn't match %s\n", partition, filename);
        return -1;
    }

    SHA1_Final(file->sha1, &sha_ctx);

    data.resize(data_size);
    file->data = std::move(data);
    // Fake some stat() info.
    file->st.st_mode = 0644;
    file->st.st_uid = 0;
    file->st.st_gid = 0;

    return 0;
}


// Save the contents of the given FileContents object under the given
// filename.  Return 0 on success.
int SaveFileContents(const char* filename, const FileContents* file) {
    int fd = ota_open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_SYNC, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        printf("failed to open \"%s\" for write: %s\n", filename, strerror(errno));
        return -1;
    }

    ssize_t bytes_written = FileSink(file->data.data(), file->data.size(), &fd);
    if (bytes_written != static_cast<ssize_t>(file->data.size())) {
        printf("short write of \"%s\" (%zd bytes of %zu) (%s)\n",
               filename, bytes_written, file->data.size(), strerror(errno));
        ota_close(fd);
        return -1;
    }
    if (ota_fsync(fd) != 0) {
        printf("fsync of \"%s\" failed: %s\n", filename, strerror(errno));
        return -1;
    }
    if (ota_close(fd) != 0) {
        printf("close of \"%s\" failed: %s\n", filename, strerror(errno));
        return -1;
    }

    if (chmod(filename, file->st.st_mode) != 0) {
        printf("chmod of \"%s\" failed: %s\n", filename, strerror(errno));
        return -1;
    }
    if (chown(filename, file->st.st_uid, file->st.st_gid) != 0) {
        printf("chown of \"%s\" failed: %s\n", filename, strerror(errno));
        return -1;
    }

    return 0;
}

// Write a memory buffer to 'target' partition, a string of the form
// "MTD:<partition>[:...]" or "EMMC:<partition_device>[:...]". The target name
// might contain multiple colons, but WriteToPartition() only uses the first
// two and ignores the rest. Return 0 on success.
int WriteToPartition(const unsigned char* data, size_t len, const char* target) {
    std::string copy(target);
    std::vector<std::string> pieces = android::base::Split(copy, ":");

    if (pieces.size() < 2) {
        printf("WriteToPartition called with bad target (%s)\n", target);
        return -1;
    }

    enum PartitionType type;
    if (pieces[0] == "MTD") {
        type = MTD;
    } else if (pieces[0] == "EMMC") {
        type = EMMC;
    } else {
        printf("WriteToPartition called with bad target (%s)\n", target);
        return -1;
    }
    const char* partition = pieces[1].c_str();

    switch (type) {
        case MTD: {
            if (!mtd_partitions_scanned) {
                mtd_scan_partitions();
                mtd_partitions_scanned = true;
            }

            const MtdPartition* mtd = mtd_find_partition_by_name(partition);
            if (mtd == NULL) {
                printf("mtd partition \"%s\" not found for writing\n", partition);
                return -1;
            }

            MtdWriteContext* ctx = mtd_write_partition(mtd);
            if (ctx == NULL) {
                printf("failed to init mtd partition \"%s\" for writing\n", partition);
                return -1;
            }

            size_t written = mtd_write_data(ctx, reinterpret_cast<const char*>(data), len);
            if (written != len) {
                printf("only wrote %zu of %zu bytes to MTD %s\n", written, len, partition);
                mtd_write_close(ctx);
                return -1;
            }

            if (mtd_erase_blocks(ctx, -1) < 0) {
                printf("error finishing mtd write of %s\n", partition);
                mtd_write_close(ctx);
                return -1;
            }

            if (mtd_write_close(ctx)) {
                printf("error closing mtd write of %s\n", partition);
                return -1;
            }
            break;
        }

        case EMMC: {
            size_t start = 0;
            bool success = false;
            int fd = ota_open(partition, O_RDWR | O_SYNC);
            if (fd < 0) {
                printf("failed to open %s: %s\n", partition, strerror(errno));
                return -1;
            }

            for (size_t attempt = 0; attempt < 2; ++attempt) {
                if (TEMP_FAILURE_RETRY(lseek(fd, start, SEEK_SET)) == -1) {
                    printf("failed seek on %s: %s\n", partition, strerror(errno));
                    return -1;
                }
                while (start < len) {
                    size_t to_write = len - start;
                    if (to_write > 1<<20) to_write = 1<<20;

                    ssize_t written = TEMP_FAILURE_RETRY(ota_write(fd, data+start, to_write));
                    if (written == -1) {
                        printf("failed write writing to %s: %s\n", partition, strerror(errno));
                        return -1;
                    }
                    start += written;
                }
                if (ota_fsync(fd) != 0) {
                   printf("failed to sync to %s (%s)\n", partition, strerror(errno));
                   return -1;
                }
                if (ota_close(fd) != 0) {
                   printf("failed to close %s (%s)\n", partition, strerror(errno));
                   return -1;
                }
                fd = ota_open(partition, O_RDONLY);
                if (fd < 0) {
                   printf("failed to reopen %s for verify (%s)\n", partition, strerror(errno));
                   return -1;
                }

                // Drop caches so our subsequent verification read
                // won't just be reading the cache.
                sync();
                int dc = ota_open("/proc/sys/vm/drop_caches", O_WRONLY);
                if (TEMP_FAILURE_RETRY(ota_write(dc, "3\n", 2)) == -1) {
                    printf("write to /proc/sys/vm/drop_caches failed: %s\n", strerror(errno));
                } else {
                    printf("  caches dropped\n");
                }
                ota_close(dc);
                sleep(1);

                // verify
                if (TEMP_FAILURE_RETRY(lseek(fd, 0, SEEK_SET)) == -1) {
                    printf("failed to seek back to beginning of %s: %s\n",
                           partition, strerror(errno));
                    return -1;
                }
                unsigned char buffer[4096];
                start = len;
                for (size_t p = 0; p < len; p += sizeof(buffer)) {
                    size_t to_read = len - p;
                    if (to_read > sizeof(buffer)) {
                        to_read = sizeof(buffer);
                    }

                    size_t so_far = 0;
                    while (so_far < to_read) {
                        ssize_t read_count =
                                TEMP_FAILURE_RETRY(ota_read(fd, buffer+so_far, to_read-so_far));
                        if (read_count == -1) {
                            printf("verify read error %s at %zu: %s\n",
                                   partition, p, strerror(errno));
                            return -1;
                        }
                        if (static_cast<size_t>(read_count) < to_read) {
                            printf("short verify read %s at %zu: %zd %zu %s\n",
                                   partition, p, read_count, to_read, strerror(errno));
                        }
                        so_far += read_count;
                    }

                    if (memcmp(buffer, data+p, to_read) != 0) {
                        printf("verification failed starting at %zu\n", p);
                        start = p;
                        break;
                    }
                }

                if (start == len) {
                    printf("verification read succeeded (attempt %zu)\n", attempt+1);
                    success = true;
                    break;
                }
            }

            if (!success) {
                printf("failed to verify after all attempts\n");
                return -1;
            }

            if (ota_close(fd) != 0) {
                printf("error closing %s (%s)\n", partition, strerror(errno));
                return -1;
            }
            sync();
            break;
        }
    }

    return 0;
}


// Take a string 'str' of 40 hex digits and parse it into the 20
// byte array 'digest'.  'str' may contain only the digest or be of
// the form "<digest>:<anything>".  Return 0 on success, -1 on any
// error.
int ParseSha1(const char* str, uint8_t* digest) {
    const char* ps = str;
    uint8_t* pd = digest;
    for (int i = 0; i < SHA_DIGEST_LENGTH * 2; ++i, ++ps) {
        int digit;
        if (*ps >= '0' && *ps <= '9') {
            digit = *ps - '0';
        } else if (*ps >= 'a' && *ps <= 'f') {
            digit = *ps - 'a' + 10;
        } else if (*ps >= 'A' && *ps <= 'F') {
            digit = *ps - 'A' + 10;
        } else {
            return -1;
        }
        if (i % 2 == 0) {
            *pd = digit << 4;
        } else {
            *pd |= digit;
            ++pd;
        }
    }
    if (*ps != '\0') return -1;
    return 0;
}

// Search an array of sha1 strings for one matching the given sha1.
// Return the index of the match on success, or -1 if no match is
// found.
int FindMatchingPatch(uint8_t* sha1, char* const * const patch_sha1_str,
                      int num_patches) {
    uint8_t patch_sha1[SHA_DIGEST_LENGTH];
    for (int i = 0; i < num_patches; ++i) {
        if (ParseSha1(patch_sha1_str[i], patch_sha1) == 0 &&
            memcmp(patch_sha1, sha1, SHA_DIGEST_LENGTH) == 0) {
            return i;
        }
    }
    return -1;
}

// Returns 0 if the contents of the file (argv[2]) or the cached file
// match any of the sha1's on the command line (argv[3:]).  Returns
// nonzero otherwise.
int applypatch_check(const char* filename, int num_patches,
                     char** const patch_sha1_str) {
    FileContents file;

    // It's okay to specify no sha1s; the check will pass if the
    // LoadFileContents is successful.  (Useful for reading
    // partitions, where the filename encodes the sha1s; no need to
    // check them twice.)
    if (LoadFileContents(filename, &file) != 0 ||
        (num_patches > 0 &&
         FindMatchingPatch(file.sha1, patch_sha1_str, num_patches) < 0)) {
        printf("file \"%s\" doesn't have any of expected "
               "sha1 sums; checking cache\n", filename);

        // If the source file is missing or corrupted, it might be because
        // we were killed in the middle of patching it.  A copy of it
        // should have been made in CACHE_TEMP_SOURCE.  If that file
        // exists and matches the sha1 we're looking for, the check still
        // passes.

        if (LoadFileContents(CACHE_TEMP_SOURCE, &file) != 0) {
            printf("failed to load cache file\n");
            return 1;
        }

        if (FindMatchingPatch(file.sha1, patch_sha1_str, num_patches) < 0) {
            printf("cache bits don't match any sha1 for \"%s\"\n", filename);
            return 1;
        }
    }
    return 0;
}

int ShowLicenses() {
    ShowBSDiffLicense();
    return 0;
}

ssize_t FileSink(const unsigned char* data, ssize_t len, void* token) {
    int fd = *static_cast<int*>(token);
    ssize_t done = 0;
    ssize_t wrote;
    while (done < len) {
        wrote = TEMP_FAILURE_RETRY(ota_write(fd, data+done, len-done));
        if (wrote == -1) {
            printf("error writing %zd bytes: %s\n", (len-done), strerror(errno));
            return done;
        }
        done += wrote;
    }
    return done;
}

ssize_t MemorySink(const unsigned char* data, ssize_t len, void* token) {
    std::string* s = static_cast<std::string*>(token);
    s->append(reinterpret_cast<const char*>(data), len);
    return len;
}

// Return the amount of free space (in bytes) on the filesystem
// containing filename.  filename must exist.  Return -1 on error.
size_t FreeSpaceForFile(const char* filename) {
    struct statfs sf;
    if (statfs(filename, &sf) != 0) {
        printf("failed to statfs %s: %s\n", filename, strerror(errno));
        return -1;
    }
    return sf.f_bsize * sf.f_bavail;
}

int CacheSizeCheck(size_t bytes) {
    if (MakeFreeSpaceOnCache(bytes) < 0) {
        printf("unable to make %ld bytes available on /cache\n", (long)bytes);
        return 1;
    } else {
        return 0;
    }
}

// This function applies binary patches to files in a way that is safe
// (the original file is not touched until we have the desired
// replacement for it) and idempotent (it's okay to run this program
// multiple times).
//
// - if the sha1 hash of <target_filename> is <target_sha1_string>,
//   does nothing and exits successfully.
//
// - otherwise, if the sha1 hash of <source_filename> is one of the
//   entries in <patch_sha1_str>, the corresponding patch from
//   <patch_data> (which must be a VAL_BLOB) is applied to produce a
//   new file (the type of patch is automatically detected from the
//   blob data).  If that new file has sha1 hash <target_sha1_str>,
//   moves it to replace <target_filename>, and exits successfully.
//   Note that if <source_filename> and <target_filename> are not the
//   same, <source_filename> is NOT deleted on success.
//   <target_filename> may be the string "-" to mean "the same as
//   source_filename".
//
// - otherwise, or if any error is encountered, exits with non-zero
//   status.
//
// <source_filename> may refer to a partition to read the source data.
// See the comments for the LoadPartitionContents() function above
// for the format of such a filename.

int applypatch(const char* source_filename,
               const char* target_filename,
               const char* target_sha1_str,
               size_t target_size,
               int num_patches,
               char** const patch_sha1_str,
               Value** patch_data,
               Value* bonus_data) {
    printf("patch %s: ", source_filename);

    if (target_filename[0] == '-' && target_filename[1] == '\0') {
        target_filename = source_filename;
    }

    uint8_t target_sha1[SHA_DIGEST_LENGTH];
    if (ParseSha1(target_sha1_str, target_sha1) != 0) {
        printf("failed to parse tgt-sha1 \"%s\"\n", target_sha1_str);
        return 1;
    }

    FileContents copy_file;
    FileContents source_file;
    const Value* source_patch_value = NULL;
    const Value* copy_patch_value = NULL;

    // We try to load the target file into the source_file object.
    if (LoadFileContents(target_filename, &source_file) == 0) {
        if (memcmp(source_file.sha1, target_sha1, SHA_DIGEST_LENGTH) == 0) {
            // The early-exit case:  the patch was already applied, this file
            // has the desired hash, nothing for us to do.
            printf("already %s\n", short_sha1(target_sha1).c_str());
            return 0;
        }
    }

    if (source_file.data.empty() ||
        (target_filename != source_filename &&
         strcmp(target_filename, source_filename) != 0)) {
        // Need to load the source file:  either we failed to load the
        // target file, or we did but it's different from the source file.
        source_file.data.clear();
        LoadFileContents(source_filename, &source_file);
    }

    if (!source_file.data.empty()) {
        int to_use = FindMatchingPatch(source_file.sha1, patch_sha1_str, num_patches);
        if (to_use >= 0) {
            source_patch_value = patch_data[to_use];
        }
    }

    if (source_patch_value == NULL) {
        source_file.data.clear();
        printf("source file is bad; trying copy\n");

        if (LoadFileContents(CACHE_TEMP_SOURCE, &copy_file) < 0) {
            // fail.
            printf("failed to read copy file\n");
            return 1;
        }

        int to_use = FindMatchingPatch(copy_file.sha1, patch_sha1_str, num_patches);
        if (to_use >= 0) {
            copy_patch_value = patch_data[to_use];
        }

        if (copy_patch_value == NULL) {
            // fail.
            printf("copy file doesn't match source SHA-1s either\n");
            return 1;
        }
    }

    return GenerateTarget(&source_file, source_patch_value,
                          &copy_file, copy_patch_value,
                          source_filename, target_filename,
                          target_sha1, target_size, bonus_data);
}

/*
 * This function flashes a given image to the target partition. It verifies
 * the target cheksum first, and will return if target has the desired hash.
 * It checks the checksum of the given source image before flashing, and
 * verifies the target partition afterwards. The function is idempotent.
 * Returns zero on success.
 */
int applypatch_flash(const char* source_filename, const char* target_filename,
                     const char* target_sha1_str, size_t target_size) {
    printf("flash %s: ", target_filename);

    uint8_t target_sha1[SHA_DIGEST_LENGTH];
    if (ParseSha1(target_sha1_str, target_sha1) != 0) {
        printf("failed to parse tgt-sha1 \"%s\"\n", target_sha1_str);
        return 1;
    }

    FileContents source_file;
    std::string target_str(target_filename);

    std::vector<std::string> pieces = android::base::Split(target_str, ":");
    if (pieces.size() != 2 || (pieces[0] != "MTD" && pieces[0] != "EMMC")) {
        printf("invalid target name \"%s\"", target_filename);
        return 1;
    }

    // Load the target into the source_file object to see if already applied.
    pieces.push_back(std::to_string(target_size));
    pieces.push_back(target_sha1_str);
    std::string fullname = android::base::Join(pieces, ':');
    if (LoadPartitionContents(fullname.c_str(), &source_file) == 0 &&
        memcmp(source_file.sha1, target_sha1, SHA_DIGEST_LENGTH) == 0) {
        // The early-exit case: the image was already applied, this partition
        // has the desired hash, nothing for us to do.
        printf("already %s\n", short_sha1(target_sha1).c_str());
        return 0;
    }

    if (LoadFileContents(source_filename, &source_file) == 0) {
        if (memcmp(source_file.sha1, target_sha1, SHA_DIGEST_LENGTH) != 0) {
            // The source doesn't have desired checksum.
            printf("source \"%s\" doesn't have expected sha1 sum\n", source_filename);
            printf("expected: %s, found: %s\n", short_sha1(target_sha1).c_str(),
                    short_sha1(source_file.sha1).c_str());
            return 1;
        }
    }

    if (WriteToPartition(source_file.data.data(), target_size, target_filename) != 0) {
        printf("write of copied data to %s failed\n", target_filename);
        return 1;
    }
    return 0;
}

static int GenerateTarget(FileContents* source_file,
                          const Value* source_patch_value,
                          FileContents* copy_file,
                          const Value* copy_patch_value,
                          const char* source_filename,
                          const char* target_filename,
                          const uint8_t target_sha1[SHA_DIGEST_LENGTH],
                          size_t target_size,
                          const Value* bonus_data) {
    int retry = 1;
    SHA_CTX ctx;
    std::string memory_sink_str;
    FileContents* source_to_use;
    int made_copy = 0;

    bool target_is_partition = (strncmp(target_filename, "MTD:", 4) == 0 ||
                                strncmp(target_filename, "EMMC:", 5) == 0);
    const std::string tmp_target_filename = std::string(target_filename) + ".patch";

    // assume that target_filename (eg "/system/app/Foo.apk") is located
    // on the same filesystem as its top-level directory ("/system").
    // We need something that exists for calling statfs().
    std::string target_fs = target_filename;
    auto slash_pos = target_fs.find('/', 1);
    if (slash_pos != std::string::npos) {
        target_fs.resize(slash_pos);
    }

    const Value* patch;
    if (source_patch_value != NULL) {
        source_to_use = source_file;
        patch = source_patch_value;
    } else {
        source_to_use = copy_file;
        patch = copy_patch_value;
    }
    if (patch->type != VAL_BLOB) {
        printf("patch is not a blob\n");
        return 1;
    }
    char* header = patch->data;
    ssize_t header_bytes_read = patch->size;
    bool use_bsdiff = false;
    if (header_bytes_read >= 8 && memcmp(header, "BSDIFF40", 8) == 0) {
        use_bsdiff = true;
    } else if (header_bytes_read >= 8 && memcmp(header, "IMGDIFF2", 8) == 0) {
        use_bsdiff = false;
    } else {
        printf("Unknown patch file format\n");
        return 1;
    }

    do {
        // Is there enough room in the target filesystem to hold the patched
        // file?

        if (target_is_partition) {
            // If the target is a partition, we're actually going to
            // write the output to /tmp and then copy it to the
            // partition.  statfs() always returns 0 blocks free for
            // /tmp, so instead we'll just assume that /tmp has enough
            // space to hold the file.

            // We still write the original source to cache, in case
            // the partition write is interrupted.
            if (MakeFreeSpaceOnCache(source_file->data.size()) < 0) {
                printf("not enough free space on /cache\n");
                return 1;
            }
            if (SaveFileContents(CACHE_TEMP_SOURCE, source_file) < 0) {
                printf("failed to back up source file\n");
                return 1;
            }
            made_copy = 1;
            retry = 0;
        } else {
            int enough_space = 0;
            if (retry > 0) {
                size_t free_space = FreeSpaceForFile(target_fs.c_str());
                enough_space =
                    (free_space > (256 << 10)) &&          // 256k (two-block) minimum
                    (free_space > (target_size * 3 / 2));  // 50% margin of error
                if (!enough_space) {
                    printf("target %zu bytes; free space %zu bytes; retry %d; enough %d\n",
                           target_size, free_space, retry, enough_space);
                }
            }

            if (!enough_space) {
                retry = 0;
            }

            if (!enough_space && source_patch_value != NULL) {
                // Using the original source, but not enough free space.  First
                // copy the source file to cache, then delete it from the original
                // location.

                if (strncmp(source_filename, "MTD:", 4) == 0 ||
                    strncmp(source_filename, "EMMC:", 5) == 0) {
                    // It's impossible to free space on the target filesystem by
                    // deleting the source if the source is a partition.  If
                    // we're ever in a state where we need to do this, fail.
                    printf("not enough free space for target but source is partition\n");
                    return 1;
                }

                if (MakeFreeSpaceOnCache(source_file->data.size()) < 0) {
                    printf("not enough free space on /cache\n");
                    return 1;
                }

                if (SaveFileContents(CACHE_TEMP_SOURCE, source_file) < 0) {
                    printf("failed to back up source file\n");
                    return 1;
                }
                made_copy = 1;
                unlink(source_filename);

                size_t free_space = FreeSpaceForFile(target_fs.c_str());
                printf("(now %zu bytes free for target) ", free_space);
            }
        }


        SinkFn sink = NULL;
        void* token = NULL;
        int output_fd = -1;
        if (target_is_partition) {
            // We store the decoded output in memory.
            sink = MemorySink;
            token = &memory_sink_str;
        } else {
            // We write the decoded output to "<tgt-file>.patch".
            output_fd = ota_open(tmp_target_filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_SYNC,
                          S_IRUSR | S_IWUSR);
            if (output_fd < 0) {
                printf("failed to open output file %s: %s\n", tmp_target_filename.c_str(),
                       strerror(errno));
                return 1;
            }
            sink = FileSink;
            token = &output_fd;
        }


        SHA1_Init(&ctx);

        int result;
        if (use_bsdiff) {
            result = ApplyBSDiffPatch(source_to_use->data.data(), source_to_use->data.size(),
                                      patch, 0, sink, token, &ctx);
        } else {
            result = ApplyImagePatch(source_to_use->data.data(), source_to_use->data.size(),
                                     patch, sink, token, &ctx, bonus_data);
        }

        if (!target_is_partition) {
            if (ota_fsync(output_fd) != 0) {
                printf("failed to fsync file \"%s\" (%s)\n", tmp_target_filename.c_str(),
                       strerror(errno));
                result = 1;
            }
            if (ota_close(output_fd) != 0) {
                printf("failed to close file \"%s\" (%s)\n", tmp_target_filename.c_str(),
                       strerror(errno));
                result = 1;
            }
        }

        if (result != 0) {
            if (retry == 0) {
                printf("applying patch failed\n");
                return result != 0;
            } else {
                printf("applying patch failed; retrying\n");
            }
            if (!target_is_partition) {
                unlink(tmp_target_filename.c_str());
            }
        } else {
            // succeeded; no need to retry
            break;
        }
    } while (retry-- > 0);

    uint8_t current_target_sha1[SHA_DIGEST_LENGTH];
    SHA1_Final(current_target_sha1, &ctx);
    if (memcmp(current_target_sha1, target_sha1, SHA_DIGEST_LENGTH) != 0) {
        printf("patch did not produce expected sha1\n");
        return 1;
    } else {
        printf("now %s\n", short_sha1(target_sha1).c_str());
    }

    if (target_is_partition) {
        // Copy the temp file to the partition.
        if (WriteToPartition(reinterpret_cast<const unsigned char*>(memory_sink_str.c_str()),
                             memory_sink_str.size(), target_filename) != 0) {
            printf("write of patched data to %s failed\n", target_filename);
            return 1;
        }
    } else {
        // Give the .patch file the same owner, group, and mode of the
        // original source file.
        if (chmod(tmp_target_filename.c_str(), source_to_use->st.st_mode) != 0) {
            printf("chmod of \"%s\" failed: %s\n", tmp_target_filename.c_str(), strerror(errno));
            return 1;
        }
        if (chown(tmp_target_filename.c_str(), source_to_use->st.st_uid, source_to_use->st.st_gid) != 0) {
            printf("chown of \"%s\" failed: %s\n", tmp_target_filename.c_str(), strerror(errno));
            return 1;
        }

        // Finally, rename the .patch file to replace the target file.
        if (rename(tmp_target_filename.c_str(), target_filename) != 0) {
            printf("rename of .patch to \"%s\" failed: %s\n", target_filename, strerror(errno));
            return 1;
        }
    }

    // If this run of applypatch created the copy, and we're here, we
    // can delete it.
    if (made_copy) {
        unlink(CACHE_TEMP_SOURCE);
    }

    // Success!
    return 0;
}
