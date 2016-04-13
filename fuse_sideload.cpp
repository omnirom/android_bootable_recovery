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

// This module creates a special filesystem containing two files.
//
// "/sideload/package.zip" appears to be a normal file, but reading
// from it causes data to be fetched from the adb host.  We can use
// this to sideload packages over an adb connection without having to
// store the entire package in RAM on the device.
//
// Because we may not trust the adb host, this filesystem maintains
// the following invariant: each read of a given position returns the
// same data as the first read at that position.  That is, once a
// section of the file is read, future reads of that section return
// the same data.  (Otherwise, a malicious adb host process could
// return one set of bits when the package is read for signature
// verification, and then different bits for when the package is
// accessed by the installer.)  If the adb host returns something
// different than it did on the first read, the reader of the file
// will see their read fail with EINVAL.
//
// The other file, "/sideload/exit", is used to control the subprocess
// that creates this filesystem.  Calling stat() on the exit file
// causes the filesystem to be unmounted and the adb process on the
// device shut down.
//
// Note that only the minimal set of file operations needed for these
// two files is implemented.  In particular, you can't opendir() or
// readdir() on the "/sideload" directory; ls on it won't work.

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/fuse.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <unistd.h>

#include <openssl/sha.h>

#include "fuse_sideload.h"

#define PACKAGE_FILE_ID   (FUSE_ROOT_ID+1)
#define EXIT_FLAG_ID      (FUSE_ROOT_ID+2)

#define NO_STATUS         1
#define NO_STATUS_EXIT    2

struct fuse_data {
    int ffd;   // file descriptor for the fuse socket

    struct provider_vtab* vtab;
    void* cookie;

    uint64_t file_size;     // bytes

    uint32_t block_size;    // block size that the adb host is using to send the file to us
    uint32_t file_blocks;   // file size in block_size blocks

    uid_t uid;
    gid_t gid;

    uint32_t curr_block;    // cache the block most recently read from the host
    uint8_t* block_data;

    uint8_t* extra_block;   // another block of storage for reads that
                            // span two blocks

    uint8_t* hashes;        // SHA-256 hash of each block (all zeros
                            // if block hasn't been read yet)
};

static void fuse_reply(struct fuse_data* fd, __u64 unique, const void *data, size_t len)
{
    struct fuse_out_header hdr;
    struct iovec vec[2];
    int res;

    hdr.len = len + sizeof(hdr);
    hdr.error = 0;
    hdr.unique = unique;

    vec[0].iov_base = &hdr;
    vec[0].iov_len = sizeof(hdr);
    vec[1].iov_base = /* const_cast */(void*)(data);
    vec[1].iov_len = len;

    res = writev(fd->ffd, vec, 2);
    if (res < 0) {
        printf("*** REPLY FAILED *** %s\n", strerror(errno));
    }
}

static int handle_init(void* data, struct fuse_data* fd, const struct fuse_in_header* hdr) {
    const struct fuse_init_in* req = reinterpret_cast<const struct fuse_init_in*>(data);
    struct fuse_init_out out;
    size_t fuse_struct_size;


    /* Kernel 2.6.16 is the first stable kernel with struct fuse_init_out
     * defined (fuse version 7.6). The structure is the same from 7.6 through
     * 7.22. Beginning with 7.23, the structure increased in size and added
     * new parameters.
     */
    if (req->major != FUSE_KERNEL_VERSION || req->minor < 6) {
        printf("Fuse kernel version mismatch: Kernel version %d.%d, Expected at least %d.6",
               req->major, req->minor, FUSE_KERNEL_VERSION);
        return -1;
    }

    out.minor = MIN(req->minor, FUSE_KERNEL_MINOR_VERSION);
    fuse_struct_size = sizeof(out);
#if defined(FUSE_COMPAT_22_INIT_OUT_SIZE)
    /* FUSE_KERNEL_VERSION >= 23. */

    /* If the kernel only works on minor revs older than or equal to 22,
     * then use the older structure size since this code only uses the 7.22
     * version of the structure. */
    if (req->minor <= 22) {
        fuse_struct_size = FUSE_COMPAT_22_INIT_OUT_SIZE;
    }
#endif

    out.major = FUSE_KERNEL_VERSION;
    out.max_readahead = req->max_readahead;
    out.flags = 0;
    out.max_background = 32;
    out.congestion_threshold = 32;
    out.max_write = 4096;
    fuse_reply(fd, hdr->unique, &out, fuse_struct_size);

    return NO_STATUS;
}

static void fill_attr(struct fuse_attr* attr, struct fuse_data* fd,
                      uint64_t nodeid, uint64_t size, uint32_t mode) {
    memset(attr, 0, sizeof(*attr));
    attr->nlink = 1;
    attr->uid = fd->uid;
    attr->gid = fd->gid;
    attr->blksize = 4096;

    attr->ino = nodeid;
    attr->size = size;
    attr->blocks = (size == 0) ? 0 : (((size-1) / attr->blksize) + 1);
    attr->mode = mode;
}

static int handle_getattr(void* /* data */, struct fuse_data* fd, const struct fuse_in_header* hdr) {
    struct fuse_attr_out out;
    memset(&out, 0, sizeof(out));
    out.attr_valid = 10;

    if (hdr->nodeid == FUSE_ROOT_ID) {
        fill_attr(&(out.attr), fd, hdr->nodeid, 4096, S_IFDIR | 0555);
    } else if (hdr->nodeid == PACKAGE_FILE_ID) {
        fill_attr(&(out.attr), fd, PACKAGE_FILE_ID, fd->file_size, S_IFREG | 0444);
    } else if (hdr->nodeid == EXIT_FLAG_ID) {
        fill_attr(&(out.attr), fd, EXIT_FLAG_ID, 0, S_IFREG | 0);
    } else {
        return -ENOENT;
    }

    fuse_reply(fd, hdr->unique, &out, sizeof(out));
    return (hdr->nodeid == EXIT_FLAG_ID) ? NO_STATUS_EXIT : NO_STATUS;
}

static int handle_lookup(void* data, struct fuse_data* fd,
                         const struct fuse_in_header* hdr) {
    struct fuse_entry_out out;
    memset(&out, 0, sizeof(out));
    out.entry_valid = 10;
    out.attr_valid = 10;

    if (strncmp(FUSE_SIDELOAD_HOST_FILENAME, reinterpret_cast<const char*>(data),
                sizeof(FUSE_SIDELOAD_HOST_FILENAME)) == 0) {
        out.nodeid = PACKAGE_FILE_ID;
        out.generation = PACKAGE_FILE_ID;
        fill_attr(&(out.attr), fd, PACKAGE_FILE_ID, fd->file_size, S_IFREG | 0444);
    } else if (strncmp(FUSE_SIDELOAD_HOST_EXIT_FLAG, reinterpret_cast<const char*>(data),
                       sizeof(FUSE_SIDELOAD_HOST_EXIT_FLAG)) == 0) {
        out.nodeid = EXIT_FLAG_ID;
        out.generation = EXIT_FLAG_ID;
        fill_attr(&(out.attr), fd, EXIT_FLAG_ID, 0, S_IFREG | 0);
    } else {
        return -ENOENT;
    }

    fuse_reply(fd, hdr->unique, &out, sizeof(out));
    return (out.nodeid == EXIT_FLAG_ID) ? NO_STATUS_EXIT : NO_STATUS;
}

static int handle_open(void* /* data */, struct fuse_data* fd, const struct fuse_in_header* hdr) {
    if (hdr->nodeid == EXIT_FLAG_ID) return -EPERM;
    if (hdr->nodeid != PACKAGE_FILE_ID) return -ENOENT;

    struct fuse_open_out out;
    memset(&out, 0, sizeof(out));
    out.fh = 10;  // an arbitrary number; we always use the same handle
    fuse_reply(fd, hdr->unique, &out, sizeof(out));
    return NO_STATUS;
}

static int handle_flush(void* data, struct fuse_data* fd, const struct fuse_in_header* hdr) {
    return 0;
}

static int handle_release(void* data, struct fuse_data* fd, const struct fuse_in_header* hdr) {
    return 0;
}

// Fetch a block from the host into fd->curr_block and fd->block_data.
// Returns 0 on successful fetch, negative otherwise.
static int fetch_block(struct fuse_data* fd, uint32_t block) {
    if (block == fd->curr_block) {
        return 0;
    }

    if (block >= fd->file_blocks) {
        memset(fd->block_data, 0, fd->block_size);
        fd->curr_block = block;
        return 0;
    }

    size_t fetch_size = fd->block_size;
    if (block * fd->block_size + fetch_size > fd->file_size) {
        // If we're reading the last (partial) block of the file,
        // expect a shorter response from the host, and pad the rest
        // of the block with zeroes.
        fetch_size = fd->file_size - (block * fd->block_size);
        memset(fd->block_data + fetch_size, 0, fd->block_size - fetch_size);
    }

    int result = fd->vtab->read_block(fd->cookie, block, fd->block_data, fetch_size);
    if (result < 0) return result;

    fd->curr_block = block;

    // Verify the hash of the block we just got from the host.
    //
    // - If the hash of the just-received data matches the stored hash
    //   for the block, accept it.
    // - If the stored hash is all zeroes, store the new hash and
    //   accept the block (this is the first time we've read this
    //   block).
    // - Otherwise, return -EINVAL for the read.

    uint8_t hash[SHA256_DIGEST_LENGTH];
    SHA256(fd->block_data, fd->block_size, hash);
    uint8_t* blockhash = fd->hashes + block * SHA256_DIGEST_LENGTH;
    if (memcmp(hash, blockhash, SHA256_DIGEST_LENGTH) == 0) {
        return 0;
    }

    int i;
    for (i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        if (blockhash[i] != 0) {
            fd->curr_block = -1;
            return -EIO;
        }
    }

    memcpy(blockhash, hash, SHA256_DIGEST_LENGTH);
    return 0;
}

static int handle_read(void* data, struct fuse_data* fd, const struct fuse_in_header* hdr) {
    const struct fuse_read_in* req = reinterpret_cast<const struct fuse_read_in*>(data);
    struct fuse_out_header outhdr;
    struct iovec vec[3];
    int vec_used;
    int result;

    if (hdr->nodeid != PACKAGE_FILE_ID) return -ENOENT;

    uint64_t offset = req->offset;
    uint32_t size = req->size;

    // The docs on the fuse kernel interface are vague about what to
    // do when a read request extends past the end of the file.  We
    // can return a short read -- the return structure does include a
    // length field -- but in testing that caused the program using
    // the file to segfault.  (I speculate that this is due to the
    // reading program accessing it via mmap; maybe mmap dislikes when
    // you return something short of a whole page?)  To fix this we
    // zero-pad reads that extend past the end of the file so we're
    // always returning exactly as many bytes as were requested.
    // (Users of the mapped file have to know its real length anyway.)

    outhdr.len = sizeof(outhdr) + size;
    outhdr.error = 0;
    outhdr.unique = hdr->unique;
    vec[0].iov_base = &outhdr;
    vec[0].iov_len = sizeof(outhdr);

    uint32_t block = offset / fd->block_size;
    result = fetch_block(fd, block);
    if (result != 0) return result;

    // Two cases:
    //
    //   - the read request is entirely within this block.  In this
    //     case we can reply immediately.
    //
    //   - the read request goes over into the next block.  Note that
    //     since we mount the filesystem with max_read=block_size, a
    //     read can never span more than two blocks.  In this case we
    //     copy the block to extra_block and issue a fetch for the
    //     following block.

    uint32_t block_offset = offset - (block * fd->block_size);

    if (size + block_offset <= fd->block_size) {
        // First case: the read fits entirely in the first block.

        vec[1].iov_base = fd->block_data + block_offset;
        vec[1].iov_len = size;
        vec_used = 2;
    } else {
        // Second case: the read spills over into the next block.

        memcpy(fd->extra_block, fd->block_data + block_offset,
               fd->block_size - block_offset);
        vec[1].iov_base = fd->extra_block;
        vec[1].iov_len = fd->block_size - block_offset;

        result = fetch_block(fd, block+1);
        if (result != 0) return result;
        vec[2].iov_base = fd->block_data;
        vec[2].iov_len = size - vec[1].iov_len;
        vec_used = 3;
    }

    if (writev(fd->ffd, vec, vec_used) < 0) {
        printf("*** READ REPLY FAILED: %s ***\n", strerror(errno));
    }
    return NO_STATUS;
}

int run_fuse_sideload(struct provider_vtab* vtab, void* cookie,
                      uint64_t file_size, uint32_t block_size)
{
    int result;

    // If something's already mounted on our mountpoint, try to remove
    // it.  (Mostly in case of a previous abnormal exit.)
    umount2(FUSE_SIDELOAD_HOST_MOUNTPOINT, MNT_FORCE);

    if (block_size < 1024) {
        fprintf(stderr, "block size (%u) is too small\n", block_size);
        return -1;
    }
    if (block_size > (1<<22)) {   // 4 MiB
        fprintf(stderr, "block size (%u) is too large\n", block_size);
        return -1;
    }

    struct fuse_data fd;
    memset(&fd, 0, sizeof(fd));
    fd.vtab = vtab;
    fd.cookie = cookie;
    fd.file_size = file_size;
    fd.block_size = block_size;
    fd.file_blocks = (file_size == 0) ? 0 : (((file_size-1) / block_size) + 1);

    if (fd.file_blocks > (1<<18)) {
        fprintf(stderr, "file has too many blocks (%u)\n", fd.file_blocks);
        result = -1;
        goto done;
    }

    fd.hashes = (uint8_t*)calloc(fd.file_blocks, SHA256_DIGEST_LENGTH);
    if (fd.hashes == NULL) {
        fprintf(stderr, "failed to allocate %d bites for hashes\n",
                fd.file_blocks * SHA256_DIGEST_LENGTH);
        result = -1;
        goto done;
    }

    fd.uid = getuid();
    fd.gid = getgid();

    fd.curr_block = -1;
    fd.block_data = (uint8_t*)malloc(block_size);
    if (fd.block_data == NULL) {
        fprintf(stderr, "failed to allocate %d bites for block_data\n", block_size);
        result = -1;
        goto done;
    }
    fd.extra_block = (uint8_t*)malloc(block_size);
    if (fd.extra_block == NULL) {
        fprintf(stderr, "failed to allocate %d bites for extra_block\n", block_size);
        result = -1;
        goto done;
    }

    fd.ffd = open("/dev/fuse", O_RDWR);
    if (fd.ffd < 0) {
        perror("open /dev/fuse");
        result = -1;
        goto done;
    }

    char opts[256];
    snprintf(opts, sizeof(opts),
             ("fd=%d,user_id=%d,group_id=%d,max_read=%u,"
              "allow_other,rootmode=040000"),
             fd.ffd, fd.uid, fd.gid, block_size);

    result = mount("/dev/fuse", FUSE_SIDELOAD_HOST_MOUNTPOINT,
                   "fuse", MS_NOSUID | MS_NODEV | MS_RDONLY | MS_NOEXEC, opts);
    if (result < 0) {
        perror("mount");
        goto done;
    }
    uint8_t request_buffer[sizeof(struct fuse_in_header) + PATH_MAX*8];
    for (;;) {
        ssize_t len = TEMP_FAILURE_RETRY(read(fd.ffd, request_buffer, sizeof(request_buffer)));
        if (len == -1) {
            perror("read request");
            if (errno == ENODEV) {
                result = -1;
                break;
            }
            continue;
        }

        if ((size_t)len < sizeof(struct fuse_in_header)) {
            fprintf(stderr, "request too short: len=%zu\n", (size_t)len);
            continue;
        }

        struct fuse_in_header* hdr = (struct fuse_in_header*) request_buffer;
        void* data = request_buffer + sizeof(struct fuse_in_header);

        result = -ENOSYS;

        switch (hdr->opcode) {
             case FUSE_INIT:
                result = handle_init(data, &fd, hdr);
                break;

             case FUSE_LOOKUP:
                result = handle_lookup(data, &fd, hdr);
                break;

            case FUSE_GETATTR:
                result = handle_getattr(data, &fd, hdr);
                break;

            case FUSE_OPEN:
                result = handle_open(data, &fd, hdr);
                break;

            case FUSE_READ:
                result = handle_read(data, &fd, hdr);
                break;

            case FUSE_FLUSH:
                result = handle_flush(data, &fd, hdr);
                break;

            case FUSE_RELEASE:
                result = handle_release(data, &fd, hdr);
                break;

            default:
                fprintf(stderr, "unknown fuse request opcode %d\n", hdr->opcode);
                break;
        }

        if (result == NO_STATUS_EXIT) {
            result = 0;
            break;
        }

        if (result != NO_STATUS) {
            struct fuse_out_header outhdr;
            outhdr.len = sizeof(outhdr);
            outhdr.error = result;
            outhdr.unique = hdr->unique;
            TEMP_FAILURE_RETRY(write(fd.ffd, &outhdr, sizeof(outhdr)));
        }
    }

  done:
    fd.vtab->close(fd.cookie);

    result = umount2(FUSE_SIDELOAD_HOST_MOUNTPOINT, MNT_DETACH);
    if (result < 0) {
        printf("fuse_sideload umount failed: %s\n", strerror(errno));
    }

    if (fd.ffd) close(fd.ffd);
    free(fd.hashes);
    free(fd.block_data);
    free(fd.extra_block);

    return result;
}
