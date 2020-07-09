/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include "EncryptInplace.h"

#include <ext4_utils/ext4.h>
#include <ext4_utils/ext4_utils.h>
#include <f2fs_sparseblock.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include <algorithm>

#include <android-base/logging.h>
#include <android-base/properties.h>

// HORRIBLE HACK, FIXME
#include "cryptfs.h"

// FIXME horrible cut-and-paste code
static inline int unix_read(int fd, void* buff, int len) {
    return TEMP_FAILURE_RETRY(read(fd, buff, len));
}

static inline int unix_write(int fd, const void* buff, int len) {
    return TEMP_FAILURE_RETRY(write(fd, buff, len));
}

#define CRYPT_SECTORS_PER_BUFSIZE (CRYPT_INPLACE_BUFSIZE / CRYPT_SECTOR_SIZE)

/* aligned 32K writes tends to make flash happy.
 * SD card association recommends it.
 */
#ifndef CONFIG_HW_DISK_ENCRYPTION
#define BLOCKS_AT_A_TIME 8
#else
#define BLOCKS_AT_A_TIME 1024
#endif

struct encryptGroupsData {
    int realfd;
    int cryptofd;
    off64_t numblocks;
    off64_t one_pct, cur_pct, new_pct;
    off64_t blocks_already_done, tot_numblocks;
    off64_t used_blocks_already_done, tot_used_blocks;
    const char* real_blkdev;
    const char* crypto_blkdev;
    int count;
    off64_t offset;
    char* buffer;
    off64_t last_written_sector;
    int completed;
    time_t time_started;
    int remaining_time;
    bool set_progress_properties;
};

static void update_progress(struct encryptGroupsData* data, int is_used) {
    data->blocks_already_done++;

    if (is_used) {
        data->used_blocks_already_done++;
    }
    if (data->tot_used_blocks) {
        data->new_pct = data->used_blocks_already_done / data->one_pct;
    } else {
        data->new_pct = data->blocks_already_done / data->one_pct;
    }

    if (!data->set_progress_properties) return;

    if (data->new_pct > data->cur_pct) {
        char buf[8];
        data->cur_pct = data->new_pct;
        snprintf(buf, sizeof(buf), "%" PRId64, data->cur_pct);
        android::base::SetProperty("vold.encrypt_progress", buf);
    }

    if (data->cur_pct >= 5) {
        struct timespec time_now;
        if (clock_gettime(CLOCK_MONOTONIC, &time_now)) {
            LOG(WARNING) << "Error getting time";
        } else {
            double elapsed_time = difftime(time_now.tv_sec, data->time_started);
            off64_t remaining_blocks = data->tot_used_blocks - data->used_blocks_already_done;
            int remaining_time =
                (int)(elapsed_time * remaining_blocks / data->used_blocks_already_done);

            // Change time only if not yet set, lower, or a lot higher for
            // best user experience
            if (data->remaining_time == -1 || remaining_time < data->remaining_time ||
                remaining_time > data->remaining_time + 60) {
                char buf[8];
                snprintf(buf, sizeof(buf), "%d", remaining_time);
                android::base::SetProperty("vold.encrypt_time_remaining", buf);
                data->remaining_time = remaining_time;
            }
        }
    }
}

static void log_progress(struct encryptGroupsData const* data, bool completed) {
    // Precondition - if completed data = 0 else data != 0

    // Track progress so we can skip logging blocks
    static off64_t offset = -1;

    // Need to close existing 'Encrypting from' log?
    if (completed || (offset != -1 && data->offset != offset)) {
        LOG(INFO) << "Encrypted to sector " << offset / info.block_size * CRYPT_SECTOR_SIZE;
        offset = -1;
    }

    // Need to start new 'Encrypting from' log?
    if (!completed && offset != data->offset) {
        LOG(INFO) << "Encrypting from sector " << data->offset / info.block_size * CRYPT_SECTOR_SIZE;
    }

    // Update offset
    if (!completed) {
        offset = data->offset + (off64_t)data->count * info.block_size;
    }
}

static int flush_outstanding_data(struct encryptGroupsData* data) {
    if (data->count == 0) {
        return 0;
    }

    LOG(DEBUG) << "Copying " << data->count << " blocks at offset " << data->offset;

    if (pread64(data->realfd, data->buffer, info.block_size * data->count, data->offset) <= 0) {
        LOG(ERROR) << "Error reading real_blkdev " << data->real_blkdev << " for inplace encrypt";
        return -1;
    }

    if (pwrite64(data->cryptofd, data->buffer, info.block_size * data->count, data->offset) <= 0) {
        LOG(ERROR) << "Error writing crypto_blkdev " << data->crypto_blkdev
                   << " for inplace encrypt";
        return -1;
    } else {
        log_progress(data, false);
    }

    data->count = 0;
    data->last_written_sector =
        (data->offset + data->count) / info.block_size * CRYPT_SECTOR_SIZE - 1;
    return 0;
}

static int encrypt_groups(struct encryptGroupsData* data) {
    unsigned int i;
    u8* block_bitmap = 0;
    unsigned int block;
    off64_t ret;
    int rc = -1;

    data->buffer = (char*)malloc(info.block_size * BLOCKS_AT_A_TIME);
    if (!data->buffer) {
        LOG(ERROR) << "Failed to allocate crypto buffer";
        goto errout;
    }

    block_bitmap = (u8*)malloc(info.block_size);
    if (!block_bitmap) {
        LOG(ERROR) << "failed to allocate block bitmap";
        goto errout;
    }

    for (i = 0; i < aux_info.groups; ++i) {
        LOG(INFO) << "Encrypting group " << i;

        u32 first_block = aux_info.first_data_block + i * info.blocks_per_group;
        u32 block_count = std::min(info.blocks_per_group, (u32)(aux_info.len_blocks - first_block));

        off64_t offset = (u64)info.block_size * aux_info.bg_desc[i].bg_block_bitmap;

        ret = pread64(data->realfd, block_bitmap, info.block_size, offset);
        if (ret != (int)info.block_size) {
            LOG(ERROR) << "failed to read all of block group bitmap " << i;
            goto errout;
        }

        offset = (u64)info.block_size * first_block;

        data->count = 0;

        for (block = 0; block < block_count; block++) {
            int used = (aux_info.bg_desc[i].bg_flags & EXT4_BG_BLOCK_UNINIT)
                           ? 0
                           : bitmap_get_bit(block_bitmap, block);
            update_progress(data, used);
            if (used) {
                if (data->count == 0) {
                    data->offset = offset;
                }
                data->count++;
            } else {
                if (flush_outstanding_data(data)) {
                    goto errout;
                }
            }

            offset += info.block_size;

            /* Write data if we are aligned or buffer size reached */
            if (offset % (info.block_size * BLOCKS_AT_A_TIME) == 0 ||
                data->count == BLOCKS_AT_A_TIME) {
                if (flush_outstanding_data(data)) {
                    goto errout;
                }
            }
        }
        if (flush_outstanding_data(data)) {
            goto errout;
        }
    }

    data->completed = 1;
    rc = 0;

errout:
    log_progress(0, true);
    free(data->buffer);
    free(block_bitmap);
    return rc;
}

static int cryptfs_enable_inplace_ext4(const char* crypto_blkdev, const char* real_blkdev,
                                       off64_t size, off64_t* size_already_done, off64_t tot_size,
                                       off64_t previously_encrypted_upto,
                                       bool set_progress_properties) {
    u32 i;
    struct encryptGroupsData data;
    int rc;  // Can't initialize without causing warning -Wclobbered
    int retries = RETRY_MOUNT_ATTEMPTS;
    struct timespec time_started = {0};

    if (previously_encrypted_upto > *size_already_done) {
        LOG(DEBUG) << "Not fast encrypting since resuming part way through";
        return -1;
    }

    memset(&data, 0, sizeof(data));
    data.real_blkdev = real_blkdev;
    data.crypto_blkdev = crypto_blkdev;
    data.set_progress_properties = set_progress_properties;

    LOG(DEBUG) << "Opening" << real_blkdev;
    if ((data.realfd = open(real_blkdev, O_RDWR | O_CLOEXEC)) < 0) {
        PLOG(ERROR) << "Error opening real_blkdev " << real_blkdev << " for inplace encrypt";
        rc = -1;
        goto errout;
    }

    LOG(DEBUG) << "Opening" << crypto_blkdev;
    // Wait until the block device appears.  Re-use the mount retry values since it is reasonable.
    while ((data.cryptofd = open(crypto_blkdev, O_WRONLY | O_CLOEXEC)) < 0) {
        if (--retries) {
            PLOG(ERROR) << "Error opening crypto_blkdev " << crypto_blkdev
                        << " for ext4 inplace encrypt, retrying";
            sleep(RETRY_MOUNT_DELAY_SECONDS);
        } else {
            PLOG(ERROR) << "Error opening crypto_blkdev " << crypto_blkdev
                        << " for ext4 inplace encrypt";
            rc = ENABLE_INPLACE_ERR_DEV;
            goto errout;
        }
    }

    if (setjmp(setjmp_env)) {  // NOLINT
        LOG(ERROR) << "Reading ext4 extent caused an exception";
        rc = -1;
        goto errout;
    }

    if (read_ext(data.realfd, 0) != 0) {
        LOG(ERROR) << "Failed to read ext4 extent";
        rc = -1;
        goto errout;
    }

    data.numblocks = size / CRYPT_SECTORS_PER_BUFSIZE;
    data.tot_numblocks = tot_size / CRYPT_SECTORS_PER_BUFSIZE;
    data.blocks_already_done = *size_already_done / CRYPT_SECTORS_PER_BUFSIZE;

    LOG(INFO) << "Encrypting ext4 filesystem in place...";

    data.tot_used_blocks = data.numblocks;
    for (i = 0; i < aux_info.groups; ++i) {
        data.tot_used_blocks -= aux_info.bg_desc[i].bg_free_blocks_count;
    }

    data.one_pct = data.tot_used_blocks / 100;
    data.cur_pct = 0;

    if (clock_gettime(CLOCK_MONOTONIC, &time_started)) {
        LOG(WARNING) << "Error getting time at start";
        // Note - continue anyway - we'll run with 0
    }
    data.time_started = time_started.tv_sec;
    data.remaining_time = -1;

    rc = encrypt_groups(&data);
    if (rc) {
        LOG(ERROR) << "Error encrypting groups";
        goto errout;
    }

    *size_already_done += data.completed ? size : data.last_written_sector;
    rc = 0;

errout:
    close(data.realfd);
    close(data.cryptofd);

    return rc;
}

static void log_progress_f2fs(u64 block, bool completed) {
    // Precondition - if completed data = 0 else data != 0

    // Track progress so we can skip logging blocks
    static u64 last_block = (u64)-1;

    // Need to close existing 'Encrypting from' log?
    if (completed || (last_block != (u64)-1 && block != last_block + 1)) {
        LOG(INFO) << "Encrypted to block " << last_block;
        last_block = -1;
    }

    // Need to start new 'Encrypting from' log?
    if (!completed && (last_block == (u64)-1 || block != last_block + 1)) {
        LOG(INFO) << "Encrypting from block " << block;
    }

    // Update offset
    if (!completed) {
        last_block = block;
    }
}

static int encrypt_one_block_f2fs(u64 pos, void* data) {
    struct encryptGroupsData* priv_dat = (struct encryptGroupsData*)data;

    priv_dat->blocks_already_done = pos - 1;
    update_progress(priv_dat, 1);

    off64_t offset = pos * CRYPT_INPLACE_BUFSIZE;

    if (pread64(priv_dat->realfd, priv_dat->buffer, CRYPT_INPLACE_BUFSIZE, offset) <= 0) {
        LOG(ERROR) << "Error reading real_blkdev " << priv_dat->crypto_blkdev
                   << " for f2fs inplace encrypt";
        return -1;
    }

    if (pwrite64(priv_dat->cryptofd, priv_dat->buffer, CRYPT_INPLACE_BUFSIZE, offset) <= 0) {
        LOG(ERROR) << "Error writing crypto_blkdev " << priv_dat->crypto_blkdev
                   << " for f2fs inplace encrypt";
        return -1;
    } else {
        log_progress_f2fs(pos, false);
    }

    return 0;
}

static int cryptfs_enable_inplace_f2fs(const char* crypto_blkdev, const char* real_blkdev,
                                       off64_t size, off64_t* size_already_done, off64_t tot_size,
                                       off64_t previously_encrypted_upto,
                                       bool set_progress_properties) {
    struct encryptGroupsData data;
    struct f2fs_info* f2fs_info = NULL;
    int rc = ENABLE_INPLACE_ERR_OTHER;
    if (previously_encrypted_upto > *size_already_done) {
        LOG(DEBUG) << "Not fast encrypting since resuming part way through";
        return ENABLE_INPLACE_ERR_OTHER;
    }
    memset(&data, 0, sizeof(data));
    data.real_blkdev = real_blkdev;
    data.crypto_blkdev = crypto_blkdev;
    data.set_progress_properties = set_progress_properties;
    data.realfd = -1;
    data.cryptofd = -1;
    if ((data.realfd = open64(real_blkdev, O_RDWR | O_CLOEXEC)) < 0) {
        PLOG(ERROR) << "Error opening real_blkdev " << real_blkdev << " for f2fs inplace encrypt";
        goto errout;
    }
    if ((data.cryptofd = open64(crypto_blkdev, O_WRONLY | O_CLOEXEC)) < 0) {
        PLOG(ERROR) << "Error opening crypto_blkdev " << crypto_blkdev
                    << " for f2fs inplace encrypt";
        rc = ENABLE_INPLACE_ERR_DEV;
        goto errout;
    }

    f2fs_info = generate_f2fs_info(data.realfd);
    if (!f2fs_info) goto errout;

    data.numblocks = size / CRYPT_SECTORS_PER_BUFSIZE;
    data.tot_numblocks = tot_size / CRYPT_SECTORS_PER_BUFSIZE;
    data.blocks_already_done = *size_already_done / CRYPT_SECTORS_PER_BUFSIZE;

    data.tot_used_blocks = get_num_blocks_used(f2fs_info);

    data.one_pct = data.tot_used_blocks / 100;
    data.cur_pct = 0;
    data.time_started = time(NULL);
    data.remaining_time = -1;

    data.buffer = (char*)malloc(f2fs_info->block_size);
    if (!data.buffer) {
        LOG(ERROR) << "Failed to allocate crypto buffer";
        goto errout;
    }

    data.count = 0;

    /* Currently, this either runs to completion, or hits a nonrecoverable error */
    rc = run_on_used_blocks(data.blocks_already_done, f2fs_info, &encrypt_one_block_f2fs, &data);

    if (rc) {
        LOG(ERROR) << "Error in running over f2fs blocks";
        rc = ENABLE_INPLACE_ERR_OTHER;
        goto errout;
    }

    *size_already_done += size;
    rc = 0;

errout:
    if (rc) LOG(ERROR) << "Failed to encrypt f2fs filesystem on " << real_blkdev;

    log_progress_f2fs(0, true);
    free(f2fs_info);
    free(data.buffer);
    close(data.realfd);
    close(data.cryptofd);

    return rc;
}

static int cryptfs_enable_inplace_full(const char* crypto_blkdev, const char* real_blkdev,
                                       off64_t size, off64_t* size_already_done, off64_t tot_size,
                                       off64_t previously_encrypted_upto,
                                       bool set_progress_properties) {
    int realfd, cryptofd;
    char* buf[CRYPT_INPLACE_BUFSIZE];
    int rc = ENABLE_INPLACE_ERR_OTHER;
    off64_t numblocks, i, remainder;
    off64_t one_pct, cur_pct, new_pct;
    off64_t blocks_already_done, tot_numblocks;

    if ((realfd = open(real_blkdev, O_RDONLY | O_CLOEXEC)) < 0) {
        PLOG(ERROR) << "Error opening real_blkdev " << real_blkdev << " for inplace encrypt";
        return ENABLE_INPLACE_ERR_OTHER;
    }

    if ((cryptofd = open(crypto_blkdev, O_WRONLY | O_CLOEXEC)) < 0) {
        PLOG(ERROR) << "Error opening crypto_blkdev " << crypto_blkdev << " for inplace encrypt";
        close(realfd);
        return ENABLE_INPLACE_ERR_DEV;
    }

    /* This is pretty much a simple loop of reading 4K, and writing 4K.
     * The size passed in is the number of 512 byte sectors in the filesystem.
     * So compute the number of whole 4K blocks we should read/write,
     * and the remainder.
     */
    numblocks = size / CRYPT_SECTORS_PER_BUFSIZE;
    remainder = size % CRYPT_SECTORS_PER_BUFSIZE;
    tot_numblocks = tot_size / CRYPT_SECTORS_PER_BUFSIZE;
    blocks_already_done = *size_already_done / CRYPT_SECTORS_PER_BUFSIZE;

    LOG(ERROR) << "Encrypting filesystem in place...";

    i = previously_encrypted_upto + 1 - *size_already_done;

    if (lseek64(realfd, i * CRYPT_SECTOR_SIZE, SEEK_SET) < 0) {
        PLOG(ERROR) << "Cannot seek to previously encrypted point on " << real_blkdev;
        goto errout;
    }

    if (lseek64(cryptofd, i * CRYPT_SECTOR_SIZE, SEEK_SET) < 0) {
        PLOG(ERROR) << "Cannot seek to previously encrypted point on " << crypto_blkdev;
        goto errout;
    }

    for (; i < size && i % CRYPT_SECTORS_PER_BUFSIZE != 0; ++i) {
        if (unix_read(realfd, buf, CRYPT_SECTOR_SIZE) <= 0) {
            PLOG(ERROR) << "Error reading initial sectors from real_blkdev " << real_blkdev
                        << " for inplace encrypt";
            goto errout;
        }
        if (unix_write(cryptofd, buf, CRYPT_SECTOR_SIZE) <= 0) {
            PLOG(ERROR) << "Error writing initial sectors to crypto_blkdev " << crypto_blkdev
                        << " for inplace encrypt";
            goto errout;
        } else {
            LOG(INFO) << "Encrypted 1 block at " << i;
        }
    }

    one_pct = tot_numblocks / 100;
    cur_pct = 0;
    /* process the majority of the filesystem in blocks */
    for (i /= CRYPT_SECTORS_PER_BUFSIZE; i < numblocks; i++) {
        new_pct = (i + blocks_already_done) / one_pct;
        if (set_progress_properties && new_pct > cur_pct) {
            char property_buf[8];

            cur_pct = new_pct;
            snprintf(property_buf, sizeof(property_buf), "%" PRId64, cur_pct);
            android::base::SetProperty("vold.encrypt_progress", property_buf);
        }
        if (unix_read(realfd, buf, CRYPT_INPLACE_BUFSIZE) <= 0) {
            PLOG(ERROR) << "Error reading real_blkdev " << real_blkdev << " for inplace encrypt";
            goto errout;
        }
        if (unix_write(cryptofd, buf, CRYPT_INPLACE_BUFSIZE) <= 0) {
            PLOG(ERROR) << "Error writing crypto_blkdev " << crypto_blkdev << " for inplace encrypt";
            goto errout;
        } else {
            LOG(DEBUG) << "Encrypted " << CRYPT_SECTORS_PER_BUFSIZE << " block at "
                       << i * CRYPT_SECTORS_PER_BUFSIZE;
        }
    }

    /* Do any remaining sectors */
    for (i = 0; i < remainder; i++) {
        if (unix_read(realfd, buf, CRYPT_SECTOR_SIZE) <= 0) {
            LOG(ERROR) << "Error reading final sectors from real_blkdev " << real_blkdev
                       << " for inplace encrypt";
            goto errout;
        }
        if (unix_write(cryptofd, buf, CRYPT_SECTOR_SIZE) <= 0) {
            LOG(ERROR) << "Error writing final sectors to crypto_blkdev " << crypto_blkdev
                       << " for inplace encrypt";
            goto errout;
        } else {
            LOG(INFO) << "Encrypted 1 block at next location";
        }
    }

    *size_already_done += size;
    rc = 0;

errout:
    close(realfd);
    close(cryptofd);

    return rc;
}

/* returns on of the ENABLE_INPLACE_* return codes */
int cryptfs_enable_inplace(const char* crypto_blkdev, const char* real_blkdev, off64_t size,
                           off64_t* size_already_done, off64_t tot_size,
                           off64_t previously_encrypted_upto, bool set_progress_properties) {
    int rc_ext4, rc_f2fs, rc_full;
    LOG(DEBUG) << "cryptfs_enable_inplace(" << crypto_blkdev << ", " << real_blkdev << ", " << size
               << ", " << size_already_done << ", " << tot_size << ", " << previously_encrypted_upto
               << ", " << set_progress_properties << ")";
    if (previously_encrypted_upto) {
        LOG(DEBUG) << "Continuing encryption from " << previously_encrypted_upto;
    }

    if (*size_already_done + size < previously_encrypted_upto) {
        LOG(DEBUG) << "cryptfs_enable_inplace already done";
        *size_already_done += size;
        return 0;
    }

    /* TODO: identify filesystem type.
     * As is, cryptfs_enable_inplace_ext4 will fail on an f2fs partition, and
     * then we will drop down to cryptfs_enable_inplace_f2fs.
     * */
    if ((rc_ext4 = cryptfs_enable_inplace_ext4(crypto_blkdev, real_blkdev, size, size_already_done,
                                               tot_size, previously_encrypted_upto,
                                               set_progress_properties)) == 0) {
        LOG(DEBUG) << "cryptfs_enable_inplace_ext4 success";
        return 0;
    }
    LOG(DEBUG) << "cryptfs_enable_inplace_ext4()=" << rc_ext4;

    if ((rc_f2fs = cryptfs_enable_inplace_f2fs(crypto_blkdev, real_blkdev, size, size_already_done,
                                               tot_size, previously_encrypted_upto,
                                               set_progress_properties)) == 0) {
        LOG(DEBUG) << "cryptfs_enable_inplace_f2fs success";
        return 0;
    }
    LOG(DEBUG) << "cryptfs_enable_inplace_f2fs()=" << rc_f2fs;

    rc_full =
        cryptfs_enable_inplace_full(crypto_blkdev, real_blkdev, size, size_already_done, tot_size,
                                    previously_encrypted_upto, set_progress_properties);
    LOG(DEBUG) << "cryptfs_enable_inplace_full()=" << rc_full;

    /* Hack for b/17898962, the following is the symptom... */
    if (rc_ext4 == ENABLE_INPLACE_ERR_DEV && rc_f2fs == ENABLE_INPLACE_ERR_DEV &&
        rc_full == ENABLE_INPLACE_ERR_DEV) {
        LOG(DEBUG) << "ENABLE_INPLACE_ERR_DEV";
        return ENABLE_INPLACE_ERR_DEV;
    }
    return rc_full;
}
