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
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "applypatch/applypatch.h"
#include "edify/expr.h"
#include "mincrypt/sha.h"
#include "minzip/DirUtil.h"
#include "updater.h"

#define BLOCKSIZE 4096

// Set this to 0 to interpret 'erase' transfers to mean do a
// BLKDISCARD ioctl (the normal behavior).  Set to 1 to interpret
// erase to mean fill the region with zeroes.
#define DEBUG_ERASE  0

#ifndef BLKDISCARD
#define BLKDISCARD _IO(0x12,119)
#endif

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
        fprintf(stderr, "failed to allocate range of %lu bytes\n",
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

static void readblock(int fd, uint8_t* data, size_t size) {
    size_t so_far = 0;
    while (so_far < size) {
        ssize_t r = read(fd, data+so_far, size-so_far);
        if (r < 0 && errno != EINTR) {
            fprintf(stderr, "read failed: %s\n", strerror(errno));
            return;
        } else {
            so_far += r;
        }
    }
}

static void writeblock(int fd, const uint8_t* data, size_t size) {
    size_t written = 0;
    while (written < size) {
        ssize_t w = write(fd, data+written, size-written);
        if (w < 0 && errno != EINTR) {
            fprintf(stderr, "write failed: %s\n", strerror(errno));
            return;
        } else {
            written += w;
        }
    }
}

static void check_lseek(int fd, off64_t offset, int whence) {
    while (true) {
        off64_t ret = lseek64(fd, offset, whence);
        if (ret < 0) {
            if (errno != EINTR) {
                fprintf(stderr, "lseek64 failed: %s\n", strerror(errno));
                exit(1);
            }
        } else {
            break;
        }
    }
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
        exit(1);
    }

    ssize_t written = 0;
    while (size > 0) {
        size_t write_now = size;
        if (rss->p_remain < write_now) write_now = rss->p_remain;
        writeblock(rss->fd, data, write_now);
        data += write_now;
        size -= write_now;

        rss->p_remain -= write_now;
        written += write_now;

        if (rss->p_remain == 0) {
            // move to the next block
            ++rss->p_block;
            if (rss->p_block < rss->tgt->count) {
                rss->p_remain = (rss->tgt->pos[rss->p_block*2+1] - rss->tgt->pos[rss->p_block*2]) * BLOCKSIZE;
                check_lseek(rss->fd, (off64_t)rss->tgt->pos[rss->p_block*2] * BLOCKSIZE, SEEK_SET);
            } else {
                // we can't write any more; return how many bytes have
                // been written so far.
                return written;
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

// args:
//    - block device (or file) to modify in-place
//    - transfer list (blob)
//    - new data stream (filename within package.zip)
//    - patch stream (filename within package.zip, must be uncompressed)

Value* BlockImageUpdateFn(const char* name, State* state, int argc, Expr* argv[]) {
    Value* blockdev_filename;
    Value* transfer_list_value;
    char* transfer_list = NULL;
    Value* new_data_fn;
    Value* patch_data_fn;
    bool success = false;

    if (ReadValueArgs(state, argv, 4, &blockdev_filename, &transfer_list_value,
                      &new_data_fn, &patch_data_fn) < 0) {
        return NULL;
    }

    if (blockdev_filename->type != VAL_STRING) {
        ErrorAbort(state, "blockdev_filename argument to %s must be string", name);
        goto done;
    }
    if (transfer_list_value->type != VAL_BLOB) {
        ErrorAbort(state, "transfer_list argument to %s must be blob", name);
        goto done;
    }
    if (new_data_fn->type != VAL_STRING) {
        ErrorAbort(state, "new_data_fn argument to %s must be string", name);
        goto done;
    }
    if (patch_data_fn->type != VAL_STRING) {
        ErrorAbort(state, "patch_data_fn argument to %s must be string", name);
        goto done;
    }

    UpdaterInfo* ui = (UpdaterInfo*)(state->cookie);
    FILE* cmd_pipe = ui->cmd_pipe;

    ZipArchive* za = ((UpdaterInfo*)(state->cookie))->package_zip;

    const ZipEntry* patch_entry = mzFindZipEntry(za, patch_data_fn->data);
    if (patch_entry == NULL) {
        ErrorAbort(state, "%s(): no file \"%s\" in package", name, patch_data_fn->data);
        goto done;
    }

    uint8_t* patch_start = ((UpdaterInfo*)(state->cookie))->package_zip_addr +
        mzGetZipEntryOffset(patch_entry);

    const ZipEntry* new_entry = mzFindZipEntry(za, new_data_fn->data);
    if (new_entry == NULL) {
        ErrorAbort(state, "%s(): no file \"%s\" in package", name, new_data_fn->data);
        goto done;
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
    //    bsdiff patchstart patchlen [src rangeset] [tgt rangeset]
    //    imgdiff patchstart patchlen [src rangeset] [tgt rangeset]
    //      - read the source blocks, apply a patch, write result to
    //        target blocks.  bsdiff or imgdiff specifies the type of
    //        patch.
    //
    //    move [src rangeset] [tgt rangeset]
    //      - copy data from source blocks to target blocks (no patch
    //        needed; rangesets are the same size)
    //
    //    erase [rangeset]
    //      - mark the given blocks as empty
    //
    // The creator of the transfer list will guarantee that no block
    // is read (ie, used as the source for a patch or move) after it
    // has been written.
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

    pthread_t new_data_thread;
    NewThreadInfo nti;
    nti.za = za;
    nti.entry = new_entry;
    nti.rss = NULL;
    pthread_mutex_init(&nti.mu, NULL);
    pthread_cond_init(&nti.cv, NULL);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    pthread_create(&new_data_thread, &attr, unzip_new_data, &nti);

    int i, j;

    char* linesave;
    char* wordsave;

    int fd = open(blockdev_filename->data, O_RDWR);
    if (fd < 0) {
        ErrorAbort(state, "failed to open %s: %s", blockdev_filename->data, strerror(errno));
        goto done;
    }

    char* line;
    char* word;

    // The data in transfer_list_value is not necessarily
    // null-terminated, so we need to copy it to a new buffer and add
    // the null that strtok_r will need.
    transfer_list = malloc(transfer_list_value->size+1);
    if (transfer_list == NULL) {
        fprintf(stderr, "failed to allocate %zd bytes for transfer list\n",
                transfer_list_value->size+1);
        exit(1);
    }
    memcpy(transfer_list, transfer_list_value->data, transfer_list_value->size);
    transfer_list[transfer_list_value->size] = '\0';

    line = strtok_r(transfer_list, "\n", &linesave);

    // first line in transfer list is the version number; currently
    // there's only version 1.
    if (strcmp(line, "1") != 0) {
        ErrorAbort(state, "unexpected transfer list version [%s]\n", line);
        goto done;
    }

    // second line in transfer list is the total number of blocks we
    // expect to write.
    line = strtok_r(NULL, "\n", &linesave);
    int total_blocks = strtol(line, NULL, 0);
    // shouldn't happen, but avoid divide by zero.
    if (total_blocks == 0) ++total_blocks;
    int blocks_so_far = 0;

    uint8_t* buffer = NULL;
    size_t buffer_alloc = 0;

    // third and subsequent lines are all individual transfer commands.
    for (line = strtok_r(NULL, "\n", &linesave); line;
         line = strtok_r(NULL, "\n", &linesave)) {
        char* style;
        style = strtok_r(line, " ", &wordsave);

        if (strcmp("move", style) == 0) {
            word = strtok_r(NULL, " ", &wordsave);
            RangeSet* src = parse_range(word);
            word = strtok_r(NULL, " ", &wordsave);
            RangeSet* tgt = parse_range(word);

            printf("  moving %d blocks\n", src->size);

            allocate(src->size * BLOCKSIZE, &buffer, &buffer_alloc);
            size_t p = 0;
            for (i = 0; i < src->count; ++i) {
                check_lseek(fd, (off64_t)src->pos[i*2] * BLOCKSIZE, SEEK_SET);
                size_t sz = (src->pos[i*2+1] - src->pos[i*2]) * BLOCKSIZE;
                readblock(fd, buffer+p, sz);
                p += sz;
            }

            p = 0;
            for (i = 0; i < tgt->count; ++i) {
                check_lseek(fd, (off64_t)tgt->pos[i*2] * BLOCKSIZE, SEEK_SET);
                size_t sz = (tgt->pos[i*2+1] - tgt->pos[i*2]) * BLOCKSIZE;
                writeblock(fd, buffer+p, sz);
                p += sz;
            }

            blocks_so_far += tgt->size;
            fprintf(cmd_pipe, "set_progress %.4f\n", (double)blocks_so_far / total_blocks);
            fflush(cmd_pipe);

            free(src);
            free(tgt);

        } else if (strcmp("zero", style) == 0 ||
                   (DEBUG_ERASE && strcmp("erase", style) == 0)) {
            word = strtok_r(NULL, " ", &wordsave);
            RangeSet* tgt = parse_range(word);

            printf("  zeroing %d blocks\n", tgt->size);

            allocate(BLOCKSIZE, &buffer, &buffer_alloc);
            memset(buffer, 0, BLOCKSIZE);
            for (i = 0; i < tgt->count; ++i) {
                check_lseek(fd, (off64_t)tgt->pos[i*2] * BLOCKSIZE, SEEK_SET);
                for (j = tgt->pos[i*2]; j < tgt->pos[i*2+1]; ++j) {
                    writeblock(fd, buffer, BLOCKSIZE);
                }
            }

            if (style[0] == 'z') {   // "zero" but not "erase"
                blocks_so_far += tgt->size;
                fprintf(cmd_pipe, "set_progress %.4f\n", (double)blocks_so_far / total_blocks);
                fflush(cmd_pipe);
            }

            free(tgt);
        } else if (strcmp("new", style) == 0) {

            word = strtok_r(NULL, " ", &wordsave);
            RangeSet* tgt = parse_range(word);

            printf("  writing %d blocks of new data\n", tgt->size);

            RangeSinkState rss;
            rss.fd = fd;
            rss.tgt = tgt;
            rss.p_block = 0;
            rss.p_remain = (tgt->pos[1] - tgt->pos[0]) * BLOCKSIZE;
            check_lseek(fd, (off64_t)tgt->pos[0] * BLOCKSIZE, SEEK_SET);

            pthread_mutex_lock(&nti.mu);
            nti.rss = &rss;
            pthread_cond_broadcast(&nti.cv);
            while (nti.rss) {
                pthread_cond_wait(&nti.cv, &nti.mu);
            }
            pthread_mutex_unlock(&nti.mu);

            blocks_so_far += tgt->size;
            fprintf(cmd_pipe, "set_progress %.4f\n", (double)blocks_so_far / total_blocks);
            fflush(cmd_pipe);

            free(tgt);

        } else if (strcmp("bsdiff", style) == 0 ||
                   strcmp("imgdiff", style) == 0) {
            word = strtok_r(NULL, " ", &wordsave);
            size_t patch_offset = strtoul(word, NULL, 0);
            word = strtok_r(NULL, " ", &wordsave);
            size_t patch_len = strtoul(word, NULL, 0);

            word = strtok_r(NULL, " ", &wordsave);
            RangeSet* src = parse_range(word);
            word = strtok_r(NULL, " ", &wordsave);
            RangeSet* tgt = parse_range(word);

            printf("  patching %d blocks to %d\n", src->size, tgt->size);

            // Read the source into memory.
            allocate(src->size * BLOCKSIZE, &buffer, &buffer_alloc);
            size_t p = 0;
            for (i = 0; i < src->count; ++i) {
                check_lseek(fd, (off64_t)src->pos[i*2] * BLOCKSIZE, SEEK_SET);
                size_t sz = (src->pos[i*2+1] - src->pos[i*2]) * BLOCKSIZE;
                readblock(fd, buffer+p, sz);
                p += sz;
            }

            Value patch_value;
            patch_value.type = VAL_BLOB;
            patch_value.size = patch_len;
            patch_value.data = (char*)(patch_start + patch_offset);

            RangeSinkState rss;
            rss.fd = fd;
            rss.tgt = tgt;
            rss.p_block = 0;
            rss.p_remain = (tgt->pos[1] - tgt->pos[0]) * BLOCKSIZE;
            check_lseek(fd, (off64_t)tgt->pos[0] * BLOCKSIZE, SEEK_SET);

            if (style[0] == 'i') {      // imgdiff
                ApplyImagePatch(buffer, src->size * BLOCKSIZE,
                                &patch_value,
                                &RangeSinkWrite, &rss, NULL, NULL);
            } else {
                ApplyBSDiffPatch(buffer, src->size * BLOCKSIZE,
                                 &patch_value, 0,
                                 &RangeSinkWrite, &rss, NULL);
            }

            // We expect the output of the patcher to fill the tgt ranges exactly.
            if (rss.p_block != tgt->count || rss.p_remain != 0) {
                fprintf(stderr, "range sink underrun?\n");
            }

            blocks_so_far += tgt->size;
            fprintf(cmd_pipe, "set_progress %.4f\n", (double)blocks_so_far / total_blocks);
            fflush(cmd_pipe);

            free(src);
            free(tgt);
        } else if (!DEBUG_ERASE && strcmp("erase", style) == 0) {
            struct stat st;
            if (fstat(fd, &st) == 0 && S_ISBLK(st.st_mode)) {
                word = strtok_r(NULL, " ", &wordsave);
                RangeSet* tgt = parse_range(word);

                printf("  erasing %d blocks\n", tgt->size);

                for (i = 0; i < tgt->count; ++i) {
                    uint64_t range[2];
                    // offset in bytes
                    range[0] = tgt->pos[i*2] * (uint64_t)BLOCKSIZE;
                    // len in bytes
                    range[1] = (tgt->pos[i*2+1] - tgt->pos[i*2]) * (uint64_t)BLOCKSIZE;

                    if (ioctl(fd, BLKDISCARD, &range) < 0) {
                        printf("    blkdiscard failed: %s\n", strerror(errno));
                    }
                }

                free(tgt);
            } else {
                printf("  ignoring erase (not block device)\n");
            }
        } else {
            fprintf(stderr, "unknown transfer style \"%s\"\n", style);
            exit(1);
        }
    }

    pthread_join(new_data_thread, NULL);
    success = true;

    free(buffer);
    printf("wrote %d blocks; expected %d\n", blocks_so_far, total_blocks);
    printf("max alloc needed was %zu\n", buffer_alloc);

  done:
    free(transfer_list);
    FreeValue(blockdev_filename);
    FreeValue(transfer_list_value);
    FreeValue(new_data_fn);
    FreeValue(patch_data_fn);
    return StringValue(success ? strdup("t") : strdup(""));
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
        ErrorAbort(state, "failed to open %s: %s", blockdev_filename->data, strerror(errno));
        goto done;
    }

    RangeSet* rs = parse_range(ranges->data);
    uint8_t buffer[BLOCKSIZE];

    SHA_CTX ctx;
    SHA_init(&ctx);

    int i, j;
    for (i = 0; i < rs->count; ++i) {
        check_lseek(fd, (off64_t)rs->pos[i*2] * BLOCKSIZE, SEEK_SET);
        for (j = rs->pos[i*2]; j < rs->pos[i*2+1]; ++j) {
            readblock(fd, buffer, BLOCKSIZE);
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
    RegisterFunction("block_image_update", BlockImageUpdateFn);
    RegisterFunction("range_sha1", RangeSha1Fn);
}
