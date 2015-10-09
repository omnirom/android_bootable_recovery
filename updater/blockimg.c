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

#include <ctype.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <inttypes.h>
#include <libgen.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "applypatch/applypatch.h"
#include "edify/expr.h"
#include "mincrypt/sha.h"
#include "minzip/Hash.h"
#include "updater.h"

#define BLOCKSIZE 4096

// Set this to 0 to interpret 'erase' transfers to mean do a
// BLKDISCARD ioctl (the normal behavior).  Set to 1 to interpret
// erase to mean fill the region with zeroes.
#define DEBUG_ERASE  0

#ifndef BLKDISCARD
#define BLKDISCARD _IO(0x12,119)
#endif

#define STASH_DIRECTORY_BASE "/cache/recovery"
#define STASH_DIRECTORY_MODE 0700
#define STASH_FILE_MODE 0600

char* PrintSha1(const uint8_t* digest);

typedef struct {
    int count;
    int size;
    int pos[0];
} RangeSet;

static RangeSet* parse_range(char* text) {
    char* save;
    int num;
    num = strtol(strtok_r(text, ",", &save), NULL, 0);

    RangeSet* out = malloc(sizeof(RangeSet) + num * sizeof(int));
    if (out == NULL) {
        fprintf(stderr, "failed to allocate range of %zu bytes\n",
                sizeof(RangeSet) + num * sizeof(int));
        exit(1);
    }
    out->count = num / 2;
    out->size = 0;
    int i;
    for (i = 0; i < num; ++i) {
        out->pos[i] = strtol(strtok_r(NULL, ",", &save), NULL, 0);
        if (i%2) {
            out->size += out->pos[i];
        } else {
            out->size -= out->pos[i];
        }
    }

    return out;
}

static int range_overlaps(RangeSet* r1, RangeSet* r2) {
    int i, j, r1_0, r1_1, r2_0, r2_1;

    if (!r1 || !r2) {
        return 0;
    }

    for (i = 0; i < r1->count; ++i) {
        r1_0 = r1->pos[i * 2];
        r1_1 = r1->pos[i * 2 + 1];

        for (j = 0; j < r2->count; ++j) {
            r2_0 = r2->pos[j * 2];
            r2_1 = r2->pos[j * 2 + 1];

            if (!(r2_0 >= r1_1 || r1_0 >= r2_1)) {
                return 1;
            }
        }
    }

    return 0;
}

static int read_all(int fd, uint8_t* data, size_t size) {
    size_t so_far = 0;
    while (so_far < size) {
        ssize_t r = TEMP_FAILURE_RETRY(read(fd, data+so_far, size-so_far));
        if (r == -1) {
            fprintf(stderr, "read failed: %s\n", strerror(errno));
            return -1;
        }
        so_far += r;
    }
    return 0;
}

static int write_all(int fd, const uint8_t* data, size_t size) {
    size_t written = 0;
    while (written < size) {
        ssize_t w = TEMP_FAILURE_RETRY(write(fd, data+written, size-written));
        if (w == -1) {
            fprintf(stderr, "write failed: %s\n", strerror(errno));
            return -1;
        }
        written += w;
    }

    if (fsync(fd) == -1) {
        fprintf(stderr, "fsync failed: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

static bool check_lseek(int fd, off64_t offset, int whence) {
    off64_t rc = TEMP_FAILURE_RETRY(lseek64(fd, offset, whence));
    if (rc == -1) {
        fprintf(stderr, "lseek64 failed: %s\n", strerror(errno));
        return false;
    }
    return true;
}

static void allocate(size_t size, uint8_t** buffer, size_t* buffer_alloc) {
    // if the buffer's big enough, reuse it.
    if (size <= *buffer_alloc) return;

    free(*buffer);

    *buffer = (uint8_t*) malloc(size);
    if (*buffer == NULL) {
        fprintf(stderr, "failed to allocate %zu bytes\n", size);
        exit(1);
    }
    *buffer_alloc = size;
}

typedef struct {
    int fd;
    RangeSet* tgt;
    int p_block;
    size_t p_remain;
} RangeSinkState;

static ssize_t RangeSinkWrite(const uint8_t* data, ssize_t size, void* token) {
    RangeSinkState* rss = (RangeSinkState*) token;

    if (rss->p_remain <= 0) {
        fprintf(stderr, "range sink write overrun");
        return 0;
    }

    ssize_t written = 0;
    while (size > 0) {
        size_t write_now = size;

        if (rss->p_remain < write_now) {
            write_now = rss->p_remain;
        }

        if (write_all(rss->fd, data, write_now) == -1) {
            break;
        }

        data += write_now;
        size -= write_now;

        rss->p_remain -= write_now;
        written += write_now;

        if (rss->p_remain == 0) {
            // move to the next block
            ++rss->p_block;
            if (rss->p_block < rss->tgt->count) {
                rss->p_remain = (rss->tgt->pos[rss->p_block * 2 + 1] -
                                 rss->tgt->pos[rss->p_block * 2]) * BLOCKSIZE;

                if (!check_lseek(rss->fd, (off64_t)rss->tgt->pos[rss->p_block*2] * BLOCKSIZE,
                                 SEEK_SET)) {
                    break;
                }
            } else {
                // we can't write any more; return how many bytes have
                // been written so far.
                break;
            }
        }
    }

    return written;
}

// All of the data for all the 'new' transfers is contained in one
// file in the update package, concatenated together in the order in
// which transfers.list will need it.  We want to stream it out of the
// archive (it's compressed) without writing it to a temp file, but we
// can't write each section until it's that transfer's turn to go.
//
// To achieve this, we expand the new data from the archive in a
// background thread, and block that threads 'receive uncompressed
// data' function until the main thread has reached a point where we
// want some new data to be written.  We signal the background thread
// with the destination for the data and block the main thread,
// waiting for the background thread to complete writing that section.
// Then it signals the main thread to wake up and goes back to
// blocking waiting for a transfer.
//
// NewThreadInfo is the struct used to pass information back and forth
// between the two threads.  When the main thread wants some data
// written, it sets rss to the destination location and signals the
// condition.  When the background thread is done writing, it clears
// rss and signals the condition again.

typedef struct {
    ZipArchive* za;
    const ZipEntry* entry;

    RangeSinkState* rss;

    pthread_mutex_t mu;
    pthread_cond_t cv;
} NewThreadInfo;

static bool receive_new_data(const unsigned char* data, int size, void* cookie) {
    NewThreadInfo* nti = (NewThreadInfo*) cookie;

    while (size > 0) {
        // Wait for nti->rss to be non-NULL, indicating some of this
        // data is wanted.
        pthread_mutex_lock(&nti->mu);
        while (nti->rss == NULL) {
            pthread_cond_wait(&nti->cv, &nti->mu);
        }
        pthread_mutex_unlock(&nti->mu);

        // At this point nti->rss is set, and we own it.  The main
        // thread is waiting for it to disappear from nti.
        ssize_t written = RangeSinkWrite(data, size, nti->rss);
        data += written;
        size -= written;

        if (nti->rss->p_block == nti->rss->tgt->count) {
            // we have written all the bytes desired by this rss.

            pthread_mutex_lock(&nti->mu);
            nti->rss = NULL;
            pthread_cond_broadcast(&nti->cv);
            pthread_mutex_unlock(&nti->mu);
        }
    }

    return true;
}

static void* unzip_new_data(void* cookie) {
    NewThreadInfo* nti = (NewThreadInfo*) cookie;
    mzProcessZipEntryContents(nti->za, nti->entry, receive_new_data, nti);
    return NULL;
}

static int ReadBlocks(RangeSet* src, uint8_t* buffer, int fd) {
    int i;
    size_t p = 0;
    size_t size;

    if (!src || !buffer) {
        return -1;
    }

    for (i = 0; i < src->count; ++i) {
        if (!check_lseek(fd, (off64_t) src->pos[i * 2] * BLOCKSIZE, SEEK_SET)) {
            return -1;
        }

        size = (src->pos[i * 2 + 1] - src->pos[i * 2]) * BLOCKSIZE;

        if (read_all(fd, buffer + p, size) == -1) {
            return -1;
        }

        p += size;
    }

    return 0;
}

static int WriteBlocks(RangeSet* tgt, uint8_t* buffer, int fd) {
    int i;
    size_t p = 0;
    size_t size;

    if (!tgt || !buffer) {
        return -1;
    }

    for (i = 0; i < tgt->count; ++i) {
        if (!check_lseek(fd, (off64_t) tgt->pos[i * 2] * BLOCKSIZE, SEEK_SET)) {
            return -1;
        }

        size = (tgt->pos[i * 2 + 1] - tgt->pos[i * 2]) * BLOCKSIZE;

        if (write_all(fd, buffer + p, size) == -1) {
            return -1;
        }

        p += size;
    }

    return 0;
}

// Do a source/target load for move/bsdiff/imgdiff in version 1.
// 'wordsave' is the save_ptr of a strtok_r()-in-progress.  We expect
// to parse the remainder of the string as:
//
//    <src_range> <tgt_range>
//
// The source range is loaded into the provided buffer, reallocating
// it to make it larger if necessary.  The target ranges are returned
// in *tgt, if tgt is non-NULL.

static int LoadSrcTgtVersion1(char** wordsave, RangeSet** tgt, int* src_blocks,
                               uint8_t** buffer, size_t* buffer_alloc, int fd) {
    char* word;
    int rc;

    word = strtok_r(NULL, " ", wordsave);
    RangeSet* src = parse_range(word);

    if (tgt != NULL) {
        word = strtok_r(NULL, " ", wordsave);
        *tgt = parse_range(word);
    }

    allocate(src->size * BLOCKSIZE, buffer, buffer_alloc);
    rc = ReadBlocks(src, *buffer, fd);
    *src_blocks = src->size;

    free(src);
    return rc;
}

static int VerifyBlocks(const char *expected, const uint8_t *buffer,
                        size_t blocks, int printerror) {
    char* hexdigest = NULL;
    int rc = -1;
    uint8_t digest[SHA_DIGEST_SIZE];

    if (!expected || !buffer) {
        return rc;
    }

    SHA_hash(buffer, blocks * BLOCKSIZE, digest);
    hexdigest = PrintSha1(digest);

    if (hexdigest != NULL) {
        rc = strcmp(expected, hexdigest);

        if (rc != 0 && printerror) {
            fprintf(stderr, "failed to verify blocks (expected %s, read %s)\n",
                expected, hexdigest);
        }

        free(hexdigest);
    }

    return rc;
}

static char* GetStashFileName(const char* base, const char* id, const char* postfix) {
    char* fn;
    int len;
    int res;

    if (base == NULL) {
        return NULL;
    }

    if (id == NULL) {
        id = "";
    }

    if (postfix == NULL) {
        postfix = "";
    }

    len = strlen(STASH_DIRECTORY_BASE) + 1 + strlen(base) + 1 + strlen(id) + strlen(postfix) + 1;
    fn = malloc(len);

    if (fn == NULL) {
        fprintf(stderr, "failed to malloc %d bytes for fn\n", len);
        return NULL;
    }

    res = snprintf(fn, len, STASH_DIRECTORY_BASE "/%s/%s%s", base, id, postfix);

    if (res < 0 || res >= len) {
        fprintf(stderr, "failed to format file name (return value %d)\n", res);
        free(fn);
        return NULL;
    }

    return fn;
}

typedef void (*StashCallback)(const char*, void*);

// Does a best effort enumeration of stash files. Ignores possible non-file
// items in the stash directory and continues despite of errors. Calls the
// 'callback' function for each file and passes 'data' to the function as a
// parameter.

static void EnumerateStash(const char* dirname, StashCallback callback, void* data) {
    char* fn;
    DIR* directory;
    int len;
    int res;
    struct dirent* item;

    if (dirname == NULL || callback == NULL) {
        return;
    }

    directory = opendir(dirname);

    if (directory == NULL) {
        if (errno != ENOENT) {
            fprintf(stderr, "opendir \"%s\" failed: %s\n", dirname, strerror(errno));
        }
        return;
    }

    while ((item = readdir(directory)) != NULL) {
        if (item->d_type != DT_REG) {
            continue;
        }

        len = strlen(dirname) + 1 + strlen(item->d_name) + 1;
        fn = malloc(len);

        if (fn == NULL) {
            fprintf(stderr, "failed to malloc %d bytes for fn\n", len);
            continue;
        }

        res = snprintf(fn, len, "%s/%s", dirname, item->d_name);

        if (res < 0 || res >= len) {
            fprintf(stderr, "failed to format file name (return value %d)\n", res);
            free(fn);
            continue;
        }

        callback(fn, data);
        free(fn);
    }

    if (closedir(directory) == -1) {
        fprintf(stderr, "closedir \"%s\" failed: %s\n", dirname, strerror(errno));
    }
}

static void UpdateFileSize(const char* fn, void* data) {
    int* size = (int*) data;
    struct stat st;

    if (!fn || !data) {
        return;
    }

    if (stat(fn, &st) == -1) {
        fprintf(stderr, "stat \"%s\" failed: %s\n", fn, strerror(errno));
        return;
    }

    *size += st.st_size;
}

// Deletes the stash directory and all files in it. Assumes that it only
// contains files. There is nothing we can do about unlikely, but possible
// errors, so they are merely logged.

static void DeleteFile(const char* fn, void* data) {
    if (fn) {
        fprintf(stderr, "deleting %s\n", fn);

        if (unlink(fn) == -1 && errno != ENOENT) {
            fprintf(stderr, "unlink \"%s\" failed: %s\n", fn, strerror(errno));
        }
    }
}

static void DeletePartial(const char* fn, void* data) {
    if (fn && strstr(fn, ".partial") != NULL) {
        DeleteFile(fn, data);
    }
}

static void DeleteStash(const char* base) {
    char* dirname;

    if (base == NULL) {
        return;
    }

    dirname = GetStashFileName(base, NULL, NULL);

    if (dirname == NULL) {
        return;
    }

    fprintf(stderr, "deleting stash %s\n", base);
    EnumerateStash(dirname, DeleteFile, NULL);

    if (rmdir(dirname) == -1) {
        if (errno != ENOENT && errno != ENOTDIR) {
            fprintf(stderr, "rmdir \"%s\" failed: %s\n", dirname, strerror(errno));
        }
    }

    free(dirname);
}

static int LoadStash(const char* base, const char* id, int verify, int* blocks, uint8_t** buffer,
        size_t* buffer_alloc, int printnoent) {
    char *fn = NULL;
    int blockcount = 0;
    int fd = -1;
    int rc = -1;
    int res;
    struct stat st;

    if (!base || !id || !buffer || !buffer_alloc) {
        goto lsout;
    }

    if (!blocks) {
        blocks = &blockcount;
    }

    fn = GetStashFileName(base, id, NULL);

    if (fn == NULL) {
        goto lsout;
    }

    res = stat(fn, &st);

    if (res == -1) {
        if (errno != ENOENT || printnoent) {
            fprintf(stderr, "stat \"%s\" failed: %s\n", fn, strerror(errno));
        }
        goto lsout;
    }

    fprintf(stderr, " loading %s\n", fn);

    if ((st.st_size % BLOCKSIZE) != 0) {
        fprintf(stderr, "%s size %zd not multiple of block size %d", fn, st.st_size, BLOCKSIZE);
        goto lsout;
    }

    fd = TEMP_FAILURE_RETRY(open(fn, O_RDONLY));

    if (fd == -1) {
        fprintf(stderr, "open \"%s\" failed: %s\n", fn, strerror(errno));
        goto lsout;
    }

    allocate(st.st_size, buffer, buffer_alloc);

    if (read_all(fd, *buffer, st.st_size) == -1) {
        goto lsout;
    }

    *blocks = st.st_size / BLOCKSIZE;

    if (verify && VerifyBlocks(id, *buffer, *blocks, 1) != 0) {
        fprintf(stderr, "unexpected contents in %s\n", fn);
        DeleteFile(fn, NULL);
        goto lsout;
    }

    rc = 0;

lsout:
    if (fd != -1) {
        close(fd);
    }

    if (fn) {
        free(fn);
    }

    return rc;
}

static int WriteStash(const char* base, const char* id, int blocks, uint8_t* buffer,
        int checkspace, int *exists) {
    char *fn = NULL;
    char *cn = NULL;
    int fd = -1;
    int rc = -1;
    int dfd = -1;
    int res;
    struct stat st;

    if (base == NULL || buffer == NULL) {
        goto wsout;
    }

    if (checkspace && CacheSizeCheck(blocks * BLOCKSIZE) != 0) {
        fprintf(stderr, "not enough space to write stash\n");
        goto wsout;
    }

    fn = GetStashFileName(base, id, ".partial");
    cn = GetStashFileName(base, id, NULL);

    if (fn == NULL || cn == NULL) {
        goto wsout;
    }

    if (exists) {
        res = stat(cn, &st);

        if (res == 0) {
            // The file already exists and since the name is the hash of the contents,
            // it's safe to assume the contents are identical (accidental hash collisions
            // are unlikely)
            fprintf(stderr, " skipping %d existing blocks in %s\n", blocks, cn);
            *exists = 1;
            rc = 0;
            goto wsout;
        }

        *exists = 0;
    }

    fprintf(stderr, " writing %d blocks to %s\n", blocks, cn);

    fd = TEMP_FAILURE_RETRY(open(fn, O_WRONLY | O_CREAT | O_TRUNC | O_SYNC, STASH_FILE_MODE));

    if (fd == -1) {
        fprintf(stderr, "failed to create \"%s\": %s\n", fn, strerror(errno));
        goto wsout;
    }

    if (write_all(fd, buffer, blocks * BLOCKSIZE) == -1) {
        goto wsout;
    }

    if (fsync(fd) == -1) {
        fprintf(stderr, "fsync \"%s\" failed: %s\n", fn, strerror(errno));
        goto wsout;
    }

    if (rename(fn, cn) == -1) {
        fprintf(stderr, "rename(\"%s\", \"%s\") failed: %s\n", fn, cn, strerror(errno));
        goto wsout;
    }

    const char* dname;
    dname = dirname(cn);
    dfd = TEMP_FAILURE_RETRY(open(dname, O_RDONLY | O_DIRECTORY));

    if (dfd == -1) {
        fprintf(stderr, "failed to open \"%s\" failed: %s\n", dname, strerror(errno));
        goto wsout;
    }

    if (fsync(dfd) == -1) {
        fprintf(stderr, "fsync \"%s\" failed: %s\n", dname, strerror(errno));
        goto wsout;
    }

    rc = 0;

wsout:
    if (fd != -1) {
        close(fd);
    }

    if (dfd != -1) {
        close(dfd);
    }

    if (fn) {
        free(fn);
    }

    if (cn) {
        free(cn);
    }

    return rc;
}

// Creates a directory for storing stash files and checks if the /cache partition
// hash enough space for the expected amount of blocks we need to store. Returns
// >0 if we created the directory, zero if it existed already, and <0 of failure.

static int CreateStash(State* state, int maxblocks, const char* blockdev, char** base) {
    char* dirname = NULL;
    const uint8_t* digest;
    int rc = -1;
    int res;
    int size = 0;
    SHA_CTX ctx;
    struct stat st;

    if (blockdev == NULL || base == NULL) {
        goto csout;
    }

    // Stash directory should be different for each partition to avoid conflicts
    // when updating multiple partitions at the same time, so we use the hash of
    // the block device name as the base directory
    SHA_init(&ctx);
    SHA_update(&ctx, blockdev, strlen(blockdev));
    digest = SHA_final(&ctx);
    *base = PrintSha1(digest);

    if (*base == NULL) {
        goto csout;
    }

    dirname = GetStashFileName(*base, NULL, NULL);

    if (dirname == NULL) {
        goto csout;
    }

    res = stat(dirname, &st);

    if (res == -1 && errno != ENOENT) {
        ErrorAbort(state, "stat \"%s\" failed: %s\n", dirname, strerror(errno));
        goto csout;
    } else if (res != 0) {
        fprintf(stderr, "creating stash %s\n", dirname);
        res = mkdir(dirname, STASH_DIRECTORY_MODE);

        if (res != 0) {
            ErrorAbort(state, "mkdir \"%s\" failed: %s\n", dirname, strerror(errno));
            goto csout;
        }

        if (CacheSizeCheck(maxblocks * BLOCKSIZE) != 0) {
            ErrorAbort(state, "not enough space for stash\n");
            goto csout;
        }

        rc = 1; // Created directory
        goto csout;
    }

    fprintf(stderr, "using existing stash %s\n", dirname);

    // If the directory already exists, calculate the space already allocated to
    // stash files and check if there's enough for all required blocks. Delete any
    // partially completed stash files first.

    EnumerateStash(dirname, DeletePartial, NULL);
    EnumerateStash(dirname, UpdateFileSize, &size);

    size = (maxblocks * BLOCKSIZE) - size;

    if (size > 0 && CacheSizeCheck(size) != 0) {
        ErrorAbort(state, "not enough space for stash (%d more needed)\n", size);
        goto csout;
    }

    rc = 0; // Using existing directory

csout:
    if (dirname) {
        free(dirname);
    }

    return rc;
}

static int SaveStash(const char* base, char** wordsave, uint8_t** buffer, size_t* buffer_alloc,
                      int fd, int usehash, int* isunresumable) {
    char *id = NULL;
    int res = -1;
    int blocks = 0;

    if (!wordsave || !buffer || !buffer_alloc || !isunresumable) {
        return -1;
    }

    id = strtok_r(NULL, " ", wordsave);

    if (id == NULL) {
        fprintf(stderr, "missing id field in stash command\n");
        return -1;
    }

    if (usehash && LoadStash(base, id, 1, &blocks, buffer, buffer_alloc, 0) == 0) {
        // Stash file already exists and has expected contents. Do not
        // read from source again, as the source may have been already
        // overwritten during a previous attempt.
        return 0;
    }

    if (LoadSrcTgtVersion1(wordsave, NULL, &blocks, buffer, buffer_alloc, fd) == -1) {
        return -1;
    }

    if (usehash && VerifyBlocks(id, *buffer, blocks, 1) != 0) {
        // Source blocks have unexpected contents. If we actually need this
        // data later, this is an unrecoverable error. However, the command
        // that uses the data may have already completed previously, so the
        // possible failure will occur during source block verification.
        fprintf(stderr, "failed to load source blocks for stash %s\n", id);
        return 0;
    }

    fprintf(stderr, "stashing %d blocks to %s\n", blocks, id);
    return WriteStash(base, id, blocks, *buffer, 0, NULL);
}

static int FreeStash(const char* base, const char* id) {
    char *fn = NULL;

    if (base == NULL || id == NULL) {
        return -1;
    }

    fn = GetStashFileName(base, id, NULL);

    if (fn == NULL) {
        return -1;
    }

    DeleteFile(fn, NULL);
    free(fn);

    return 0;
}

static void MoveRange(uint8_t* dest, RangeSet* locs, const uint8_t* source) {
    // source contains packed data, which we want to move to the
    // locations given in *locs in the dest buffer.  source and dest
    // may be the same buffer.

    int start = locs->size;
    int i;
    for (i = locs->count-1; i >= 0; --i) {
        int blocks = locs->pos[i*2+1] - locs->pos[i*2];
        start -= blocks;
        memmove(dest + (locs->pos[i*2] * BLOCKSIZE), source + (start * BLOCKSIZE),
                blocks * BLOCKSIZE);
    }
}

// Do a source/target load for move/bsdiff/imgdiff in version 2.
// 'wordsave' is the save_ptr of a strtok_r()-in-progress.  We expect
// to parse the remainder of the string as one of:
//
//    <tgt_range> <src_block_count> <src_range>
//        (loads data from source image only)
//
//    <tgt_range> <src_block_count> - <[stash_id:stash_range] ...>
//        (loads data from stashes only)
//
//    <tgt_range> <src_block_count> <src_range> <src_loc> <[stash_id:stash_range] ...>
//        (loads data from both source image and stashes)
//
// On return, buffer is filled with the loaded source data (rearranged
// and combined with stashed data as necessary).  buffer may be
// reallocated if needed to accommodate the source data.  *tgt is the
// target RangeSet.  Any stashes required are loaded using LoadStash.

static int LoadSrcTgtVersion2(char** wordsave, RangeSet** tgt, int* src_blocks,
                               uint8_t** buffer, size_t* buffer_alloc, int fd,
                               const char* stashbase, int* overlap) {
    char* word;
    char* colonsave;
    char* colon;
    int id;
    int res;
    RangeSet* locs;
    size_t stashalloc = 0;
    uint8_t* stash = NULL;

    if (tgt != NULL) {
        word = strtok_r(NULL, " ", wordsave);
        *tgt = parse_range(word);
    }

    word = strtok_r(NULL, " ", wordsave);
    *src_blocks = strtol(word, NULL, 0);

    allocate(*src_blocks * BLOCKSIZE, buffer, buffer_alloc);

    word = strtok_r(NULL, " ", wordsave);
    if (word[0] == '-' && word[1] == '\0') {
        // no source ranges, only stashes
    } else {
        RangeSet* src = parse_range(word);
        res = ReadBlocks(src, *buffer, fd);

        if (overlap && tgt) {
            *overlap = range_overlaps(src, *tgt);
        }

        free(src);

        if (res == -1) {
            return -1;
        }

        word = strtok_r(NULL, " ", wordsave);
        if (word == NULL) {
            // no stashes, only source range
            return 0;
        }

        locs = parse_range(word);
        MoveRange(*buffer, locs, *buffer);
        free(locs);
    }

    while ((word = strtok_r(NULL, " ", wordsave)) != NULL) {
        // Each word is a an index into the stash table, a colon, and
        // then a rangeset describing where in the source block that
        // stashed data should go.
        colonsave = NULL;
        colon = strtok_r(word, ":", &colonsave);

        res = LoadStash(stashbase, colon, 0, NULL, &stash, &stashalloc, 1);

        if (res == -1) {
            // These source blocks will fail verification if used later, but we
            // will let the caller decide if this is a fatal failure
            fprintf(stderr, "failed to load stash %s\n", colon);
            continue;
        }

        colon = strtok_r(NULL, ":", &colonsave);
        locs = parse_range(colon);

        MoveRange(*buffer, locs, stash);
        free(locs);
    }

    if (stash) {
        free(stash);
    }

    return 0;
}

// Parameters for transfer list command functions
typedef struct {
    char* cmdname;
    char* cpos;
    char* freestash;
    char* stashbase;
    int canwrite;
    int createdstash;
    int fd;
    int foundwrites;
    int isunresumable;
    int version;
    int written;
    NewThreadInfo nti;
    pthread_t thread;
    size_t bufsize;
    uint8_t* buffer;
    uint8_t* patch_start;
} CommandParameters;

// Do a source/target load for move/bsdiff/imgdiff in version 3.
//
// Parameters are the same as for LoadSrcTgtVersion2, except for 'onehash', which
// tells the function whether to expect separate source and targe block hashes, or
// if they are both the same and only one hash should be expected, and
// 'isunresumable', which receives a non-zero value if block verification fails in
// a way that the update cannot be resumed anymore.
//
// If the function is unable to load the necessary blocks or their contents don't
// match the hashes, the return value is -1 and the command should be aborted.
//
// If the return value is 1, the command has already been completed according to
// the contents of the target blocks, and should not be performed again.
//
// If the return value is 0, source blocks have expected content and the command
// can be performed.

static int LoadSrcTgtVersion3(CommandParameters* params, RangeSet** tgt, int* src_blocks,
                              int onehash, int* overlap) {
    char* srchash = NULL;
    char* tgthash = NULL;
    int stash_exists = 0;
    int overlap_blocks = 0;
    int rc = -1;
    uint8_t* tgtbuffer = NULL;

    if (!params|| !tgt || !src_blocks || !overlap) {
        goto v3out;
    }

    srchash = strtok_r(NULL, " ", &params->cpos);

    if (srchash == NULL) {
        fprintf(stderr, "missing source hash\n");
        goto v3out;
    }

    if (onehash) {
        tgthash = srchash;
    } else {
        tgthash = strtok_r(NULL, " ", &params->cpos);

        if (tgthash == NULL) {
            fprintf(stderr, "missing target hash\n");
            goto v3out;
        }
    }

    if (LoadSrcTgtVersion2(&params->cpos, tgt, src_blocks, &params->buffer, &params->bufsize,
            params->fd, params->stashbase, overlap) == -1) {
        goto v3out;
    }

    tgtbuffer = (uint8_t*) malloc((*tgt)->size * BLOCKSIZE);

    if (tgtbuffer == NULL) {
        fprintf(stderr, "failed to allocate %d bytes\n", (*tgt)->size * BLOCKSIZE);
        goto v3out;
    }

    if (ReadBlocks(*tgt, tgtbuffer, params->fd) == -1) {
        goto v3out;
    }

    if (VerifyBlocks(tgthash, tgtbuffer, (*tgt)->size, 0) == 0) {
        // Target blocks already have expected content, command should be skipped
        rc = 1;
        goto v3out;
    }

    if (VerifyBlocks(srchash, params->buffer, *src_blocks, 1) == 0) {
        // If source and target blocks overlap, stash the source blocks so we can
        // resume from possible write errors
        if (*overlap) {
            fprintf(stderr, "stashing %d overlapping blocks to %s\n", *src_blocks,
                srchash);

            if (WriteStash(params->stashbase, srchash, *src_blocks, params->buffer, 1,
                    &stash_exists) != 0) {
                fprintf(stderr, "failed to stash overlapping source blocks\n");
                goto v3out;
            }

            // Can be deleted when the write has completed
            if (!stash_exists) {
                params->freestash = srchash;
            }
        }

        // Source blocks have expected content, command can proceed
        rc = 0;
        goto v3out;
    }

    if (*overlap && LoadStash(params->stashbase, srchash, 1, NULL, &params->buffer,
                        &params->bufsize, 1) == 0) {
        // Overlapping source blocks were previously stashed, command can proceed.
        // We are recovering from an interrupted command, so we don't know if the
        // stash can safely be deleted after this command.
        rc = 0;
        goto v3out;
    }

    // Valid source data not available, update cannot be resumed
    fprintf(stderr, "partition has unexpected contents\n");
    params->isunresumable = 1;

v3out:
    if (tgtbuffer) {
        free(tgtbuffer);
    }

    return rc;
}

static int PerformCommandMove(CommandParameters* params) {
    int blocks = 0;
    int overlap = 0;
    int rc = -1;
    int status = 0;
    RangeSet* tgt = NULL;

    if (!params) {
        goto pcmout;
    }

    if (params->version == 1) {
        status = LoadSrcTgtVersion1(&params->cpos, &tgt, &blocks, &params->buffer,
                    &params->bufsize, params->fd);
    } else if (params->version == 2) {
        status = LoadSrcTgtVersion2(&params->cpos, &tgt, &blocks, &params->buffer,
                    &params->bufsize, params->fd, params->stashbase, NULL);
    } else if (params->version >= 3) {
        status = LoadSrcTgtVersion3(params, &tgt, &blocks, 1, &overlap);
    }

    if (status == -1) {
        fprintf(stderr, "failed to read blocks for move\n");
        goto pcmout;
    }

    if (status == 0) {
        params->foundwrites = 1;
    } else if (params->foundwrites) {
        fprintf(stderr, "warning: commands executed out of order [%s]\n", params->cmdname);
    }

    if (params->canwrite) {
        if (status == 0) {
            fprintf(stderr, "  moving %d blocks\n", blocks);

            if (WriteBlocks(tgt, params->buffer, params->fd) == -1) {
                goto pcmout;
            }
        } else {
            fprintf(stderr, "skipping %d already moved blocks\n", blocks);
        }

    }

    if (params->freestash) {
        FreeStash(params->stashbase, params->freestash);
        params->freestash = NULL;
    }

    params->written += tgt->size;
    rc = 0;

pcmout:
    if (tgt) {
        free(tgt);
    }

    return rc;
}

static int PerformCommandStash(CommandParameters* params) {
    if (!params) {
        return -1;
    }

    return SaveStash(params->stashbase, &params->cpos, &params->buffer, &params->bufsize,
                params->fd, (params->version >= 3), &params->isunresumable);
}

static int PerformCommandFree(CommandParameters* params) {
    if (!params) {
        return -1;
    }

    if (params->createdstash || params->canwrite) {
        return FreeStash(params->stashbase, params->cpos);
    }

    return 0;
}

static int PerformCommandZero(CommandParameters* params) {
    char* range = NULL;
    int i;
    int j;
    int rc = -1;
    RangeSet* tgt = NULL;

    if (!params) {
        goto pczout;
    }

    range = strtok_r(NULL, " ", &params->cpos);

    if (range == NULL) {
        fprintf(stderr, "missing target blocks for zero\n");
        goto pczout;
    }

    tgt = parse_range(range);

    fprintf(stderr, "  zeroing %d blocks\n", tgt->size);

    allocate(BLOCKSIZE, &params->buffer, &params->bufsize);
    memset(params->buffer, 0, BLOCKSIZE);

    if (params->canwrite) {
        for (i = 0; i < tgt->count; ++i) {
            if (!check_lseek(params->fd, (off64_t) tgt->pos[i * 2] * BLOCKSIZE, SEEK_SET)) {
                goto pczout;
            }

            for (j = tgt->pos[i * 2]; j < tgt->pos[i * 2 + 1]; ++j) {
                if (write_all(params->fd, params->buffer, BLOCKSIZE) == -1) {
                    goto pczout;
                }
            }
        }
    }

    if (params->cmdname[0] == 'z') {
        // Update only for the zero command, as the erase command will call
        // this if DEBUG_ERASE is defined.
        params->written += tgt->size;
    }

    rc = 0;

pczout:
    if (tgt) {
        free(tgt);
    }

    return rc;
}

static int PerformCommandNew(CommandParameters* params) {
    char* range = NULL;
    int rc = -1;
    RangeSet* tgt = NULL;
    RangeSinkState rss;

    if (!params) {
        goto pcnout;
    }

    range = strtok_r(NULL, " ", &params->cpos);

    if (range == NULL) {
        goto pcnout;
    }

    tgt = parse_range(range);

    if (params->canwrite) {
        fprintf(stderr, " writing %d blocks of new data\n", tgt->size);

        rss.fd = params->fd;
        rss.tgt = tgt;
        rss.p_block = 0;
        rss.p_remain = (tgt->pos[1] - tgt->pos[0]) * BLOCKSIZE;

        if (!check_lseek(params->fd, (off64_t) tgt->pos[0] * BLOCKSIZE, SEEK_SET)) {
            goto pcnout;
        }

        pthread_mutex_lock(&params->nti.mu);
        params->nti.rss = &rss;
        pthread_cond_broadcast(&params->nti.cv);

        while (params->nti.rss) {
            pthread_cond_wait(&params->nti.cv, &params->nti.mu);
        }

        pthread_mutex_unlock(&params->nti.mu);
    }

    params->written += tgt->size;
    rc = 0;

pcnout:
    if (tgt) {
        free(tgt);
    }

    return rc;
}

static int PerformCommandDiff(CommandParameters* params) {
    char* logparams = NULL;
    char* value = NULL;
    int blocks = 0;
    int overlap = 0;
    int rc = -1;
    int status = 0;
    RangeSet* tgt = NULL;
    RangeSinkState rss;
    size_t len = 0;
    size_t offset = 0;
    Value patch_value;

    if (!params) {
        goto pcdout;
    }

    logparams = strdup(params->cpos);
    value = strtok_r(NULL, " ", &params->cpos);

    if (value == NULL) {
        fprintf(stderr, "missing patch offset for %s\n", params->cmdname);
        goto pcdout;
    }

    offset = strtoul(value, NULL, 0);

    value = strtok_r(NULL, " ", &params->cpos);

    if (value == NULL) {
        fprintf(stderr, "missing patch length for %s\n", params->cmdname);
        goto pcdout;
    }

    len = strtoul(value, NULL, 0);

    if (params->version == 1) {
        status = LoadSrcTgtVersion1(&params->cpos, &tgt, &blocks, &params->buffer,
                    &params->bufsize, params->fd);
    } else if (params->version == 2) {
        status = LoadSrcTgtVersion2(&params->cpos, &tgt, &blocks, &params->buffer,
                    &params->bufsize, params->fd, params->stashbase, NULL);
    } else if (params->version >= 3) {
        status = LoadSrcTgtVersion3(params, &tgt, &blocks, 0, &overlap);
    }

    if (status == -1) {
        fprintf(stderr, "failed to read blocks for diff\n");
        goto pcdout;
    }

    if (status == 0) {
        params->foundwrites = 1;
    } else if (params->foundwrites) {
        fprintf(stderr, "warning: commands executed out of order [%s]\n", params->cmdname);
    }

    if (params->canwrite) {
        if (status == 0) {
            fprintf(stderr, "patching %d blocks to %d\n", blocks, tgt->size);

            patch_value.type = VAL_BLOB;
            patch_value.size = len;
            patch_value.data = (char*) (params->patch_start + offset);

            rss.fd = params->fd;
            rss.tgt = tgt;
            rss.p_block = 0;
            rss.p_remain = (tgt->pos[1] - tgt->pos[0]) * BLOCKSIZE;

            if (!check_lseek(params->fd, (off64_t) tgt->pos[0] * BLOCKSIZE, SEEK_SET)) {
                goto pcdout;
            }

            if (params->cmdname[0] == 'i') {      // imgdiff
                ApplyImagePatch(params->buffer, blocks * BLOCKSIZE, &patch_value,
                    &RangeSinkWrite, &rss, NULL, NULL);
            } else {
                ApplyBSDiffPatch(params->buffer, blocks * BLOCKSIZE, &patch_value,
                    0, &RangeSinkWrite, &rss, NULL);
            }

            // We expect the output of the patcher to fill the tgt ranges exactly.
            if (rss.p_block != tgt->count || rss.p_remain != 0) {
                fprintf(stderr, "range sink underrun?\n");
            }
        } else {
            fprintf(stderr, "skipping %d blocks already patched to %d [%s]\n",
                blocks, tgt->size, logparams);
        }
    }

    if (params->freestash) {
        FreeStash(params->stashbase, params->freestash);
        params->freestash = NULL;
    }

    params->written += tgt->size;
    rc = 0;

pcdout:
    if (logparams) {
        free(logparams);
    }

    if (tgt) {
        free(tgt);
    }

    return rc;
}

static int PerformCommandErase(CommandParameters* params) {
    char* range = NULL;
    int i;
    int rc = -1;
    RangeSet* tgt = NULL;
    struct stat st;
    uint64_t blocks[2];

    if (DEBUG_ERASE) {
        return PerformCommandZero(params);
    }

    if (!params) {
        goto pceout;
    }

    if (fstat(params->fd, &st) == -1) {
        fprintf(stderr, "failed to fstat device to erase: %s\n", strerror(errno));
        goto pceout;
    }

    if (!S_ISBLK(st.st_mode)) {
        fprintf(stderr, "not a block device; skipping erase\n");
        goto pceout;
    }

    range = strtok_r(NULL, " ", &params->cpos);

    if (range == NULL) {
        fprintf(stderr, "missing target blocks for zero\n");
        goto pceout;
    }

    tgt = parse_range(range);

    if (params->canwrite) {
        fprintf(stderr, " erasing %d blocks\n", tgt->size);

        for (i = 0; i < tgt->count; ++i) {
            // offset in bytes
            blocks[0] = tgt->pos[i * 2] * (uint64_t) BLOCKSIZE;
            // length in bytes
            blocks[1] = (tgt->pos[i * 2 + 1] - tgt->pos[i * 2]) * (uint64_t) BLOCKSIZE;

            if (ioctl(params->fd, BLKDISCARD, &blocks) == -1) {
                fprintf(stderr, "BLKDISCARD ioctl failed: %s\n", strerror(errno));
                goto pceout;
            }
        }
    }

    rc = 0;

pceout:
    if (tgt) {
        free(tgt);
    }

    return rc;
}

// Definitions for transfer list command functions
typedef int (*CommandFunction)(CommandParameters*);

typedef struct {
    const char* name;
    CommandFunction f;
} Command;

// CompareCommands and CompareCommandNames are for the hash table

static int CompareCommands(const void* c1, const void* c2) {
    return strcmp(((const Command*) c1)->name, ((const Command*) c2)->name);
}

static int CompareCommandNames(const void* c1, const void* c2) {
    return strcmp(((const Command*) c1)->name, (const char*) c2);
}

// HashString is used to hash command names for the hash table

static unsigned int HashString(const char *s) {
    unsigned int hash = 0;
    if (s) {
        while (*s) {
            hash = hash * 33 + *s++;
        }
    }
    return hash;
}

// args:
//    - block device (or file) to modify in-place
//    - transfer list (blob)
//    - new data stream (filename within package.zip)
//    - patch stream (filename within package.zip, must be uncompressed)

static Value* PerformBlockImageUpdate(const char* name, State* state, int argc, Expr* argv[],
            const Command* commands, int cmdcount, int dryrun) {

    char* line = NULL;
    char* linesave = NULL;
    char* logcmd = NULL;
    char* transfer_list = NULL;
    CommandParameters params;
    const Command* cmd = NULL;
    const ZipEntry* new_entry = NULL;
    const ZipEntry* patch_entry = NULL;
    FILE* cmd_pipe = NULL;
    HashTable* cmdht = NULL;
    int i;
    int res;
    int rc = -1;
    int stash_max_blocks = 0;
    int total_blocks = 0;
    pthread_attr_t attr;
    unsigned int cmdhash;
    UpdaterInfo* ui = NULL;
    Value* blockdev_filename = NULL;
    Value* new_data_fn = NULL;
    Value* patch_data_fn = NULL;
    Value* transfer_list_value = NULL;
    ZipArchive* za = NULL;

    memset(&params, 0, sizeof(params));
    params.canwrite = !dryrun;

    fprintf(stderr, "performing %s\n", dryrun ? "verification" : "update");

    if (ReadValueArgs(state, argv, 4, &blockdev_filename, &transfer_list_value,
            &new_data_fn, &patch_data_fn) < 0) {
        goto pbiudone;
    }

    if (blockdev_filename->type != VAL_STRING) {
        ErrorAbort(state, "blockdev_filename argument to %s must be string", name);
        goto pbiudone;
    }
    if (transfer_list_value->type != VAL_BLOB) {
        ErrorAbort(state, "transfer_list argument to %s must be blob", name);
        goto pbiudone;
    }
    if (new_data_fn->type != VAL_STRING) {
        ErrorAbort(state, "new_data_fn argument to %s must be string", name);
        goto pbiudone;
    }
    if (patch_data_fn->type != VAL_STRING) {
        ErrorAbort(state, "patch_data_fn argument to %s must be string", name);
        goto pbiudone;
    }

    ui = (UpdaterInfo*) state->cookie;

    if (ui == NULL) {
        goto pbiudone;
    }

    cmd_pipe = ui->cmd_pipe;
    za = ui->package_zip;

    if (cmd_pipe == NULL || za == NULL) {
        goto pbiudone;
    }

    patch_entry = mzFindZipEntry(za, patch_data_fn->data);

    if (patch_entry == NULL) {
        fprintf(stderr, "%s(): no file \"%s\" in package", name, patch_data_fn->data);
        goto pbiudone;
    }

    params.patch_start = ui->package_zip_addr + mzGetZipEntryOffset(patch_entry);
    new_entry = mzFindZipEntry(za, new_data_fn->data);

    if (new_entry == NULL) {
        fprintf(stderr, "%s(): no file \"%s\" in package", name, new_data_fn->data);
        goto pbiudone;
    }

    params.fd = TEMP_FAILURE_RETRY(open(blockdev_filename->data, O_RDWR));

    if (params.fd == -1) {
        fprintf(stderr, "open \"%s\" failed: %s\n", blockdev_filename->data, strerror(errno));
        goto pbiudone;
    }

    if (params.canwrite) {
        params.nti.za = za;
        params.nti.entry = new_entry;

        pthread_mutex_init(&params.nti.mu, NULL);
        pthread_cond_init(&params.nti.cv, NULL);
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

        int error = pthread_create(&params.thread, &attr, unzip_new_data, &params.nti);
        if (error != 0) {
            fprintf(stderr, "pthread_create failed: %s\n", strerror(error));
            goto pbiudone;
        }
    }

    // The data in transfer_list_value is not necessarily null-terminated, so we need
    // to copy it to a new buffer and add the null that strtok_r will need.
    transfer_list = malloc(transfer_list_value->size + 1);

    if (transfer_list == NULL) {
        fprintf(stderr, "failed to allocate %zd bytes for transfer list\n",
            transfer_list_value->size + 1);
        goto pbiudone;
    }

    memcpy(transfer_list, transfer_list_value->data, transfer_list_value->size);
    transfer_list[transfer_list_value->size] = '\0';

    // First line in transfer list is the version number
    line = strtok_r(transfer_list, "\n", &linesave);
    params.version = strtol(line, NULL, 0);

    if (params.version < 1 || params.version > 3) {
        fprintf(stderr, "unexpected transfer list version [%s]\n", line);
        goto pbiudone;
    }

    fprintf(stderr, "blockimg version is %d\n", params.version);

    // Second line in transfer list is the total number of blocks we expect to write
    line = strtok_r(NULL, "\n", &linesave);
    total_blocks = strtol(line, NULL, 0);

    if (total_blocks < 0) {
        ErrorAbort(state, "unexpected block count [%s]\n", line);
        goto pbiudone;
    } else if (total_blocks == 0) {
        rc = 0;
        goto pbiudone;
    }

    if (params.version >= 2) {
        // Third line is how many stash entries are needed simultaneously
        line = strtok_r(NULL, "\n", &linesave);
        fprintf(stderr, "maximum stash entries %s\n", line);

        // Fourth line is the maximum number of blocks that will be stashed simultaneously
        line = strtok_r(NULL, "\n", &linesave);
        stash_max_blocks = strtol(line, NULL, 0);

        if (stash_max_blocks < 0) {
            ErrorAbort(state, "unexpected maximum stash blocks [%s]\n", line);
            goto pbiudone;
        }

        if (stash_max_blocks >= 0) {
            res = CreateStash(state, stash_max_blocks, blockdev_filename->data,
                    &params.stashbase);

            if (res == -1) {
                goto pbiudone;
            }

            params.createdstash = res;
        }
    }

    // Build a hash table of the available commands
    cmdht = mzHashTableCreate(cmdcount, NULL);

    for (i = 0; i < cmdcount; ++i) {
        cmdhash = HashString(commands[i].name);
        mzHashTableLookup(cmdht, cmdhash, (void*) &commands[i], CompareCommands, true);
    }

    // Subsequent lines are all individual transfer commands
    for (line = strtok_r(NULL, "\n", &linesave); line;
         line = strtok_r(NULL, "\n", &linesave)) {

        logcmd = strdup(line);
        params.cmdname = strtok_r(line, " ", &params.cpos);

        if (params.cmdname == NULL) {
            fprintf(stderr, "missing command [%s]\n", line);
            goto pbiudone;
        }

        cmdhash = HashString(params.cmdname);
        cmd = (const Command*) mzHashTableLookup(cmdht, cmdhash, params.cmdname,
                                    CompareCommandNames, false);

        if (cmd == NULL) {
            fprintf(stderr, "unexpected command [%s]\n", params.cmdname);
            goto pbiudone;
        }

        if (cmd->f != NULL && cmd->f(&params) == -1) {
            fprintf(stderr, "failed to execute command [%s]\n",
                logcmd ? logcmd : params.cmdname);
            goto pbiudone;
        }

        if (logcmd) {
            free(logcmd);
            logcmd = NULL;
        }

        if (params.canwrite) {
            fprintf(cmd_pipe, "set_progress %.4f\n", (double) params.written / total_blocks);
            fflush(cmd_pipe);
        }
    }

    if (params.canwrite) {
        pthread_join(params.thread, NULL);

        fprintf(stderr, "wrote %d blocks; expected %d\n", params.written, total_blocks);
        fprintf(stderr, "max alloc needed was %zu\n", params.bufsize);

        // Delete stash only after successfully completing the update, as it
        // may contain blocks needed to complete the update later.
        DeleteStash(params.stashbase);
    } else {
        fprintf(stderr, "verified partition contents; update may be resumed\n");
    }

    rc = 0;

pbiudone:
    if (params.fd != -1) {
        if (fsync(params.fd) == -1) {
            fprintf(stderr, "fsync failed: %s\n", strerror(errno));
        }
        close(params.fd);
    }

    if (logcmd) {
        free(logcmd);
    }

    if (cmdht) {
        mzHashTableFree(cmdht);
    }

    if (params.buffer) {
        free(params.buffer);
    }

    if (transfer_list) {
        free(transfer_list);
    }

    if (blockdev_filename) {
        FreeValue(blockdev_filename);
    }

    if (transfer_list_value) {
        FreeValue(transfer_list_value);
    }

    if (new_data_fn) {
        FreeValue(new_data_fn);
    }

    if (patch_data_fn) {
        FreeValue(patch_data_fn);
    }

    // Only delete the stash if the update cannot be resumed, or it's
    // a verification run and we created the stash.
    if (params.isunresumable || (!params.canwrite && params.createdstash)) {
        DeleteStash(params.stashbase);
    }

    if (params.stashbase) {
        free(params.stashbase);
    }

    return StringValue(rc == 0 ? strdup("t") : strdup(""));
}

// The transfer list is a text file containing commands to
// transfer data from one place to another on the target
// partition.  We parse it and execute the commands in order:
//
//    zero [rangeset]
//      - fill the indicated blocks with zeros
//
//    new [rangeset]
//      - fill the blocks with data read from the new_data file
//
//    erase [rangeset]
//      - mark the given blocks as empty
//
//    move <...>
//    bsdiff <patchstart> <patchlen> <...>
//    imgdiff <patchstart> <patchlen> <...>
//      - read the source blocks, apply a patch (or not in the
//        case of move), write result to target blocks.  bsdiff or
//        imgdiff specifies the type of patch; move means no patch
//        at all.
//
//        The format of <...> differs between versions 1 and 2;
//        see the LoadSrcTgtVersion{1,2}() functions for a
//        description of what's expected.
//
//    stash <stash_id> <src_range>
//      - (version 2+ only) load the given source range and stash
//        the data in the given slot of the stash table.
//
// The creator of the transfer list will guarantee that no block
// is read (ie, used as the source for a patch or move) after it
// has been written.
//
// In version 2, the creator will guarantee that a given stash is
// loaded (with a stash command) before it's used in a
// move/bsdiff/imgdiff command.
//
// Within one command the source and target ranges may overlap so
// in general we need to read the entire source into memory before
// writing anything to the target blocks.
//
// All the patch data is concatenated into one patch_data file in
// the update package.  It must be stored uncompressed because we
// memory-map it in directly from the archive.  (Since patches are
// already compressed, we lose very little by not compressing
// their concatenation.)
//
// In version 3, commands that read data from the partition (i.e.
// move/bsdiff/imgdiff/stash) have one or more additional hashes
// before the range parameters, which are used to check if the
// command has already been completed and verify the integrity of
// the source data.

Value* BlockImageVerifyFn(const char* name, State* state, int argc, Expr* argv[]) {
    // Commands which are not tested are set to NULL to skip them completely
    const Command commands[] = {
        { "bsdiff",     PerformCommandDiff  },
        { "erase",      NULL                },
        { "free",       PerformCommandFree  },
        { "imgdiff",    PerformCommandDiff  },
        { "move",       PerformCommandMove  },
        { "new",        NULL                },
        { "stash",      PerformCommandStash },
        { "zero",       NULL                }
    };

    // Perform a dry run without writing to test if an update can proceed
    return PerformBlockImageUpdate(name, state, argc, argv, commands,
                sizeof(commands) / sizeof(commands[0]), 1);
}

Value* BlockImageUpdateFn(const char* name, State* state, int argc, Expr* argv[]) {
    const Command commands[] = {
        { "bsdiff",     PerformCommandDiff  },
        { "erase",      PerformCommandErase },
        { "free",       PerformCommandFree  },
        { "imgdiff",    PerformCommandDiff  },
        { "move",       PerformCommandMove  },
        { "new",        PerformCommandNew   },
        { "stash",      PerformCommandStash },
        { "zero",       PerformCommandZero  }
    };

    return PerformBlockImageUpdate(name, state, argc, argv, commands,
                sizeof(commands) / sizeof(commands[0]), 0);
}

Value* RangeSha1Fn(const char* name, State* state, int argc, Expr* argv[]) {
    Value* blockdev_filename;
    Value* ranges;
    const uint8_t* digest = NULL;
    if (ReadValueArgs(state, argv, 2, &blockdev_filename, &ranges) < 0) {
        return NULL;
    }

    if (blockdev_filename->type != VAL_STRING) {
        ErrorAbort(state, "blockdev_filename argument to %s must be string", name);
        goto done;
    }
    if (ranges->type != VAL_STRING) {
        ErrorAbort(state, "ranges argument to %s must be string", name);
        goto done;
    }

    int fd = open(blockdev_filename->data, O_RDWR);
    if (fd < 0) {
        ErrorAbort(state, "open \"%s\" failed: %s", blockdev_filename->data, strerror(errno));
        goto done;
    }

    RangeSet* rs = parse_range(ranges->data);
    uint8_t buffer[BLOCKSIZE];

    SHA_CTX ctx;
    SHA_init(&ctx);

    int i, j;
    for (i = 0; i < rs->count; ++i) {
        if (!check_lseek(fd, (off64_t)rs->pos[i*2] * BLOCKSIZE, SEEK_SET)) {
            ErrorAbort(state, "failed to seek %s: %s", blockdev_filename->data,
                strerror(errno));
            goto done;
        }

        for (j = rs->pos[i*2]; j < rs->pos[i*2+1]; ++j) {
            if (read_all(fd, buffer, BLOCKSIZE) == -1) {
                ErrorAbort(state, "failed to read %s: %s", blockdev_filename->data,
                    strerror(errno));
                goto done;
            }

            SHA_update(&ctx, buffer, BLOCKSIZE);
        }
    }
    digest = SHA_final(&ctx);
    close(fd);

  done:
    FreeValue(blockdev_filename);
    FreeValue(ranges);
    if (digest == NULL) {
        return StringValue(strdup(""));
    } else {
        return StringValue(PrintSha1(digest));
    }
}

void RegisterBlockImageFunctions() {
    RegisterFunction("block_image_verify", BlockImageVerifyFn);
    RegisterFunction("block_image_update", BlockImageUpdateFn);
    RegisterFunction("range_sha1", RangeSha1Fn);
}
