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
#include <linux/fs.h>
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
#include <fec/io.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <android-base/parseint.h>
#include <android-base/strings.h>

#include "applypatch/applypatch.h"
#include "edify/expr.h"
#include "error_code.h"
#include "install.h"
#include "openssl/sha.h"
#include "minzip/Hash.h"
#include "ota_io.h"
#include "print_sha1.h"
#include "unique_fd.h"
#include "updater.h"

#define BLOCKSIZE 4096

// Set this to 0 to interpret 'erase' transfers to mean do a
// BLKDISCARD ioctl (the normal behavior).  Set to 1 to interpret
// erase to mean fill the region with zeroes.
#define DEBUG_ERASE  0

#define STASH_DIRECTORY_BASE "/cache/recovery"
#define STASH_DIRECTORY_MODE 0700
#define STASH_FILE_MODE 0600

struct RangeSet {
    size_t count;             // Limit is INT_MAX.
    size_t size;
    std::vector<size_t> pos;  // Actual limit is INT_MAX.
};

static CauseCode failure_type = kNoCause;
static bool is_retry = false;
static std::map<std::string, RangeSet> stash_map;

static void parse_range(const std::string& range_text, RangeSet& rs) {

    std::vector<std::string> pieces = android::base::Split(range_text, ",");
    if (pieces.size() < 3) {
        goto err;
    }

    size_t num;
    if (!android::base::ParseUint(pieces[0].c_str(), &num, static_cast<size_t>(INT_MAX))) {
        goto err;
    }

    if (num == 0 || num % 2) {
        goto err; // must be even
    } else if (num != pieces.size() - 1) {
        goto err;
    }

    rs.pos.resize(num);
    rs.count = num / 2;
    rs.size = 0;

    for (size_t i = 0; i < num; i += 2) {
        if (!android::base::ParseUint(pieces[i+1].c_str(), &rs.pos[i],
                                      static_cast<size_t>(INT_MAX))) {
            goto err;
        }

        if (!android::base::ParseUint(pieces[i+2].c_str(), &rs.pos[i+1],
                                      static_cast<size_t>(INT_MAX))) {
            goto err;
        }

        if (rs.pos[i] >= rs.pos[i+1]) {
            goto err; // empty or negative range
        }

        size_t sz = rs.pos[i+1] - rs.pos[i];
        if (rs.size > SIZE_MAX - sz) {
            goto err; // overflow
        }

        rs.size += sz;
    }

    return;

err:
    fprintf(stderr, "failed to parse range '%s'\n", range_text.c_str());
    exit(1);
}

static bool range_overlaps(const RangeSet& r1, const RangeSet& r2) {
    for (size_t i = 0; i < r1.count; ++i) {
        size_t r1_0 = r1.pos[i * 2];
        size_t r1_1 = r1.pos[i * 2 + 1];

        for (size_t j = 0; j < r2.count; ++j) {
            size_t r2_0 = r2.pos[j * 2];
            size_t r2_1 = r2.pos[j * 2 + 1];

            if (!(r2_0 >= r1_1 || r1_0 >= r2_1)) {
                return true;
            }
        }
    }

    return false;
}

static int read_all(int fd, uint8_t* data, size_t size) {
    size_t so_far = 0;
    while (so_far < size) {
        ssize_t r = TEMP_FAILURE_RETRY(ota_read(fd, data+so_far, size-so_far));
        if (r == -1) {
            failure_type = kFreadFailure;
            fprintf(stderr, "read failed: %s\n", strerror(errno));
            return -1;
        }
        so_far += r;
    }
    return 0;
}

static int read_all(int fd, std::vector<uint8_t>& buffer, size_t size) {
    return read_all(fd, buffer.data(), size);
}

static int write_all(int fd, const uint8_t* data, size_t size) {
    size_t written = 0;
    while (written < size) {
        ssize_t w = TEMP_FAILURE_RETRY(ota_write(fd, data+written, size-written));
        if (w == -1) {
            failure_type = kFwriteFailure;
            fprintf(stderr, "write failed: %s\n", strerror(errno));
            return -1;
        }
        written += w;
    }

    return 0;
}

static int write_all(int fd, const std::vector<uint8_t>& buffer, size_t size) {
    return write_all(fd, buffer.data(), size);
}

static bool discard_blocks(int fd, off64_t offset, uint64_t size) {
    // Don't discard blocks unless the update is a retry run.
    if (!is_retry) {
        return true;
    }

    uint64_t args[2] = {static_cast<uint64_t>(offset), size};
    int status = ioctl(fd, BLKDISCARD, &args);
    if (status == -1) {
        fprintf(stderr, "BLKDISCARD ioctl failed: %s\n", strerror(errno));
        return false;
    }
    return true;
}

static bool check_lseek(int fd, off64_t offset, int whence) {
    off64_t rc = TEMP_FAILURE_RETRY(lseek64(fd, offset, whence));
    if (rc == -1) {
        failure_type = kLseekFailure;
        fprintf(stderr, "lseek64 failed: %s\n", strerror(errno));
        return false;
    }
    return true;
}

static void allocate(size_t size, std::vector<uint8_t>& buffer) {
    // if the buffer's big enough, reuse it.
    if (size <= buffer.size()) return;

    buffer.resize(size);
}

struct RangeSinkState {
    RangeSinkState(RangeSet& rs) : tgt(rs) { };

    int fd;
    const RangeSet& tgt;
    size_t p_block;
    size_t p_remain;
};

static ssize_t RangeSinkWrite(const uint8_t* data, ssize_t size, void* token) {
    RangeSinkState* rss = reinterpret_cast<RangeSinkState*>(token);

    if (rss->p_remain == 0) {
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
            if (rss->p_block < rss->tgt.count) {
                rss->p_remain = (rss->tgt.pos[rss->p_block * 2 + 1] -
                                 rss->tgt.pos[rss->p_block * 2]) * BLOCKSIZE;

                off64_t offset = static_cast<off64_t>(rss->tgt.pos[rss->p_block*2]) * BLOCKSIZE;
                if (!discard_blocks(rss->fd, offset, rss->p_remain)) {
                    break;
                }

                if (!check_lseek(rss->fd, offset, SEEK_SET)) {
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

struct NewThreadInfo {
    ZipArchive* za;
    const ZipEntry* entry;

    RangeSinkState* rss;

    pthread_mutex_t mu;
    pthread_cond_t cv;
};

static bool receive_new_data(const unsigned char* data, int size, void* cookie) {
    NewThreadInfo* nti = reinterpret_cast<NewThreadInfo*>(cookie);

    while (size > 0) {
        // Wait for nti->rss to be non-null, indicating some of this
        // data is wanted.
        pthread_mutex_lock(&nti->mu);
        while (nti->rss == nullptr) {
            pthread_cond_wait(&nti->cv, &nti->mu);
        }
        pthread_mutex_unlock(&nti->mu);

        // At this point nti->rss is set, and we own it.  The main
        // thread is waiting for it to disappear from nti.
        ssize_t written = RangeSinkWrite(data, size, nti->rss);
        data += written;
        size -= written;

        if (nti->rss->p_block == nti->rss->tgt.count) {
            // we have written all the bytes desired by this rss.

            pthread_mutex_lock(&nti->mu);
            nti->rss = nullptr;
            pthread_cond_broadcast(&nti->cv);
            pthread_mutex_unlock(&nti->mu);
        }
    }

    return true;
}

static void* unzip_new_data(void* cookie) {
    NewThreadInfo* nti = (NewThreadInfo*) cookie;
    mzProcessZipEntryContents(nti->za, nti->entry, receive_new_data, nti);
    return nullptr;
}

static int ReadBlocks(const RangeSet& src, std::vector<uint8_t>& buffer, int fd) {
    size_t p = 0;
    uint8_t* data = buffer.data();

    for (size_t i = 0; i < src.count; ++i) {
        if (!check_lseek(fd, (off64_t) src.pos[i * 2] * BLOCKSIZE, SEEK_SET)) {
            return -1;
        }

        size_t size = (src.pos[i * 2 + 1] - src.pos[i * 2]) * BLOCKSIZE;

        if (read_all(fd, data + p, size) == -1) {
            return -1;
        }

        p += size;
    }

    return 0;
}

static int WriteBlocks(const RangeSet& tgt, const std::vector<uint8_t>& buffer, int fd) {
    const uint8_t* data = buffer.data();

    size_t p = 0;
    for (size_t i = 0; i < tgt.count; ++i) {
        off64_t offset = static_cast<off64_t>(tgt.pos[i * 2]) * BLOCKSIZE;
        size_t size = (tgt.pos[i * 2 + 1] - tgt.pos[i * 2]) * BLOCKSIZE;
        if (!discard_blocks(fd, offset, size)) {
            return -1;
        }

        if (!check_lseek(fd, offset, SEEK_SET)) {
            return -1;
        }

        if (write_all(fd, data + p, size) == -1) {
            return -1;
        }

        p += size;
    }

    return 0;
}

// Parameters for transfer list command functions
struct CommandParameters {
    std::vector<std::string> tokens;
    size_t cpos;
    const char* cmdname;
    const char* cmdline;
    std::string freestash;
    std::string stashbase;
    bool canwrite;
    int createdstash;
    int fd;
    bool foundwrites;
    bool isunresumable;
    int version;
    size_t written;
    size_t stashed;
    NewThreadInfo nti;
    pthread_t thread;
    std::vector<uint8_t> buffer;
    uint8_t* patch_start;
};

// Do a source/target load for move/bsdiff/imgdiff in version 1.
// We expect to parse the remainder of the parameter tokens as:
//
//    <src_range> <tgt_range>
//
// The source range is loaded into the provided buffer, reallocating
// it to make it larger if necessary.

static int LoadSrcTgtVersion1(CommandParameters& params, RangeSet& tgt, size_t& src_blocks,
        std::vector<uint8_t>& buffer, int fd) {

    if (params.cpos + 1 >= params.tokens.size()) {
        fprintf(stderr, "invalid parameters\n");
        return -1;
    }

    // <src_range>
    RangeSet src;
    parse_range(params.tokens[params.cpos++], src);

    // <tgt_range>
    parse_range(params.tokens[params.cpos++], tgt);

    allocate(src.size * BLOCKSIZE, buffer);
    int rc = ReadBlocks(src, buffer, fd);
    src_blocks = src.size;

    return rc;
}

static int VerifyBlocks(const std::string& expected, const std::vector<uint8_t>& buffer,
        const size_t blocks, bool printerror) {
    uint8_t digest[SHA_DIGEST_LENGTH];
    const uint8_t* data = buffer.data();

    SHA1(data, blocks * BLOCKSIZE, digest);

    std::string hexdigest = print_sha1(digest);

    if (hexdigest != expected) {
        if (printerror) {
            fprintf(stderr, "failed to verify blocks (expected %s, read %s)\n",
                    expected.c_str(), hexdigest.c_str());
        }
        return -1;
    }

    return 0;
}

static std::string GetStashFileName(const std::string& base, const std::string& id,
        const std::string& postfix) {
    if (base.empty()) {
        return "";
    }

    std::string fn(STASH_DIRECTORY_BASE);
    fn += "/" + base + "/" + id + postfix;

    return fn;
}

typedef void (*StashCallback)(const std::string&, void*);

// Does a best effort enumeration of stash files. Ignores possible non-file
// items in the stash directory and continues despite of errors. Calls the
// 'callback' function for each file and passes 'data' to the function as a
// parameter.

static void EnumerateStash(const std::string& dirname, StashCallback callback, void* data) {
    if (dirname.empty() || callback == nullptr) {
        return;
    }

    std::unique_ptr<DIR, int(*)(DIR*)> directory(opendir(dirname.c_str()), closedir);

    if (directory == nullptr) {
        if (errno != ENOENT) {
            fprintf(stderr, "opendir \"%s\" failed: %s\n", dirname.c_str(), strerror(errno));
        }
        return;
    }

    struct dirent* item;
    while ((item = readdir(directory.get())) != nullptr) {
        if (item->d_type != DT_REG) {
            continue;
        }

        std::string fn = dirname + "/" + std::string(item->d_name);
        callback(fn, data);
    }
}

static void UpdateFileSize(const std::string& fn, void* data) {
    if (fn.empty() || !data) {
        return;
    }

    struct stat sb;
    if (stat(fn.c_str(), &sb) == -1) {
        fprintf(stderr, "stat \"%s\" failed: %s\n", fn.c_str(), strerror(errno));
        return;
    }

    int* size = reinterpret_cast<int*>(data);
    *size += sb.st_size;
}

// Deletes the stash directory and all files in it. Assumes that it only
// contains files. There is nothing we can do about unlikely, but possible
// errors, so they are merely logged.

static void DeleteFile(const std::string& fn, void* /* data */) {
    if (!fn.empty()) {
        fprintf(stderr, "deleting %s\n", fn.c_str());

        if (unlink(fn.c_str()) == -1 && errno != ENOENT) {
            fprintf(stderr, "unlink \"%s\" failed: %s\n", fn.c_str(), strerror(errno));
        }
    }
}

static void DeletePartial(const std::string& fn, void* data) {
    if (android::base::EndsWith(fn, ".partial")) {
        DeleteFile(fn, data);
    }
}

static void DeleteStash(const std::string& base) {
    if (base.empty()) {
        return;
    }

    fprintf(stderr, "deleting stash %s\n", base.c_str());

    std::string dirname = GetStashFileName(base, "", "");
    EnumerateStash(dirname, DeleteFile, nullptr);

    if (rmdir(dirname.c_str()) == -1) {
        if (errno != ENOENT && errno != ENOTDIR) {
            fprintf(stderr, "rmdir \"%s\" failed: %s\n", dirname.c_str(), strerror(errno));
        }
    }
}

static int LoadStash(CommandParameters& params, const std::string& base, const std::string& id,
        bool verify, size_t* blocks, std::vector<uint8_t>& buffer, bool printnoent) {
    // In verify mode, if source range_set was saved for the given hash,
    // check contents in the source blocks first. If the check fails,
    // search for the stashed files on /cache as usual.
    if (!params.canwrite) {
        if (stash_map.find(id) != stash_map.end()) {
            const RangeSet& src = stash_map[id];
            allocate(src.size * BLOCKSIZE, buffer);

            if (ReadBlocks(src, buffer, params.fd) == -1) {
                fprintf(stderr, "failed to read source blocks in stash map.\n");
                return -1;
            }
            if (VerifyBlocks(id, buffer, src.size, true) != 0) {
                fprintf(stderr, "failed to verify loaded source blocks in stash map.\n");
                return -1;
            }
            return 0;
        }
    }

    if (base.empty()) {
        return -1;
    }

    size_t blockcount = 0;

    if (!blocks) {
        blocks = &blockcount;
    }

    std::string fn = GetStashFileName(base, id, "");

    struct stat sb;
    int res = stat(fn.c_str(), &sb);

    if (res == -1) {
        if (errno != ENOENT || printnoent) {
            fprintf(stderr, "stat \"%s\" failed: %s\n", fn.c_str(), strerror(errno));
        }
        return -1;
    }

    fprintf(stderr, " loading %s\n", fn.c_str());

    if ((sb.st_size % BLOCKSIZE) != 0) {
        fprintf(stderr, "%s size %" PRId64 " not multiple of block size %d",
                fn.c_str(), static_cast<int64_t>(sb.st_size), BLOCKSIZE);
        return -1;
    }

    int fd = TEMP_FAILURE_RETRY(open(fn.c_str(), O_RDONLY));
    unique_fd fd_holder(fd);

    if (fd == -1) {
        fprintf(stderr, "open \"%s\" failed: %s\n", fn.c_str(), strerror(errno));
        return -1;
    }

    allocate(sb.st_size, buffer);

    if (read_all(fd, buffer, sb.st_size) == -1) {
        return -1;
    }

    *blocks = sb.st_size / BLOCKSIZE;

    if (verify && VerifyBlocks(id, buffer, *blocks, true) != 0) {
        fprintf(stderr, "unexpected contents in %s\n", fn.c_str());
        DeleteFile(fn, nullptr);
        return -1;
    }

    return 0;
}

static int WriteStash(const std::string& base, const std::string& id, int blocks,
        std::vector<uint8_t>& buffer, bool checkspace, bool *exists) {
    if (base.empty()) {
        return -1;
    }

    if (checkspace && CacheSizeCheck(blocks * BLOCKSIZE) != 0) {
        fprintf(stderr, "not enough space to write stash\n");
        return -1;
    }

    std::string fn = GetStashFileName(base, id, ".partial");
    std::string cn = GetStashFileName(base, id, "");

    if (exists) {
        struct stat sb;
        int res = stat(cn.c_str(), &sb);

        if (res == 0) {
            // The file already exists and since the name is the hash of the contents,
            // it's safe to assume the contents are identical (accidental hash collisions
            // are unlikely)
            fprintf(stderr, " skipping %d existing blocks in %s\n", blocks, cn.c_str());
            *exists = true;
            return 0;
        }

        *exists = false;
    }

    fprintf(stderr, " writing %d blocks to %s\n", blocks, cn.c_str());

    int fd = TEMP_FAILURE_RETRY(open(fn.c_str(), O_WRONLY | O_CREAT | O_TRUNC, STASH_FILE_MODE));
    unique_fd fd_holder(fd);

    if (fd == -1) {
        fprintf(stderr, "failed to create \"%s\": %s\n", fn.c_str(), strerror(errno));
        return -1;
    }

    if (write_all(fd, buffer, blocks * BLOCKSIZE) == -1) {
        return -1;
    }

    if (ota_fsync(fd) == -1) {
        failure_type = kFsyncFailure;
        fprintf(stderr, "fsync \"%s\" failed: %s\n", fn.c_str(), strerror(errno));
        return -1;
    }

    if (rename(fn.c_str(), cn.c_str()) == -1) {
        fprintf(stderr, "rename(\"%s\", \"%s\") failed: %s\n", fn.c_str(), cn.c_str(),
                strerror(errno));
        return -1;
    }

    std::string dname = GetStashFileName(base, "", "");
    int dfd = TEMP_FAILURE_RETRY(open(dname.c_str(), O_RDONLY | O_DIRECTORY));
    unique_fd dfd_holder(dfd);

    if (dfd == -1) {
        failure_type = kFileOpenFailure;
        fprintf(stderr, "failed to open \"%s\" failed: %s\n", dname.c_str(), strerror(errno));
        return -1;
    }

    if (ota_fsync(dfd) == -1) {
        failure_type = kFsyncFailure;
        fprintf(stderr, "fsync \"%s\" failed: %s\n", dname.c_str(), strerror(errno));
        return -1;
    }

    return 0;
}

// Creates a directory for storing stash files and checks if the /cache partition
// hash enough space for the expected amount of blocks we need to store. Returns
// >0 if we created the directory, zero if it existed already, and <0 of failure.

static int CreateStash(State* state, int maxblocks, const char* blockdev, std::string& base) {
    if (blockdev == nullptr) {
        return -1;
    }

    // Stash directory should be different for each partition to avoid conflicts
    // when updating multiple partitions at the same time, so we use the hash of
    // the block device name as the base directory
    uint8_t digest[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const uint8_t*>(blockdev), strlen(blockdev), digest);
    base = print_sha1(digest);

    std::string dirname = GetStashFileName(base, "", "");
    struct stat sb;
    int res = stat(dirname.c_str(), &sb);

    if (res == -1 && errno != ENOENT) {
        ErrorAbort(state, kStashCreationFailure, "stat \"%s\" failed: %s\n",
                   dirname.c_str(), strerror(errno));
        return -1;
    } else if (res != 0) {
        fprintf(stderr, "creating stash %s\n", dirname.c_str());
        res = mkdir(dirname.c_str(), STASH_DIRECTORY_MODE);

        if (res != 0) {
            ErrorAbort(state, kStashCreationFailure, "mkdir \"%s\" failed: %s\n",
                       dirname.c_str(), strerror(errno));
            return -1;
        }

        if (CacheSizeCheck(maxblocks * BLOCKSIZE) != 0) {
            ErrorAbort(state, kStashCreationFailure, "not enough space for stash\n");
            return -1;
        }

        return 1;  // Created directory
    }

    fprintf(stderr, "using existing stash %s\n", dirname.c_str());

    // If the directory already exists, calculate the space already allocated to
    // stash files and check if there's enough for all required blocks. Delete any
    // partially completed stash files first.

    EnumerateStash(dirname, DeletePartial, nullptr);
    int size = 0;
    EnumerateStash(dirname, UpdateFileSize, &size);

    size = maxblocks * BLOCKSIZE - size;

    if (size > 0 && CacheSizeCheck(size) != 0) {
        ErrorAbort(state, kStashCreationFailure, "not enough space for stash (%d more needed)\n",
                   size);
        return -1;
    }

    return 0; // Using existing directory
}

static int SaveStash(CommandParameters& params, const std::string& base,
        std::vector<uint8_t>& buffer, int fd, bool usehash) {

    // <stash_id> <src_range>
    if (params.cpos + 1 >= params.tokens.size()) {
        fprintf(stderr, "missing id and/or src range fields in stash command\n");
        return -1;
    }
    const std::string& id = params.tokens[params.cpos++];

    size_t blocks = 0;
    if (usehash && LoadStash(params, base, id, true, &blocks, buffer, false) == 0) {
        // Stash file already exists and has expected contents. Do not
        // read from source again, as the source may have been already
        // overwritten during a previous attempt.
        return 0;
    }

    RangeSet src;
    parse_range(params.tokens[params.cpos++], src);

    allocate(src.size * BLOCKSIZE, buffer);
    if (ReadBlocks(src, buffer, fd) == -1) {
        return -1;
    }
    blocks = src.size;

    if (usehash && VerifyBlocks(id, buffer, blocks, true) != 0) {
        // Source blocks have unexpected contents. If we actually need this
        // data later, this is an unrecoverable error. However, the command
        // that uses the data may have already completed previously, so the
        // possible failure will occur during source block verification.
        fprintf(stderr, "failed to load source blocks for stash %s\n", id.c_str());
        return 0;
    }

    // In verify mode, save source range_set instead of stashing blocks.
    if (!params.canwrite && usehash) {
        stash_map[id] = src;
        return 0;
    }

    fprintf(stderr, "stashing %zu blocks to %s\n", blocks, id.c_str());
    params.stashed += blocks;
    return WriteStash(base, id, blocks, buffer, false, nullptr);
}

static int FreeStash(const std::string& base, const std::string& id) {
    if (base.empty() || id.empty()) {
        return -1;
    }

    std::string fn = GetStashFileName(base, id, "");
    DeleteFile(fn, nullptr);

    return 0;
}

static void MoveRange(std::vector<uint8_t>& dest, const RangeSet& locs,
        const std::vector<uint8_t>& source) {
    // source contains packed data, which we want to move to the
    // locations given in locs in the dest buffer.  source and dest
    // may be the same buffer.

    const uint8_t* from = source.data();
    uint8_t* to = dest.data();
    size_t start = locs.size;
    for (int i = locs.count-1; i >= 0; --i) {
        size_t blocks = locs.pos[i*2+1] - locs.pos[i*2];
        start -= blocks;
        memmove(to + (locs.pos[i*2] * BLOCKSIZE), from + (start * BLOCKSIZE),
                blocks * BLOCKSIZE);
    }
}

// Do a source/target load for move/bsdiff/imgdiff in version 2.
// We expect to parse the remainder of the parameter tokens as one of:
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

static int LoadSrcTgtVersion2(CommandParameters& params, RangeSet& tgt, size_t& src_blocks,
        std::vector<uint8_t>& buffer, int fd, const std::string& stashbase, bool* overlap) {

    // At least it needs to provide three parameters: <tgt_range>,
    // <src_block_count> and "-"/<src_range>.
    if (params.cpos + 2 >= params.tokens.size()) {
        fprintf(stderr, "invalid parameters\n");
        return -1;
    }

    // <tgt_range>
    parse_range(params.tokens[params.cpos++], tgt);

    // <src_block_count>
    const std::string& token = params.tokens[params.cpos++];
    if (!android::base::ParseUint(token.c_str(), &src_blocks)) {
        fprintf(stderr, "invalid src_block_count \"%s\"\n", token.c_str());
        return -1;
    }

    allocate(src_blocks * BLOCKSIZE, buffer);

    // "-" or <src_range> [<src_loc>]
    if (params.tokens[params.cpos] == "-") {
        // no source ranges, only stashes
        params.cpos++;
    } else {
        RangeSet src;
        parse_range(params.tokens[params.cpos++], src);
        int res = ReadBlocks(src, buffer, fd);

        if (overlap) {
            *overlap = range_overlaps(src, tgt);
        }

        if (res == -1) {
            return -1;
        }

        if (params.cpos >= params.tokens.size()) {
            // no stashes, only source range
            return 0;
        }

        RangeSet locs;
        parse_range(params.tokens[params.cpos++], locs);
        MoveRange(buffer, locs, buffer);
    }

    // <[stash_id:stash_range]>
    while (params.cpos < params.tokens.size()) {
        // Each word is a an index into the stash table, a colon, and
        // then a rangeset describing where in the source block that
        // stashed data should go.
        std::vector<std::string> tokens = android::base::Split(params.tokens[params.cpos++], ":");
        if (tokens.size() != 2) {
            fprintf(stderr, "invalid parameter\n");
            return -1;
        }

        std::vector<uint8_t> stash;
        int res = LoadStash(params, stashbase, tokens[0], false, nullptr, stash, true);

        if (res == -1) {
            // These source blocks will fail verification if used later, but we
            // will let the caller decide if this is a fatal failure
            fprintf(stderr, "failed to load stash %s\n", tokens[0].c_str());
            continue;
        }

        RangeSet locs;
        parse_range(tokens[1], locs);

        MoveRange(buffer, locs, stash);
    }

    return 0;
}

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

static int LoadSrcTgtVersion3(CommandParameters& params, RangeSet& tgt, size_t& src_blocks,
        bool onehash, bool& overlap) {

    if (params.cpos >= params.tokens.size()) {
        fprintf(stderr, "missing source hash\n");
        return -1;
    }

    std::string srchash = params.tokens[params.cpos++];
    std::string tgthash;

    if (onehash) {
        tgthash = srchash;
    } else {
        if (params.cpos >= params.tokens.size()) {
            fprintf(stderr, "missing target hash\n");
            return -1;
        }
        tgthash = params.tokens[params.cpos++];
    }

    if (LoadSrcTgtVersion2(params, tgt, src_blocks, params.buffer, params.fd, params.stashbase,
            &overlap) == -1) {
        return -1;
    }

    std::vector<uint8_t> tgtbuffer(tgt.size * BLOCKSIZE);

    if (ReadBlocks(tgt, tgtbuffer, params.fd) == -1) {
        return -1;
    }

    if (VerifyBlocks(tgthash, tgtbuffer, tgt.size, false) == 0) {
        // Target blocks already have expected content, command should be skipped
        return 1;
    }

    if (VerifyBlocks(srchash, params.buffer, src_blocks, true) == 0) {
        // If source and target blocks overlap, stash the source blocks so we can
        // resume from possible write errors. In verify mode, we can skip stashing
        // because the source blocks won't be overwritten.
        if (overlap && params.canwrite) {
            fprintf(stderr, "stashing %zu overlapping blocks to %s\n", src_blocks,
                    srchash.c_str());

            bool stash_exists = false;
            if (WriteStash(params.stashbase, srchash, src_blocks, params.buffer, true,
                           &stash_exists) != 0) {
                fprintf(stderr, "failed to stash overlapping source blocks\n");
                return -1;
            }

            params.stashed += src_blocks;
            // Can be deleted when the write has completed
            if (!stash_exists) {
                params.freestash = srchash;
            }
        }

        // Source blocks have expected content, command can proceed
        return 0;
    }

    if (overlap && LoadStash(params, params.stashbase, srchash, true, nullptr, params.buffer,
                             true) == 0) {
        // Overlapping source blocks were previously stashed, command can proceed.
        // We are recovering from an interrupted command, so we don't know if the
        // stash can safely be deleted after this command.
        return 0;
    }

    // Valid source data not available, update cannot be resumed
    fprintf(stderr, "partition has unexpected contents\n");
    params.isunresumable = true;

    return -1;
}

static int PerformCommandMove(CommandParameters& params) {
    size_t blocks = 0;
    bool overlap = false;
    int status = 0;
    RangeSet tgt;

    if (params.version == 1) {
        status = LoadSrcTgtVersion1(params, tgt, blocks, params.buffer, params.fd);
    } else if (params.version == 2) {
        status = LoadSrcTgtVersion2(params, tgt, blocks, params.buffer, params.fd,
                params.stashbase, nullptr);
    } else if (params.version >= 3) {
        status = LoadSrcTgtVersion3(params, tgt, blocks, true, overlap);
    }

    if (status == -1) {
        fprintf(stderr, "failed to read blocks for move\n");
        return -1;
    }

    if (status == 0) {
        params.foundwrites = true;
    } else if (params.foundwrites) {
        fprintf(stderr, "warning: commands executed out of order [%s]\n", params.cmdname);
    }

    if (params.canwrite) {
        if (status == 0) {
            fprintf(stderr, "  moving %zu blocks\n", blocks);

            if (WriteBlocks(tgt, params.buffer, params.fd) == -1) {
                return -1;
            }
        } else {
            fprintf(stderr, "skipping %zu already moved blocks\n", blocks);
        }

    }

    if (!params.freestash.empty()) {
        FreeStash(params.stashbase, params.freestash);
        params.freestash.clear();
    }

    params.written += tgt.size;

    return 0;
}

static int PerformCommandStash(CommandParameters& params) {
    return SaveStash(params, params.stashbase, params.buffer, params.fd,
            (params.version >= 3));
}

static int PerformCommandFree(CommandParameters& params) {
    // <stash_id>
    if (params.cpos >= params.tokens.size()) {
        fprintf(stderr, "missing stash id in free command\n");
        return -1;
    }

    const std::string& id = params.tokens[params.cpos++];

    if (!params.canwrite && stash_map.find(id) != stash_map.end()) {
        stash_map.erase(id);
        return 0;
    }

    if (params.createdstash || params.canwrite) {
        return FreeStash(params.stashbase, id);
    }

    return 0;
}

static int PerformCommandZero(CommandParameters& params) {

    if (params.cpos >= params.tokens.size()) {
        fprintf(stderr, "missing target blocks for zero\n");
        return -1;
    }

    RangeSet tgt;
    parse_range(params.tokens[params.cpos++], tgt);

    fprintf(stderr, "  zeroing %zu blocks\n", tgt.size);

    allocate(BLOCKSIZE, params.buffer);
    memset(params.buffer.data(), 0, BLOCKSIZE);

    if (params.canwrite) {
        for (size_t i = 0; i < tgt.count; ++i) {
            off64_t offset = static_cast<off64_t>(tgt.pos[i * 2]) * BLOCKSIZE;
            size_t size = (tgt.pos[i * 2 + 1] - tgt.pos[i * 2]) * BLOCKSIZE;
            if (!discard_blocks(params.fd, offset, size)) {
                return -1;
            }

            if (!check_lseek(params.fd, offset, SEEK_SET)) {
                return -1;
            }

            for (size_t j = tgt.pos[i * 2]; j < tgt.pos[i * 2 + 1]; ++j) {
                if (write_all(params.fd, params.buffer, BLOCKSIZE) == -1) {
                    return -1;
                }
            }
        }
    }

    if (params.cmdname[0] == 'z') {
        // Update only for the zero command, as the erase command will call
        // this if DEBUG_ERASE is defined.
        params.written += tgt.size;
    }

    return 0;
}

static int PerformCommandNew(CommandParameters& params) {

    if (params.cpos >= params.tokens.size()) {
        fprintf(stderr, "missing target blocks for new\n");
        return -1;
    }

    RangeSet tgt;
    parse_range(params.tokens[params.cpos++], tgt);

    if (params.canwrite) {
        fprintf(stderr, " writing %zu blocks of new data\n", tgt.size);

        RangeSinkState rss(tgt);
        rss.fd = params.fd;
        rss.p_block = 0;
        rss.p_remain = (tgt.pos[1] - tgt.pos[0]) * BLOCKSIZE;

        off64_t offset = static_cast<off64_t>(tgt.pos[0]) * BLOCKSIZE;
        if (!discard_blocks(params.fd, offset, tgt.size * BLOCKSIZE)) {
            return -1;
        }

        if (!check_lseek(params.fd, offset, SEEK_SET)) {
            return -1;
        }

        pthread_mutex_lock(&params.nti.mu);
        params.nti.rss = &rss;
        pthread_cond_broadcast(&params.nti.cv);

        while (params.nti.rss) {
            pthread_cond_wait(&params.nti.cv, &params.nti.mu);
        }

        pthread_mutex_unlock(&params.nti.mu);
    }

    params.written += tgt.size;

    return 0;
}

static int PerformCommandDiff(CommandParameters& params) {

    // <offset> <length>
    if (params.cpos + 1 >= params.tokens.size()) {
        fprintf(stderr, "missing patch offset or length for %s\n", params.cmdname);
        return -1;
    }

    size_t offset;
    if (!android::base::ParseUint(params.tokens[params.cpos++].c_str(), &offset)) {
        fprintf(stderr, "invalid patch offset\n");
        return -1;
    }

    size_t len;
    if (!android::base::ParseUint(params.tokens[params.cpos++].c_str(), &len)) {
        fprintf(stderr, "invalid patch offset\n");
        return -1;
    }

    RangeSet tgt;
    size_t blocks = 0;
    bool overlap = false;
    int status = 0;
    if (params.version == 1) {
        status = LoadSrcTgtVersion1(params, tgt, blocks, params.buffer, params.fd);
    } else if (params.version == 2) {
        status = LoadSrcTgtVersion2(params, tgt, blocks, params.buffer, params.fd,
                params.stashbase, nullptr);
    } else if (params.version >= 3) {
        status = LoadSrcTgtVersion3(params, tgt, blocks, false, overlap);
    }

    if (status == -1) {
        fprintf(stderr, "failed to read blocks for diff\n");
        return -1;
    }

    if (status == 0) {
        params.foundwrites = true;
    } else if (params.foundwrites) {
        fprintf(stderr, "warning: commands executed out of order [%s]\n", params.cmdname);
    }

    if (params.canwrite) {
        if (status == 0) {
            fprintf(stderr, "patching %zu blocks to %zu\n", blocks, tgt.size);

            Value patch_value;
            patch_value.type = VAL_BLOB;
            patch_value.size = len;
            patch_value.data = (char*) (params.patch_start + offset);

            RangeSinkState rss(tgt);
            rss.fd = params.fd;
            rss.p_block = 0;
            rss.p_remain = (tgt.pos[1] - tgt.pos[0]) * BLOCKSIZE;

            off64_t offset = static_cast<off64_t>(tgt.pos[0]) * BLOCKSIZE;
            if (!discard_blocks(params.fd, offset, rss.p_remain)) {
                return -1;
            }

            if (!check_lseek(params.fd, offset, SEEK_SET)) {
                return -1;
            }

            if (params.cmdname[0] == 'i') {      // imgdiff
                if (ApplyImagePatch(params.buffer.data(), blocks * BLOCKSIZE, &patch_value,
                        &RangeSinkWrite, &rss, nullptr, nullptr) != 0) {
                    fprintf(stderr, "Failed to apply image patch.\n");
                    return -1;
                }
            } else {
                if (ApplyBSDiffPatch(params.buffer.data(), blocks * BLOCKSIZE, &patch_value,
                        0, &RangeSinkWrite, &rss, nullptr) != 0) {
                    fprintf(stderr, "Failed to apply bsdiff patch.\n");
                    return -1;
                }
            }

            // We expect the output of the patcher to fill the tgt ranges exactly.
            if (rss.p_block != tgt.count || rss.p_remain != 0) {
                fprintf(stderr, "range sink underrun?\n");
            }
        } else {
            fprintf(stderr, "skipping %zu blocks already patched to %zu [%s]\n",
                blocks, tgt.size, params.cmdline);
        }
    }

    if (!params.freestash.empty()) {
        FreeStash(params.stashbase, params.freestash);
        params.freestash.clear();
    }

    params.written += tgt.size;

    return 0;
}

static int PerformCommandErase(CommandParameters& params) {
    if (DEBUG_ERASE) {
        return PerformCommandZero(params);
    }

    struct stat sb;
    if (fstat(params.fd, &sb) == -1) {
        fprintf(stderr, "failed to fstat device to erase: %s\n", strerror(errno));
        return -1;
    }

    if (!S_ISBLK(sb.st_mode)) {
        fprintf(stderr, "not a block device; skipping erase\n");
        return -1;
    }

    if (params.cpos >= params.tokens.size()) {
        fprintf(stderr, "missing target blocks for erase\n");
        return -1;
    }

    RangeSet tgt;
    parse_range(params.tokens[params.cpos++], tgt);

    if (params.canwrite) {
        fprintf(stderr, " erasing %zu blocks\n", tgt.size);

        for (size_t i = 0; i < tgt.count; ++i) {
            uint64_t blocks[2];
            // offset in bytes
            blocks[0] = tgt.pos[i * 2] * (uint64_t) BLOCKSIZE;
            // length in bytes
            blocks[1] = (tgt.pos[i * 2 + 1] - tgt.pos[i * 2]) * (uint64_t) BLOCKSIZE;

            if (ioctl(params.fd, BLKDISCARD, &blocks) == -1) {
                fprintf(stderr, "BLKDISCARD ioctl failed: %s\n", strerror(errno));
                return -1;
            }
        }
    }

    return 0;
}

// Definitions for transfer list command functions
typedef int (*CommandFunction)(CommandParameters&);

struct Command {
    const char* name;
    CommandFunction f;
};

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

static Value* PerformBlockImageUpdate(const char* name, State* state, int /* argc */, Expr* argv[],
        const Command* commands, size_t cmdcount, bool dryrun) {
    CommandParameters params;
    memset(&params, 0, sizeof(params));
    params.canwrite = !dryrun;

    fprintf(stderr, "performing %s\n", dryrun ? "verification" : "update");
    if (state->is_retry) {
        is_retry = true;
        fprintf(stderr, "This update is a retry.\n");
    }

    Value* blockdev_filename = nullptr;
    Value* transfer_list_value = nullptr;
    Value* new_data_fn = nullptr;
    Value* patch_data_fn = nullptr;
    if (ReadValueArgs(state, argv, 4, &blockdev_filename, &transfer_list_value,
            &new_data_fn, &patch_data_fn) < 0) {
        return StringValue(strdup(""));
    }
    std::unique_ptr<Value, decltype(&FreeValue)> blockdev_filename_holder(blockdev_filename,
            FreeValue);
    std::unique_ptr<Value, decltype(&FreeValue)> transfer_list_value_holder(transfer_list_value,
            FreeValue);
    std::unique_ptr<Value, decltype(&FreeValue)> new_data_fn_holder(new_data_fn, FreeValue);
    std::unique_ptr<Value, decltype(&FreeValue)> patch_data_fn_holder(patch_data_fn, FreeValue);

    if (blockdev_filename->type != VAL_STRING) {
        ErrorAbort(state, kArgsParsingFailure, "blockdev_filename argument to %s must be string",
                   name);
        return StringValue(strdup(""));
    }
    if (transfer_list_value->type != VAL_BLOB) {
        ErrorAbort(state, kArgsParsingFailure, "transfer_list argument to %s must be blob", name);
        return StringValue(strdup(""));
    }
    if (new_data_fn->type != VAL_STRING) {
        ErrorAbort(state, kArgsParsingFailure, "new_data_fn argument to %s must be string", name);
        return StringValue(strdup(""));
    }
    if (patch_data_fn->type != VAL_STRING) {
        ErrorAbort(state, kArgsParsingFailure, "patch_data_fn argument to %s must be string",
                   name);
        return StringValue(strdup(""));
    }

    UpdaterInfo* ui = reinterpret_cast<UpdaterInfo*>(state->cookie);

    if (ui == nullptr) {
        return StringValue(strdup(""));
    }

    FILE* cmd_pipe = ui->cmd_pipe;
    ZipArchive* za = ui->package_zip;

    if (cmd_pipe == nullptr || za == nullptr) {
        return StringValue(strdup(""));
    }

    const ZipEntry* patch_entry = mzFindZipEntry(za, patch_data_fn->data);
    if (patch_entry == nullptr) {
        fprintf(stderr, "%s(): no file \"%s\" in package", name, patch_data_fn->data);
        return StringValue(strdup(""));
    }

    params.patch_start = ui->package_zip_addr + mzGetZipEntryOffset(patch_entry);
    const ZipEntry* new_entry = mzFindZipEntry(za, new_data_fn->data);
    if (new_entry == nullptr) {
        fprintf(stderr, "%s(): no file \"%s\" in package", name, new_data_fn->data);
        return StringValue(strdup(""));
    }

    params.fd = TEMP_FAILURE_RETRY(open(blockdev_filename->data, O_RDWR));
    unique_fd fd_holder(params.fd);

    if (params.fd == -1) {
        fprintf(stderr, "open \"%s\" failed: %s\n", blockdev_filename->data, strerror(errno));
        return StringValue(strdup(""));
    }

    if (params.canwrite) {
        params.nti.za = za;
        params.nti.entry = new_entry;

        pthread_mutex_init(&params.nti.mu, nullptr);
        pthread_cond_init(&params.nti.cv, nullptr);
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

        int error = pthread_create(&params.thread, &attr, unzip_new_data, &params.nti);
        if (error != 0) {
            fprintf(stderr, "pthread_create failed: %s\n", strerror(error));
            return StringValue(strdup(""));
        }
    }

    // Copy all the lines in transfer_list_value into std::string for
    // processing.
    const std::string transfer_list(transfer_list_value->data, transfer_list_value->size);
    std::vector<std::string> lines = android::base::Split(transfer_list, "\n");
    if (lines.size() < 2) {
        ErrorAbort(state, kArgsParsingFailure, "too few lines in the transfer list [%zd]\n",
                   lines.size());
        return StringValue(strdup(""));
    }

    // First line in transfer list is the version number
    if (!android::base::ParseInt(lines[0].c_str(), &params.version, 1, 4)) {
        fprintf(stderr, "unexpected transfer list version [%s]\n", lines[0].c_str());
        return StringValue(strdup(""));
    }

    fprintf(stderr, "blockimg version is %d\n", params.version);

    // Second line in transfer list is the total number of blocks we expect to write
    int total_blocks;
    if (!android::base::ParseInt(lines[1].c_str(), &total_blocks, 0)) {
        ErrorAbort(state, kArgsParsingFailure, "unexpected block count [%s]\n", lines[1].c_str());
        return StringValue(strdup(""));
    }

    if (total_blocks == 0) {
        return StringValue(strdup("t"));
    }

    size_t start = 2;
    if (params.version >= 2) {
        if (lines.size() < 4) {
            ErrorAbort(state, kArgsParsingFailure, "too few lines in the transfer list [%zu]\n",
                       lines.size());
            return StringValue(strdup(""));
        }

        // Third line is how many stash entries are needed simultaneously
        fprintf(stderr, "maximum stash entries %s\n", lines[2].c_str());

        // Fourth line is the maximum number of blocks that will be stashed simultaneously
        int stash_max_blocks;
        if (!android::base::ParseInt(lines[3].c_str(), &stash_max_blocks, 0)) {
            ErrorAbort(state, kArgsParsingFailure, "unexpected maximum stash blocks [%s]\n",
                       lines[3].c_str());
            return StringValue(strdup(""));
        }

        int res = CreateStash(state, stash_max_blocks, blockdev_filename->data, params.stashbase);
        if (res == -1) {
            return StringValue(strdup(""));
        }

        params.createdstash = res;

        start += 2;
    }

    // Build a hash table of the available commands
    HashTable* cmdht = mzHashTableCreate(cmdcount, nullptr);
    std::unique_ptr<HashTable, decltype(&mzHashTableFree)> cmdht_holder(cmdht, mzHashTableFree);

    for (size_t i = 0; i < cmdcount; ++i) {
        unsigned int cmdhash = HashString(commands[i].name);
        mzHashTableLookup(cmdht, cmdhash, (void*) &commands[i], CompareCommands, true);
    }

    int rc = -1;

    // Subsequent lines are all individual transfer commands
    for (auto it = lines.cbegin() + start; it != lines.cend(); it++) {
        const std::string& line_str(*it);
        if (line_str.empty()) {
            continue;
        }

        params.tokens = android::base::Split(line_str, " ");
        params.cpos = 0;
        params.cmdname = params.tokens[params.cpos++].c_str();
        params.cmdline = line_str.c_str();

        unsigned int cmdhash = HashString(params.cmdname);
        const Command* cmd = reinterpret_cast<const Command*>(mzHashTableLookup(cmdht, cmdhash,
                const_cast<char*>(params.cmdname), CompareCommandNames,
                false));

        if (cmd == nullptr) {
            fprintf(stderr, "unexpected command [%s]\n", params.cmdname);
            goto pbiudone;
        }

        if (cmd->f != nullptr && cmd->f(params) == -1) {
            fprintf(stderr, "failed to execute command [%s]\n", line_str.c_str());
            goto pbiudone;
        }

        if (params.canwrite) {
            if (ota_fsync(params.fd) == -1) {
                failure_type = kFsyncFailure;
                fprintf(stderr, "fsync failed: %s\n", strerror(errno));
                goto pbiudone;
            }
            fprintf(cmd_pipe, "set_progress %.4f\n", (double) params.written / total_blocks);
            fflush(cmd_pipe);
        }
    }

    if (params.canwrite) {
        pthread_join(params.thread, nullptr);

        fprintf(stderr, "wrote %zu blocks; expected %d\n", params.written, total_blocks);
        fprintf(stderr, "stashed %zu blocks\n", params.stashed);
        fprintf(stderr, "max alloc needed was %zu\n", params.buffer.size());

        const char* partition = strrchr(blockdev_filename->data, '/');
        if (partition != nullptr && *(partition+1) != 0) {
            fprintf(cmd_pipe, "log bytes_written_%s: %zu\n", partition + 1,
                    params.written * BLOCKSIZE);
            fprintf(cmd_pipe, "log bytes_stashed_%s: %zu\n", partition + 1,
                    params.stashed * BLOCKSIZE);
            fflush(cmd_pipe);
        }
        // Delete stash only after successfully completing the update, as it
        // may contain blocks needed to complete the update later.
        DeleteStash(params.stashbase);
    } else {
        fprintf(stderr, "verified partition contents; update may be resumed\n");
    }

    rc = 0;

pbiudone:
    if (ota_fsync(params.fd) == -1) {
        failure_type = kFsyncFailure;
        fprintf(stderr, "fsync failed: %s\n", strerror(errno));
    }
    // params.fd will be automatically closed because of the fd_holder above.

    // Only delete the stash if the update cannot be resumed, or it's
    // a verification run and we created the stash.
    if (params.isunresumable || (!params.canwrite && params.createdstash)) {
        DeleteStash(params.stashbase);
    }

    if (failure_type != kNoCause && state->cause_code == kNoCause) {
        state->cause_code = failure_type;
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
//    free <stash_id>
//      - (version 3+ only) free the given stash data.
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
    // Commands which are not tested are set to nullptr to skip them completely
    const Command commands[] = {
        { "bsdiff",     PerformCommandDiff  },
        { "erase",      nullptr             },
        { "free",       PerformCommandFree  },
        { "imgdiff",    PerformCommandDiff  },
        { "move",       PerformCommandMove  },
        { "new",        nullptr             },
        { "stash",      PerformCommandStash },
        { "zero",       nullptr             }
    };

    // Perform a dry run without writing to test if an update can proceed
    return PerformBlockImageUpdate(name, state, argc, argv, commands,
                sizeof(commands) / sizeof(commands[0]), true);
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
                sizeof(commands) / sizeof(commands[0]), false);
}

Value* RangeSha1Fn(const char* name, State* state, int /* argc */, Expr* argv[]) {
    Value* blockdev_filename;
    Value* ranges;

    if (ReadValueArgs(state, argv, 2, &blockdev_filename, &ranges) < 0) {
        return StringValue(strdup(""));
    }
    std::unique_ptr<Value, decltype(&FreeValue)> ranges_holder(ranges, FreeValue);
    std::unique_ptr<Value, decltype(&FreeValue)> blockdev_filename_holder(blockdev_filename,
            FreeValue);

    if (blockdev_filename->type != VAL_STRING) {
        ErrorAbort(state, kArgsParsingFailure, "blockdev_filename argument to %s must be string",
                   name);
        return StringValue(strdup(""));
    }
    if (ranges->type != VAL_STRING) {
        ErrorAbort(state, kArgsParsingFailure, "ranges argument to %s must be string", name);
        return StringValue(strdup(""));
    }

    int fd = open(blockdev_filename->data, O_RDWR);
    unique_fd fd_holder(fd);
    if (fd < 0) {
        ErrorAbort(state, kFileOpenFailure, "open \"%s\" failed: %s", blockdev_filename->data,
                   strerror(errno));
        return StringValue(strdup(""));
    }

    RangeSet rs;
    parse_range(ranges->data, rs);

    SHA_CTX ctx;
    SHA1_Init(&ctx);

    std::vector<uint8_t> buffer(BLOCKSIZE);
    for (size_t i = 0; i < rs.count; ++i) {
        if (!check_lseek(fd, (off64_t)rs.pos[i*2] * BLOCKSIZE, SEEK_SET)) {
            ErrorAbort(state, kLseekFailure, "failed to seek %s: %s", blockdev_filename->data,
                       strerror(errno));
            return StringValue(strdup(""));
        }

        for (size_t j = rs.pos[i*2]; j < rs.pos[i*2+1]; ++j) {
            if (read_all(fd, buffer, BLOCKSIZE) == -1) {
                ErrorAbort(state, kFreadFailure, "failed to read %s: %s", blockdev_filename->data,
                        strerror(errno));
                return StringValue(strdup(""));
            }

            SHA1_Update(&ctx, buffer.data(), BLOCKSIZE);
        }
    }
    uint8_t digest[SHA_DIGEST_LENGTH];
    SHA1_Final(digest, &ctx);

    return StringValue(strdup(print_sha1(digest).c_str()));
}

// This function checks if a device has been remounted R/W prior to an incremental
// OTA update. This is an common cause of update abortion. The function reads the
// 1st block of each partition and check for mounting time/count. It return string "t"
// if executes successfully and an empty string otherwise.

Value* CheckFirstBlockFn(const char* name, State* state, int argc, Expr* argv[]) {
    Value* arg_filename;

    if (ReadValueArgs(state, argv, 1, &arg_filename) < 0) {
        return nullptr;
    }
    std::unique_ptr<Value, decltype(&FreeValue)> filename(arg_filename, FreeValue);

    if (filename->type != VAL_STRING) {
        ErrorAbort(state, kArgsParsingFailure, "filename argument to %s must be string", name);
        return StringValue(strdup(""));
    }

    int fd = open(arg_filename->data, O_RDONLY);
    unique_fd fd_holder(fd);
    if (fd == -1) {
        ErrorAbort(state, kFileOpenFailure, "open \"%s\" failed: %s", arg_filename->data,
                   strerror(errno));
        return StringValue(strdup(""));
    }

    RangeSet blk0 {1 /*count*/, 1/*size*/, std::vector<size_t> {0, 1}/*position*/};
    std::vector<uint8_t> block0_buffer(BLOCKSIZE);

    if (ReadBlocks(blk0, block0_buffer, fd) == -1) {
        ErrorAbort(state, kFreadFailure, "failed to read %s: %s", arg_filename->data,
                strerror(errno));
        return StringValue(strdup(""));
    }

    // https://ext4.wiki.kernel.org/index.php/Ext4_Disk_Layout
    // Super block starts from block 0, offset 0x400
    //   0x2C: len32 Mount time
    //   0x30: len32 Write time
    //   0x34: len16 Number of mounts since the last fsck
    //   0x38: len16 Magic signature 0xEF53

    time_t mount_time = *reinterpret_cast<uint32_t*>(&block0_buffer[0x400+0x2C]);
    uint16_t mount_count = *reinterpret_cast<uint16_t*>(&block0_buffer[0x400+0x34]);

    if (mount_count > 0) {
        uiPrintf(state, "Device was remounted R/W %d times\n", mount_count);
        uiPrintf(state, "Last remount happened on %s", ctime(&mount_time));
    }

    return StringValue(strdup("t"));
}


Value* BlockImageRecoverFn(const char* name, State* state, int argc, Expr* argv[]) {
    Value* arg_filename;
    Value* arg_ranges;

    if (ReadValueArgs(state, argv, 2, &arg_filename, &arg_ranges) < 0) {
        return NULL;
    }

    std::unique_ptr<Value, decltype(&FreeValue)> filename(arg_filename, FreeValue);
    std::unique_ptr<Value, decltype(&FreeValue)> ranges(arg_ranges, FreeValue);

    if (filename->type != VAL_STRING) {
        ErrorAbort(state, kArgsParsingFailure, "filename argument to %s must be string", name);
        return StringValue(strdup(""));
    }
    if (ranges->type != VAL_STRING) {
        ErrorAbort(state, kArgsParsingFailure, "ranges argument to %s must be string", name);
        return StringValue(strdup(""));
    }

    // Output notice to log when recover is attempted
    fprintf(stderr, "%s image corrupted, attempting to recover...\n", filename->data);

    // When opened with O_RDWR, libfec rewrites corrupted blocks when they are read
    fec::io fh(filename->data, O_RDWR);

    if (!fh) {
        ErrorAbort(state, kLibfecFailure, "fec_open \"%s\" failed: %s", filename->data,
                   strerror(errno));
        return StringValue(strdup(""));
    }

    if (!fh.has_ecc() || !fh.has_verity()) {
        ErrorAbort(state, kLibfecFailure, "unable to use metadata to correct errors");
        return StringValue(strdup(""));
    }

    fec_status status;

    if (!fh.get_status(status)) {
        ErrorAbort(state, kLibfecFailure, "failed to read FEC status");
        return StringValue(strdup(""));
    }

    RangeSet rs;
    parse_range(ranges->data, rs);

    uint8_t buffer[BLOCKSIZE];

    for (size_t i = 0; i < rs.count; ++i) {
        for (size_t j = rs.pos[i * 2]; j < rs.pos[i * 2 + 1]; ++j) {
            // Stay within the data area, libfec validates and corrects metadata
            if (status.data_size <= (uint64_t)j * BLOCKSIZE) {
                continue;
            }

            if (fh.pread(buffer, BLOCKSIZE, (off64_t)j * BLOCKSIZE) != BLOCKSIZE) {
                ErrorAbort(state, kLibfecFailure, "failed to recover %s (block %zu): %s",
                           filename->data, j, strerror(errno));
                return StringValue(strdup(""));
            }

            // If we want to be able to recover from a situation where rewriting a corrected
            // block doesn't guarantee the same data will be returned when re-read later, we
            // can save a copy of corrected blocks to /cache. Note:
            //
            //  1. Maximum space required from /cache is the same as the maximum number of
            //     corrupted blocks we can correct. For RS(255, 253) and a 2 GiB partition,
            //     this would be ~16 MiB, for example.
            //
            //  2. To find out if this block was corrupted, call fec_get_status after each
            //     read and check if the errors field value has increased.
        }
    }
    fprintf(stderr, "...%s image recovered successfully.\n", filename->data);
    return StringValue(strdup("t"));
}

void RegisterBlockImageFunctions() {
    RegisterFunction("block_image_verify", BlockImageVerifyFn);
    RegisterFunction("block_image_update", BlockImageUpdateFn);
    RegisterFunction("block_image_recover", BlockImageRecoverFn);
    RegisterFunction("check_first_block", CheckFirstBlockFn);
    RegisterFunction("range_sha1", RangeSha1Fn);
}
