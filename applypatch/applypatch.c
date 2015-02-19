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
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdbool.h>

#include "mincrypt/sha.h"
#include "applypatch.h"
#include "mtdutils/mtdutils.h"
#include "edify/expr.h"

static int LoadPartitionContents(const char* filename, FileContents* file);
static ssize_t FileSink(const unsigned char* data, ssize_t len, void* token);
static int GenerateTarget(FileContents* source_file,
                          const Value* source_patch_value,
                          FileContents* copy_file,
                          const Value* copy_patch_value,
                          const char* source_filename,
                          const char* target_filename,
                          const uint8_t target_sha1[SHA_DIGEST_SIZE],
                          size_t target_size,
                          const Value* bonus_data);

static int mtd_partitions_scanned = 0;

// Read a file into memory; store the file contents and associated
// metadata in *file.
//
// Return 0 on success.
int LoadFileContents(const char* filename, FileContents* file) {
    file->data = NULL;

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

    file->size = file->st.st_size;
    file->data = malloc(file->size);

    FILE* f = fopen(filename, "rb");
    if (f == NULL) {
        printf("failed to open \"%s\": %s\n", filename, strerror(errno));
        free(file->data);
        file->data = NULL;
        return -1;
    }

    ssize_t bytes_read = fread(file->data, 1, file->size, f);
    if (bytes_read != file->size) {
        printf("short read of \"%s\" (%ld bytes of %ld)\n",
               filename, (long)bytes_read, (long)file->size);
        free(file->data);
        file->data = NULL;
        return -1;
    }
    fclose(f);

    SHA_hash(file->data, file->size, file->sha1);
    return 0;
}

static size_t* size_array;
// comparison function for qsort()ing an int array of indexes into
// size_array[].
static int compare_size_indices(const void* a, const void* b) {
    int aa = *(int*)a;
    int bb = *(int*)b;
    if (size_array[aa] < size_array[bb]) {
        return -1;
    } else if (size_array[aa] > size_array[bb]) {
        return 1;
    } else {
        return 0;
    }
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
    char* copy = strdup(filename);
    const char* magic = strtok(copy, ":");

    enum PartitionType type;

    if (strcmp(magic, "MTD") == 0) {
        type = MTD;
    } else if (strcmp(magic, "EMMC") == 0) {
        type = EMMC;
    } else {
        printf("LoadPartitionContents called with bad filename (%s)\n",
               filename);
        return -1;
    }
    const char* partition = strtok(NULL, ":");

    int i;
    int colons = 0;
    for (i = 0; filename[i] != '\0'; ++i) {
        if (filename[i] == ':') {
            ++colons;
        }
    }
    if (colons < 3 || colons%2 == 0) {
        printf("LoadPartitionContents called with bad filename (%s)\n",
               filename);
    }

    int pairs = (colons-1)/2;     // # of (size,sha1) pairs in filename
    int* index = malloc(pairs * sizeof(int));
    size_t* size = malloc(pairs * sizeof(size_t));
    char** sha1sum = malloc(pairs * sizeof(char*));

    for (i = 0; i < pairs; ++i) {
        const char* size_str = strtok(NULL, ":");
        size[i] = strtol(size_str, NULL, 10);
        if (size[i] == 0) {
            printf("LoadPartitionContents called with bad size (%s)\n", filename);
            return -1;
        }
        sha1sum[i] = strtok(NULL, ":");
        index[i] = i;
    }

    // sort the index[] array so it indexes the pairs in order of
    // increasing size.
    size_array = size;
    qsort(index, pairs, sizeof(int), compare_size_indices);

    MtdReadContext* ctx = NULL;
    FILE* dev = NULL;

    switch (type) {
        case MTD:
            if (!mtd_partitions_scanned) {
                mtd_scan_partitions();
                mtd_partitions_scanned = 1;
            }

            const MtdPartition* mtd = mtd_find_partition_by_name(partition);
            if (mtd == NULL) {
                printf("mtd partition \"%s\" not found (loading %s)\n",
                       partition, filename);
                return -1;
            }

            ctx = mtd_read_partition(mtd);
            if (ctx == NULL) {
                printf("failed to initialize read of mtd partition \"%s\"\n",
                       partition);
                return -1;
            }
            break;

        case EMMC:
            dev = fopen(partition, "rb");
            if (dev == NULL) {
                printf("failed to open emmc partition \"%s\": %s\n",
                       partition, strerror(errno));
                return -1;
            }
    }

    SHA_CTX sha_ctx;
    SHA_init(&sha_ctx);
    uint8_t parsed_sha[SHA_DIGEST_SIZE];

    // allocate enough memory to hold the largest size.
    file->data = malloc(size[index[pairs-1]]);
    char* p = (char*)file->data;
    file->size = 0;                // # bytes read so far

    for (i = 0; i < pairs; ++i) {
        // Read enough additional bytes to get us up to the next size
        // (again, we're trying the possibilities in order of increasing
        // size).
        size_t next = size[index[i]] - file->size;
        size_t read = 0;
        if (next > 0) {
            switch (type) {
                case MTD:
                    read = mtd_read_data(ctx, p, next);
                    break;

                case EMMC:
                    read = fread(p, 1, next, dev);
                    break;
            }
            if (next != read) {
                printf("short read (%zu bytes of %zu) for partition \"%s\"\n",
                       read, next, partition);
                free(file->data);
                file->data = NULL;
                return -1;
            }
            SHA_update(&sha_ctx, p, read);
            file->size += read;
        }

        // Duplicate the SHA context and finalize the duplicate so we can
        // check it against this pair's expected hash.
        SHA_CTX temp_ctx;
        memcpy(&temp_ctx, &sha_ctx, sizeof(SHA_CTX));
        const uint8_t* sha_so_far = SHA_final(&temp_ctx);

        if (ParseSha1(sha1sum[index[i]], parsed_sha) != 0) {
            printf("failed to parse sha1 %s in %s\n",
                   sha1sum[index[i]], filename);
            free(file->data);
            file->data = NULL;
            return -1;
        }

        if (memcmp(sha_so_far, parsed_sha, SHA_DIGEST_SIZE) == 0) {
            // we have a match.  stop reading the partition; we'll return
            // the data we've read so far.
            printf("partition read matched size %zu sha %s\n",
                   size[index[i]], sha1sum[index[i]]);
            break;
        }

        p += read;
    }

    switch (type) {
        case MTD:
            mtd_read_close(ctx);
            break;

        case EMMC:
            fclose(dev);
            break;
    }


    if (i == pairs) {
        // Ran off the end of the list of (size,sha1) pairs without
        // finding a match.
        printf("contents of partition \"%s\" didn't match %s\n",
               partition, filename);
        free(file->data);
        file->data = NULL;
        return -1;
    }

    const uint8_t* sha_final = SHA_final(&sha_ctx);
    for (i = 0; i < SHA_DIGEST_SIZE; ++i) {
        file->sha1[i] = sha_final[i];
    }

    // Fake some stat() info.
    file->st.st_mode = 0644;
    file->st.st_uid = 0;
    file->st.st_gid = 0;

    free(copy);
    free(index);
    free(size);
    free(sha1sum);

    return 0;
}


// Save the contents of the given FileContents object under the given
// filename.  Return 0 on success.
int SaveFileContents(const char* filename, const FileContents* file) {
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_SYNC, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        printf("failed to open \"%s\" for write: %s\n",
               filename, strerror(errno));
        return -1;
    }

    ssize_t bytes_written = FileSink(file->data, file->size, &fd);
    if (bytes_written != file->size) {
        printf("short write of \"%s\" (%ld bytes of %ld) (%s)\n",
               filename, (long)bytes_written, (long)file->size,
               strerror(errno));
        close(fd);
        return -1;
    }
    if (fsync(fd) != 0) {
        printf("fsync of \"%s\" failed: %s\n", filename, strerror(errno));
        return -1;
    }
    if (close(fd) != 0) {
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
// "MTD:<partition>[:...]" or "EMMC:<partition_device>:".  Return 0 on
// success.
int WriteToPartition(unsigned char* data, size_t len,
                        const char* target) {
    char* copy = strdup(target);
    const char* magic = strtok(copy, ":");

    enum PartitionType type;
    if (strcmp(magic, "MTD") == 0) {
        type = MTD;
    } else if (strcmp(magic, "EMMC") == 0) {
        type = EMMC;
    } else {
        printf("WriteToPartition called with bad target (%s)\n", target);
        return -1;
    }
    const char* partition = strtok(NULL, ":");

    if (partition == NULL) {
        printf("bad partition target name \"%s\"\n", target);
        return -1;
    }

    switch (type) {
        case MTD:
            if (!mtd_partitions_scanned) {
                mtd_scan_partitions();
                mtd_partitions_scanned = 1;
            }

            const MtdPartition* mtd = mtd_find_partition_by_name(partition);
            if (mtd == NULL) {
                printf("mtd partition \"%s\" not found for writing\n",
                       partition);
                return -1;
            }

            MtdWriteContext* ctx = mtd_write_partition(mtd);
            if (ctx == NULL) {
                printf("failed to init mtd partition \"%s\" for writing\n",
                       partition);
                return -1;
            }

            size_t written = mtd_write_data(ctx, (char*)data, len);
            if (written != len) {
                printf("only wrote %zu of %zu bytes to MTD %s\n",
                       written, len, partition);
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

        case EMMC:
        {
            size_t start = 0;
            int success = 0;
            int fd = open(partition, O_RDWR | O_SYNC);
            if (fd < 0) {
                printf("failed to open %s: %s\n", partition, strerror(errno));
                return -1;
            }
            int attempt;

            for (attempt = 0; attempt < 2; ++attempt) {
                lseek(fd, start, SEEK_SET);
                while (start < len) {
                    size_t to_write = len - start;
                    if (to_write > 1<<20) to_write = 1<<20;

                    ssize_t written = write(fd, data+start, to_write);
                    if (written < 0) {
                        if (errno == EINTR) {
                            written = 0;
                        } else {
                            printf("failed write writing to %s (%s)\n",
                                   partition, strerror(errno));
                            return -1;
                        }
                    }
                    start += written;
                }
                if (fsync(fd) != 0) {
                   printf("failed to sync to %s (%s)\n",
                          partition, strerror(errno));
                   return -1;
                }
                if (close(fd) != 0) {
                   printf("failed to close %s (%s)\n",
                          partition, strerror(errno));
                   return -1;
                }
                fd = open(partition, O_RDONLY);
                if (fd < 0) {
                   printf("failed to reopen %s for verify (%s)\n",
                          partition, strerror(errno));
                   return -1;
                }

                // drop caches so our subsequent verification read
                // won't just be reading the cache.
                sync();
                int dc = open("/proc/sys/vm/drop_caches", O_WRONLY);
                write(dc, "3\n", 2);
                close(dc);
                sleep(1);
                printf("  caches dropped\n");

                // verify
                lseek(fd, 0, SEEK_SET);
                unsigned char buffer[4096];
                start = len;
                size_t p;
                for (p = 0; p < len; p += sizeof(buffer)) {
                    size_t to_read = len - p;
                    if (to_read > sizeof(buffer)) to_read = sizeof(buffer);

                    size_t so_far = 0;
                    while (so_far < to_read) {
                        ssize_t read_count = read(fd, buffer+so_far, to_read-so_far);
                        if (read_count < 0) {
                            if (errno == EINTR) {
                                read_count = 0;
                            } else {
                                printf("verify read error %s at %zu: %s\n",
                                       partition, p, strerror(errno));
                                return -1;
                            }
                        }
                        if ((size_t)read_count < to_read) {
                            printf("short verify read %s at %zu: %zd %zu %s\n",
                                   partition, p, read_count, to_read, strerror(errno));
                        }
                        so_far += read_count;
                    }

                    if (memcmp(buffer, data+p, to_read)) {
                        printf("verification failed starting at %zu\n", p);
                        start = p;
                        break;
                    }
                }

                if (start == len) {
                    printf("verification read succeeded (attempt %d)\n", attempt+1);
                    success = true;
                    break;
                }
            }

            if (!success) {
                printf("failed to verify after all attempts\n");
                return -1;
            }

            if (close(fd) != 0) {
                printf("error closing %s (%s)\n", partition, strerror(errno));
                return -1;
            }
            sync();
            break;
        }
    }

    free(copy);
    return 0;
}


// Take a string 'str' of 40 hex digits and parse it into the 20
// byte array 'digest'.  'str' may contain only the digest or be of
// the form "<digest>:<anything>".  Return 0 on success, -1 on any
// error.
int ParseSha1(const char* str, uint8_t* digest) {
    int i;
    const char* ps = str;
    uint8_t* pd = digest;
    for (i = 0; i < SHA_DIGEST_SIZE * 2; ++i, ++ps) {
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
    int i;
    uint8_t patch_sha1[SHA_DIGEST_SIZE];
    for (i = 0; i < num_patches; ++i) {
        if (ParseSha1(patch_sha1_str[i], patch_sha1) == 0 &&
            memcmp(patch_sha1, sha1, SHA_DIGEST_SIZE) == 0) {
            return i;
        }
    }
    return -1;
}

// Returns 0 if the contents of the file (argv[2]) or the cached file
// match any of the sha1's on the command line (argv[3:]).  Returns
// nonzero otherwise.
int applypatch_check(const char* filename,
                     int num_patches, char** const patch_sha1_str) {
    FileContents file;
    file.data = NULL;

    // It's okay to specify no sha1s; the check will pass if the
    // LoadFileContents is successful.  (Useful for reading
    // partitions, where the filename encodes the sha1s; no need to
    // check them twice.)
    if (LoadFileContents(filename, &file) != 0 ||
        (num_patches > 0 &&
         FindMatchingPatch(file.sha1, patch_sha1_str, num_patches) < 0)) {
        printf("file \"%s\" doesn't have any of expected "
               "sha1 sums; checking cache\n", filename);

        free(file.data);
        file.data = NULL;

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
            free(file.data);
            return 1;
        }
    }

    free(file.data);
    return 0;
}

int ShowLicenses() {
    ShowBSDiffLicense();
    return 0;
}

ssize_t FileSink(const unsigned char* data, ssize_t len, void* token) {
    int fd = *(int *)token;
    ssize_t done = 0;
    ssize_t wrote;
    while (done < (ssize_t) len) {
        wrote = write(fd, data+done, len-done);
        if (wrote <= 0) {
            printf("error writing %d bytes: %s\n", (int)(len-done), strerror(errno));
            return done;
        }
        done += wrote;
    }
    return done;
}

typedef struct {
    unsigned char* buffer;
    ssize_t size;
    ssize_t pos;
} MemorySinkInfo;

ssize_t MemorySink(const unsigned char* data, ssize_t len, void* token) {
    MemorySinkInfo* msi = (MemorySinkInfo*)token;
    if (msi->size - msi->pos < len) {
        return -1;
    }
    memcpy(msi->buffer + msi->pos, data, len);
    msi->pos += len;
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
    return sf.f_bsize * sf.f_bfree;
}

int CacheSizeCheck(size_t bytes) {
    if (MakeFreeSpaceOnCache(bytes) < 0) {
        printf("unable to make %ld bytes available on /cache\n", (long)bytes);
        return 1;
    } else {
        return 0;
    }
}

static void print_short_sha1(const uint8_t sha1[SHA_DIGEST_SIZE]) {
    int i;
    const char* hex = "0123456789abcdef";
    for (i = 0; i < 4; ++i) {
        putchar(hex[(sha1[i]>>4) & 0xf]);
        putchar(hex[sha1[i] & 0xf]);
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
//   blob daat).  If that new file has sha1 hash <target_sha1_str>,
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
// See the comments for the LoadPartition Contents() function above
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

    if (target_filename[0] == '-' &&
        target_filename[1] == '\0') {
        target_filename = source_filename;
    }

    uint8_t target_sha1[SHA_DIGEST_SIZE];
    if (ParseSha1(target_sha1_str, target_sha1) != 0) {
        printf("failed to parse tgt-sha1 \"%s\"\n", target_sha1_str);
        return 1;
    }

    FileContents copy_file;
    FileContents source_file;
    copy_file.data = NULL;
    source_file.data = NULL;
    const Value* source_patch_value = NULL;
    const Value* copy_patch_value = NULL;

    // We try to load the target file into the source_file object.
    if (LoadFileContents(target_filename, &source_file) == 0) {
        if (memcmp(source_file.sha1, target_sha1, SHA_DIGEST_SIZE) == 0) {
            // The early-exit case:  the patch was already applied, this file
            // has the desired hash, nothing for us to do.
            printf("already ");
            print_short_sha1(target_sha1);
            putchar('\n');
            free(source_file.data);
            return 0;
        }
    }

    if (source_file.data == NULL ||
        (target_filename != source_filename &&
         strcmp(target_filename, source_filename) != 0)) {
        // Need to load the source file:  either we failed to load the
        // target file, or we did but it's different from the source file.
        free(source_file.data);
        source_file.data = NULL;
        LoadFileContents(source_filename, &source_file);
    }

    if (source_file.data != NULL) {
        int to_use = FindMatchingPatch(source_file.sha1,
                                       patch_sha1_str, num_patches);
        if (to_use >= 0) {
            source_patch_value = patch_data[to_use];
        }
    }

    if (source_patch_value == NULL) {
        free(source_file.data);
        source_file.data = NULL;
        printf("source file is bad; trying copy\n");

        if (LoadFileContents(CACHE_TEMP_SOURCE, &copy_file) < 0) {
            // fail.
            printf("failed to read copy file\n");
            return 1;
        }

        int to_use = FindMatchingPatch(copy_file.sha1,
                                       patch_sha1_str, num_patches);
        if (to_use >= 0) {
            copy_patch_value = patch_data[to_use];
        }

        if (copy_patch_value == NULL) {
            // fail.
            printf("copy file doesn't match source SHA-1s either\n");
            free(copy_file.data);
            return 1;
        }
    }

    int result = GenerateTarget(&source_file, source_patch_value,
                                &copy_file, copy_patch_value,
                                source_filename, target_filename,
                                target_sha1, target_size, bonus_data);
    free(source_file.data);
    free(copy_file.data);

    return result;
}

static int GenerateTarget(FileContents* source_file,
                          const Value* source_patch_value,
                          FileContents* copy_file,
                          const Value* copy_patch_value,
                          const char* source_filename,
                          const char* target_filename,
                          const uint8_t target_sha1[SHA_DIGEST_SIZE],
                          size_t target_size,
                          const Value* bonus_data) {
    int retry = 1;
    SHA_CTX ctx;
    int output;
    MemorySinkInfo msi;
    FileContents* source_to_use;
    char* outname;
    int made_copy = 0;

    // assume that target_filename (eg "/system/app/Foo.apk") is located
    // on the same filesystem as its top-level directory ("/system").
    // We need something that exists for calling statfs().
    char target_fs[strlen(target_filename)+1];
    char* slash = strchr(target_filename+1, '/');
    if (slash != NULL) {
        int count = slash - target_filename;
        strncpy(target_fs, target_filename, count);
        target_fs[count] = '\0';
    } else {
        strcpy(target_fs, target_filename);
    }

    do {
        // Is there enough room in the target filesystem to hold the patched
        // file?

        if (strncmp(target_filename, "MTD:", 4) == 0 ||
            strncmp(target_filename, "EMMC:", 5) == 0) {
            // If the target is a partition, we're actually going to
            // write the output to /tmp and then copy it to the
            // partition.  statfs() always returns 0 blocks free for
            // /tmp, so instead we'll just assume that /tmp has enough
            // space to hold the file.

            // We still write the original source to cache, in case
            // the partition write is interrupted.
            if (MakeFreeSpaceOnCache(source_file->size) < 0) {
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
                size_t free_space = FreeSpaceForFile(target_fs);
                enough_space =
                    (free_space > (256 << 10)) &&          // 256k (two-block) minimum
                    (free_space > (target_size * 3 / 2));  // 50% margin of error
                if (!enough_space) {
                    printf("target %ld bytes; free space %ld bytes; retry %d; enough %d\n",
                           (long)target_size, (long)free_space, retry, enough_space);
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
                    printf("not enough free space for target but source "
                           "is partition\n");
                    return 1;
                }

                if (MakeFreeSpaceOnCache(source_file->size) < 0) {
                    printf("not enough free space on /cache\n");
                    return 1;
                }

                if (SaveFileContents(CACHE_TEMP_SOURCE, source_file) < 0) {
                    printf("failed to back up source file\n");
                    return 1;
                }
                made_copy = 1;
                unlink(source_filename);

                size_t free_space = FreeSpaceForFile(target_fs);
                printf("(now %ld bytes free for target) ", (long)free_space);
            }
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

        SinkFn sink = NULL;
        void* token = NULL;
        output = -1;
        outname = NULL;
        if (strncmp(target_filename, "MTD:", 4) == 0 ||
            strncmp(target_filename, "EMMC:", 5) == 0) {
            // We store the decoded output in memory.
            msi.buffer = malloc(target_size);
            if (msi.buffer == NULL) {
                printf("failed to alloc %ld bytes for output\n",
                       (long)target_size);
                return 1;
            }
            msi.pos = 0;
            msi.size = target_size;
            sink = MemorySink;
            token = &msi;
        } else {
            // We write the decoded output to "<tgt-file>.patch".
            outname = (char*)malloc(strlen(target_filename) + 10);
            strcpy(outname, target_filename);
            strcat(outname, ".patch");

            output = open(outname, O_WRONLY | O_CREAT | O_TRUNC | O_SYNC,
                S_IRUSR | S_IWUSR);
            if (output < 0) {
                printf("failed to open output file %s: %s\n",
                       outname, strerror(errno));
                return 1;
            }
            sink = FileSink;
            token = &output;
        }

        char* header = patch->data;
        ssize_t header_bytes_read = patch->size;

        SHA_init(&ctx);

        int result;

        if (header_bytes_read >= 8 &&
            memcmp(header, "BSDIFF40", 8) == 0) {
            result = ApplyBSDiffPatch(source_to_use->data, source_to_use->size,
                                      patch, 0, sink, token, &ctx);
        } else if (header_bytes_read >= 8 &&
                   memcmp(header, "IMGDIFF2", 8) == 0) {
            result = ApplyImagePatch(source_to_use->data, source_to_use->size,
                                     patch, sink, token, &ctx, bonus_data);
        } else {
            printf("Unknown patch file format\n");
            return 1;
        }

        if (output >= 0) {
            if (fsync(output) != 0) {
                printf("failed to fsync file \"%s\" (%s)\n", outname, strerror(errno));
                result = 1;
            }
            if (close(output) != 0) {
                printf("failed to close file \"%s\" (%s)\n", outname, strerror(errno));
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
            if (outname != NULL) {
                unlink(outname);
            }
        } else {
            // succeeded; no need to retry
            break;
        }
    } while (retry-- > 0);

    const uint8_t* current_target_sha1 = SHA_final(&ctx);
    if (memcmp(current_target_sha1, target_sha1, SHA_DIGEST_SIZE) != 0) {
        printf("patch did not produce expected sha1\n");
        return 1;
    } else {
        printf("now ");
        print_short_sha1(target_sha1);
        putchar('\n');
    }

    if (output < 0) {
        // Copy the temp file to the partition.
        if (WriteToPartition(msi.buffer, msi.pos, target_filename) != 0) {
            printf("write of patched data to %s failed\n", target_filename);
            return 1;
        }
        free(msi.buffer);
    } else {
        // Give the .patch file the same owner, group, and mode of the
        // original source file.
        if (chmod(outname, source_to_use->st.st_mode) != 0) {
            printf("chmod of \"%s\" failed: %s\n", outname, strerror(errno));
            return 1;
        }
        if (chown(outname, source_to_use->st.st_uid,
                  source_to_use->st.st_gid) != 0) {
            printf("chown of \"%s\" failed: %s\n", outname, strerror(errno));
            return 1;
        }

        // Finally, rename the .patch file to replace the target file.
        if (rename(outname, target_filename) != 0) {
            printf("rename of .patch to \"%s\" failed: %s\n",
                   target_filename, strerror(errno));
            return 1;
        }
    }

    // If this run of applypatch created the copy, and we're here, we
    // can delete it.
    if (made_copy) unlink(CACHE_TEMP_SOURCE);

    // Success!
    return 0;
}
