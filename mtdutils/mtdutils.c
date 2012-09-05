/*
 * Copyright (C) 2007 The Android Open Source Project
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mount.h>  // for _IOW, _IOR, mount()
#include <sys/stat.h>
#include <mtd/mtd-user.h>
#undef NDEBUG
#include <assert.h>

#include "mtdutils.h"

struct MtdReadContext {
    const MtdPartition *partition;
    char *buffer;
    size_t consumed;
    int fd;
};

struct MtdWriteContext {
    const MtdPartition *partition;
    char *buffer;
    size_t stored;
    int fd;

    off_t* bad_block_offsets;
    int bad_block_alloc;
    int bad_block_count;
};

typedef struct {
    MtdPartition *partitions;
    int partitions_allocd;
    int partition_count;
} MtdState;

static MtdState g_mtd_state = {
    NULL,   // partitions
    0,      // partitions_allocd
    -1      // partition_count
};

#define MTD_PROC_FILENAME   "/proc/mtd"

int
mtd_scan_partitions()
{
    char buf[2048];
    const char *bufp;
    int fd;
    int i;
    ssize_t nbytes;

    if (g_mtd_state.partitions == NULL) {
        const int nump = 32;
        MtdPartition *partitions = malloc(nump * sizeof(*partitions));
        if (partitions == NULL) {
            errno = ENOMEM;
            return -1;
        }
        g_mtd_state.partitions = partitions;
        g_mtd_state.partitions_allocd = nump;
        memset(partitions, 0, nump * sizeof(*partitions));
    }
    g_mtd_state.partition_count = 0;

    /* Initialize all of the entries to make things easier later.
     * (Lets us handle sparsely-numbered partitions, which
     * may not even be possible.)
     */
    for (i = 0; i < g_mtd_state.partitions_allocd; i++) {
        MtdPartition *p = &g_mtd_state.partitions[i];
        if (p->name != NULL) {
            free(p->name);
            p->name = NULL;
        }
        p->device_index = -1;
    }

    /* Open and read the file contents.
     */
    fd = open(MTD_PROC_FILENAME, O_RDONLY);
    if (fd < 0) {
        goto bail;
    }
    nbytes = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (nbytes < 0) {
        goto bail;
    }
    buf[nbytes] = '\0';

    /* Parse the contents of the file, which looks like:
     *
     *     # cat /proc/mtd
     *     dev:    size   erasesize  name
     *     mtd0: 00080000 00020000 "bootloader"
     *     mtd1: 00400000 00020000 "mfg_and_gsm"
     *     mtd2: 00400000 00020000 "0000000c"
     *     mtd3: 00200000 00020000 "0000000d"
     *     mtd4: 04000000 00020000 "system"
     *     mtd5: 03280000 00020000 "userdata"
     */
    bufp = buf;
    while (nbytes > 0) {
        int mtdnum, mtdsize, mtderasesize;
        int matches;
        char mtdname[64];
        mtdname[0] = '\0';
        mtdnum = -1;

        matches = sscanf(bufp, "mtd%d: %x %x \"%63[^\"]",
                &mtdnum, &mtdsize, &mtderasesize, mtdname);
        /* This will fail on the first line, which just contains
         * column headers.
         */
        if (matches == 4) {
            MtdPartition *p = &g_mtd_state.partitions[mtdnum];
            p->device_index = mtdnum;
            p->size = mtdsize;
            p->erase_size = mtderasesize;
            p->name = strdup(mtdname);
            if (p->name == NULL) {
                errno = ENOMEM;
                goto bail;
            }
            g_mtd_state.partition_count++;
        }

        /* Eat the line.
         */
        while (nbytes > 0 && *bufp != '\n') {
            bufp++;
            nbytes--;
        }
        if (nbytes > 0) {
            bufp++;
            nbytes--;
        }
    }

    return g_mtd_state.partition_count;

bail:
    // keep "partitions" around so we can free the names on a rescan.
    g_mtd_state.partition_count = -1;
    return -1;
}

const MtdPartition *
mtd_find_partition_by_name(const char *name)
{
    if (g_mtd_state.partitions != NULL) {
        int i;
        for (i = 0; i < g_mtd_state.partitions_allocd; i++) {
            MtdPartition *p = &g_mtd_state.partitions[i];
            if (p->device_index >= 0 && p->name != NULL) {
                if (strcmp(p->name, name) == 0) {
                    return p;
                }
            }
        }
    }
    return NULL;
}

int
mtd_mount_partition(const MtdPartition *partition, const char *mount_point,
        const char *filesystem, int read_only)
{
    const unsigned long flags = MS_NOATIME | MS_NODEV | MS_NODIRATIME;
    char devname[64];
    int rv = -1;

    sprintf(devname, "/dev/block/mtdblock%d", partition->device_index);
    if (!read_only) {
        rv = mount(devname, mount_point, filesystem, flags, NULL);
    }
    if (read_only || rv < 0) {
        rv = mount(devname, mount_point, filesystem, flags | MS_RDONLY, 0);
        if (rv < 0) {
            printf("Failed to mount %s on %s: %s\n",
                    devname, mount_point, strerror(errno));
        } else {
            printf("Mount %s on %s read-only\n", devname, mount_point);
        }
    }
#if 1   //TODO: figure out why this is happening; remove include of stat.h
    if (rv >= 0) {
        /* For some reason, the x bits sometimes aren't set on the root
         * of mounted volumes.
         */
        struct stat st;
        rv = stat(mount_point, &st);
        if (rv < 0) {
            return rv;
        }
        mode_t new_mode = st.st_mode | S_IXUSR | S_IXGRP | S_IXOTH;
        if (new_mode != st.st_mode) {
printf("Fixing execute permissions for %s\n", mount_point);
            rv = chmod(mount_point, new_mode);
            if (rv < 0) {
                printf("Couldn't fix permissions for %s: %s\n",
                        mount_point, strerror(errno));
            }
        }
    }
#endif
    return rv;
}

int
mtd_partition_info(const MtdPartition *partition,
        size_t *total_size, size_t *erase_size, size_t *write_size)
{
    char mtddevname[32];
    sprintf(mtddevname, "/dev/mtd/mtd%d", partition->device_index);
    int fd = open(mtddevname, O_RDONLY);
    if (fd < 0) return -1;

    struct mtd_info_user mtd_info;
    int ret = ioctl(fd, MEMGETINFO, &mtd_info);
    close(fd);
    if (ret < 0) return -1;

    if (total_size != NULL) *total_size = mtd_info.size;
    if (erase_size != NULL) *erase_size = mtd_info.erasesize;
    if (write_size != NULL) *write_size = mtd_info.writesize;
    return 0;
}

MtdReadContext *mtd_read_partition(const MtdPartition *partition)
{
    MtdReadContext *ctx = (MtdReadContext*) malloc(sizeof(MtdReadContext));
    if (ctx == NULL) return NULL;

    ctx->buffer = malloc(partition->erase_size);
    if (ctx->buffer == NULL) {
        free(ctx);
        return NULL;
    }

    char mtddevname[32];
    sprintf(mtddevname, "/dev/mtd/mtd%d", partition->device_index);
    ctx->fd = open(mtddevname, O_RDONLY);
    if (ctx->fd < 0) {
        free(ctx->buffer);
        free(ctx);
        return NULL;
    }

    ctx->partition = partition;
    ctx->consumed = partition->erase_size;
    return ctx;
}

// Seeks to a location in the partition.  Don't mix with reads of
// anything other than whole blocks; unpredictable things will result.
void mtd_read_skip_to(const MtdReadContext* ctx, size_t offset) {
    lseek64(ctx->fd, offset, SEEK_SET);
}

static int read_block(const MtdPartition *partition, int fd, char *data)
{
    struct mtd_ecc_stats before, after;
    if (ioctl(fd, ECCGETSTATS, &before)) {
        fprintf(stderr, "mtd: ECCGETSTATS error (%s)\n", strerror(errno));
        return -1;
    }

    loff_t pos = lseek64(fd, 0, SEEK_CUR);

    ssize_t size = partition->erase_size;
    int mgbb;

    while (pos + size <= (int) partition->size) {
        if (lseek64(fd, pos, SEEK_SET) != pos || read(fd, data, size) != size) {
            fprintf(stderr, "mtd: read error at 0x%08llx (%s)\n",
                    pos, strerror(errno));
        } else if (ioctl(fd, ECCGETSTATS, &after)) {
            fprintf(stderr, "mtd: ECCGETSTATS error (%s)\n", strerror(errno));
            return -1;
        } else if (after.failed != before.failed) {
            fprintf(stderr, "mtd: ECC errors (%d soft, %d hard) at 0x%08llx\n",
                    after.corrected - before.corrected,
                    after.failed - before.failed, pos);
            // copy the comparison baseline for the next read.
            memcpy(&before, &after, sizeof(struct mtd_ecc_stats));
        } else if ((mgbb = ioctl(fd, MEMGETBADBLOCK, &pos))) {
            fprintf(stderr,
                    "mtd: MEMGETBADBLOCK returned %d at 0x%08llx (errno=%d)\n",
                    mgbb, pos, errno);
        } else {
            return 0;  // Success!
        }

        pos += partition->erase_size;
    }

    errno = ENOSPC;
    return -1;
}

ssize_t mtd_read_data(MtdReadContext *ctx, char *data, size_t len)
{
    ssize_t read = 0;
    while (read < (int) len) {
        if (ctx->consumed < ctx->partition->erase_size) {
            size_t avail = ctx->partition->erase_size - ctx->consumed;
            size_t copy = len - read < avail ? len - read : avail;
            memcpy(data + read, ctx->buffer + ctx->consumed, copy);
            ctx->consumed += copy;
            read += copy;
        }

        // Read complete blocks directly into the user's buffer
        while (ctx->consumed == ctx->partition->erase_size &&
               len - read >= ctx->partition->erase_size) {
            if (read_block(ctx->partition, ctx->fd, data + read)) return -1;
            read += ctx->partition->erase_size;
        }

        if (read >= (int)len) {
            return read;
        }

        // Read the next block into the buffer
        if (ctx->consumed == ctx->partition->erase_size && read < (int) len) {
            if (read_block(ctx->partition, ctx->fd, ctx->buffer)) return -1;
            ctx->consumed = 0;
        }
    }

    return read;
}

void mtd_read_close(MtdReadContext *ctx)
{
    close(ctx->fd);
    free(ctx->buffer);
    free(ctx);
}

MtdWriteContext *mtd_write_partition(const MtdPartition *partition)
{
    MtdWriteContext *ctx = (MtdWriteContext*) malloc(sizeof(MtdWriteContext));
    if (ctx == NULL) return NULL;

    ctx->bad_block_offsets = NULL;
    ctx->bad_block_alloc = 0;
    ctx->bad_block_count = 0;

    ctx->buffer = malloc(partition->erase_size);
    if (ctx->buffer == NULL) {
        free(ctx);
        return NULL;
    }

    char mtddevname[32];
    sprintf(mtddevname, "/dev/mtd/mtd%d", partition->device_index);
    ctx->fd = open(mtddevname, O_RDWR);
    if (ctx->fd < 0) {
        free(ctx->buffer);
        free(ctx);
        return NULL;
    }

    ctx->partition = partition;
    ctx->stored = 0;
    return ctx;
}

static void add_bad_block_offset(MtdWriteContext *ctx, off_t pos) {
    if (ctx->bad_block_count + 1 > ctx->bad_block_alloc) {
        ctx->bad_block_alloc = (ctx->bad_block_alloc*2) + 1;
        ctx->bad_block_offsets = realloc(ctx->bad_block_offsets,
                                         ctx->bad_block_alloc * sizeof(off_t));
    }
    ctx->bad_block_offsets[ctx->bad_block_count++] = pos;
}

static int write_block(MtdWriteContext *ctx, const char *data)
{
    const MtdPartition *partition = ctx->partition;
    int fd = ctx->fd;

    off_t pos = lseek(fd, 0, SEEK_CUR);
    if (pos == (off_t) -1) return 1;

    ssize_t size = partition->erase_size;
    while (pos + size <= (int) partition->size) {
        loff_t bpos = pos;
        int ret = ioctl(fd, MEMGETBADBLOCK, &bpos);
        if (ret != 0 && !(ret == -1 && errno == EOPNOTSUPP)) {
            add_bad_block_offset(ctx, pos);
            fprintf(stderr,
                    "mtd: not writing bad block at 0x%08lx (ret %d errno %d)\n",
                    pos, ret, errno);
            pos += partition->erase_size;
            continue;  // Don't try to erase known factory-bad blocks.
        }

        struct erase_info_user erase_info;
        erase_info.start = pos;
        erase_info.length = size;
        int retry;
        for (retry = 0; retry < 2; ++retry) {
            if (ioctl(fd, MEMERASE, &erase_info) < 0) {
                fprintf(stderr, "mtd: erase failure at 0x%08lx (%s)\n",
                        pos, strerror(errno));
                continue;
            }
            if (lseek(fd, pos, SEEK_SET) != pos ||
                write(fd, data, size) != size) {
                fprintf(stderr, "mtd: write error at 0x%08lx (%s)\n",
                        pos, strerror(errno));
            }

            char verify[size];
            if (lseek(fd, pos, SEEK_SET) != pos ||
                read(fd, verify, size) != size) {
                fprintf(stderr, "mtd: re-read error at 0x%08lx (%s)\n",
                        pos, strerror(errno));
                continue;
            }
            if (memcmp(data, verify, size) != 0) {
                fprintf(stderr, "mtd: verification error at 0x%08lx (%s)\n",
                        pos, strerror(errno));
                continue;
            }

            if (retry > 0) {
                fprintf(stderr, "mtd: wrote block after %d retries\n", retry);
            }
            fprintf(stderr, "mtd: successfully wrote block at %llx\n", pos);
            return 0;  // Success!
        }

        // Try to erase it once more as we give up on this block
        add_bad_block_offset(ctx, pos);
        fprintf(stderr, "mtd: skipping write block at 0x%08lx\n", pos);
        ioctl(fd, MEMERASE, &erase_info);
        pos += partition->erase_size;
    }

    // Ran out of space on the device
    errno = ENOSPC;
    return -1;
}

ssize_t mtd_write_data(MtdWriteContext *ctx, const char *data, size_t len)
{
    size_t wrote = 0;
    while (wrote < len) {
        // Coalesce partial writes into complete blocks
        if (ctx->stored > 0 || len - wrote < ctx->partition->erase_size) {
            size_t avail = ctx->partition->erase_size - ctx->stored;
            size_t copy = len - wrote < avail ? len - wrote : avail;
            memcpy(ctx->buffer + ctx->stored, data + wrote, copy);
            ctx->stored += copy;
            wrote += copy;
        }

        // If a complete block was accumulated, write it
        if (ctx->stored == ctx->partition->erase_size) {
            if (write_block(ctx, ctx->buffer)) return -1;
            ctx->stored = 0;
        }

        // Write complete blocks directly from the user's buffer
        while (ctx->stored == 0 && len - wrote >= ctx->partition->erase_size) {
            if (write_block(ctx, data + wrote)) return -1;
            wrote += ctx->partition->erase_size;
        }
    }

    return wrote;
}

off_t mtd_erase_blocks(MtdWriteContext *ctx, int blocks)
{
    // Zero-pad and write any pending data to get us to a block boundary
    if (ctx->stored > 0) {
        size_t zero = ctx->partition->erase_size - ctx->stored;
        memset(ctx->buffer + ctx->stored, 0, zero);
        if (write_block(ctx, ctx->buffer)) return -1;
        ctx->stored = 0;
    }

    off_t pos = lseek(ctx->fd, 0, SEEK_CUR);
    if ((off_t) pos == (off_t) -1) return pos;

    const int total = (ctx->partition->size - pos) / ctx->partition->erase_size;
    if (blocks < 0) blocks = total;
    if (blocks > total) {
        errno = ENOSPC;
        return -1;
    }

    // Erase the specified number of blocks
    while (blocks-- > 0) {
        loff_t bpos = pos;
        if (ioctl(ctx->fd, MEMGETBADBLOCK, &bpos) > 0) {
            fprintf(stderr, "mtd: not erasing bad block at 0x%08lx\n", pos);
            pos += ctx->partition->erase_size;
            continue;  // Don't try to erase known factory-bad blocks.
        }

        struct erase_info_user erase_info;
        erase_info.start = pos;
        erase_info.length = ctx->partition->erase_size;
        if (ioctl(ctx->fd, MEMERASE, &erase_info) < 0) {
            fprintf(stderr, "mtd: erase failure at 0x%08lx\n", pos);
        }
        pos += ctx->partition->erase_size;
    }

    return pos;
}

int mtd_write_close(MtdWriteContext *ctx)
{
    int r = 0;
    // Make sure any pending data gets written
    if (mtd_erase_blocks(ctx, 0) == (off_t) -1) r = -1;
    if (close(ctx->fd)) r = -1;
    free(ctx->bad_block_offsets);
    free(ctx->buffer);
    free(ctx);
    return r;
}

/* Return the offset of the first good block at or after pos (which
 * might be pos itself).
 */
off_t mtd_find_write_start(MtdWriteContext *ctx, off_t pos) {
    int i;
    for (i = 0; i < ctx->bad_block_count; ++i) {
        if (ctx->bad_block_offsets[i] == pos) {
            pos += ctx->partition->erase_size;
        } else if (ctx->bad_block_offsets[i] > pos) {
            return pos;
        }
    }
    return pos;
}

#define BLOCK_SIZE    2048
#define SPARE_SIZE    (BLOCK_SIZE >> 5)
#define HEADER_SIZE 2048

int cmd_mtd_restore_raw_partition(const char *partition_name, const char *filename)
{
    const MtdPartition *ptn;
    MtdWriteContext *write;
    void *data;

    FILE* f = fopen(filename, "rb");
    if (f == NULL) {
        fprintf(stderr, "error opening %s", filename);
        return -1;
    }

    if (mtd_scan_partitions() <= 0)
    {
        fprintf(stderr, "error scanning partitions");
        return -1;
    }
    const MtdPartition *mtd = mtd_find_partition_by_name(partition_name);
    if (mtd == NULL)
    {
        fprintf(stderr, "can't find %s partition", partition_name);
        return -1;
    }

    int fd = open(filename, O_RDONLY);
    if (fd < 0)
    {
        printf("error opening %s", filename);
        return -1;
    }
    
    MtdWriteContext* ctx = mtd_write_partition(mtd);
    if (ctx == NULL) {
        printf("error writing %s", partition_name);
        return -1;
    }

    int success = 1;
    char* buffer = malloc(BUFSIZ);
    int read;
    while (success && (read = fread(buffer, 1, BUFSIZ, f)) > 0) {
        int wrote = mtd_write_data(ctx, buffer, read);
        success = success && (wrote == read);
    }
    free(buffer);
    fclose(f);

    if (!success) {
        fprintf(stderr, "error writing %s", partition_name);
        return -1;
    }

    if (mtd_erase_blocks(ctx, -1) == -1) {
        fprintf(stderr, "error erasing blocks of %s\n", partition_name);
    }
    if (mtd_write_close(ctx) != 0) {
        fprintf(stderr, "error closing write of %s\n", partition_name);
    }
    printf("%s %s partition\n", success ? "wrote" : "failed to write", partition_name);
    return 0;
}


int cmd_mtd_backup_raw_partition(const char *partition_name, const char *filename)
{
    MtdReadContext *in;
    const MtdPartition *partition;
    char buf[BLOCK_SIZE + SPARE_SIZE];
    size_t partition_size;
    size_t read_size;
    size_t total;
    int fd;
    int wrote;
    int len;

    if (mtd_scan_partitions() <= 0)
    {
        printf("error scanning partitions");
        return -1;
    }

    partition = mtd_find_partition_by_name(partition_name);
    if (partition == NULL)
    {
        printf("can't find %s partition", partition_name);
        return -1;
    }

    if (mtd_partition_info(partition, &partition_size, NULL, NULL)) {
        printf("can't get info of partition %s", partition_name);
        return -1;
    }

    if (!strcmp(filename, "-")) {
        fd = fileno(stdout);
    }
    else {
        fd = open(filename, O_WRONLY|O_CREAT|O_TRUNC, 0666);
    }

    if (fd < 0)
    {
       printf("error opening %s", filename);
       return -1;
    }

    in = mtd_read_partition(partition);
    if (in == NULL) {
        close(fd);
        unlink(filename);
        printf("error opening %s: %s\n", partition_name, strerror(errno));
        return -1;
    }

    total = 0;
    while ((len = mtd_read_data(in, buf, BLOCK_SIZE)) > 0) {
        wrote = write(fd, buf, len);
        if (wrote != len) {
            close(fd);
            unlink(filename);
            printf("error writing %s", filename);
            return -1;
        }
        total += BLOCK_SIZE;
    }

    mtd_read_close(in);

    if (close(fd)) {
        unlink(filename);
        printf("error closing %s", filename);
        return -1;
    }
    return 0;
}

int cmd_mtd_erase_raw_partition(const char *partition_name)
{
    MtdWriteContext *out;
    size_t erased;
    size_t total_size;
    size_t erase_size;

    if (mtd_scan_partitions() <= 0)
    {
        printf("error scanning partitions");
        return -1;
    }
    const MtdPartition *p = mtd_find_partition_by_name(partition_name);
    if (p == NULL)
    {
        printf("can't find %s partition", partition_name);
        return -1;
    }

    out = mtd_write_partition(p);
    if (out == NULL)
    {
        printf("could not estabilish write context for %s", partition_name);
        return -1;
    }

    // do the actual erase, -1 = full partition erase
    erased = mtd_erase_blocks(out, -1);

    // erased = bytes erased, if zero, something borked
    if (!erased)
    {
        printf("error erasing %s", partition_name);
        return -1;
    }

    return 0;
}

int cmd_mtd_erase_partition(const char *partition, const char *filesystem)
{
    return cmd_mtd_erase_raw_partition(partition);
}


int cmd_mtd_mount_partition(const char *partition, const char *mount_point, const char *filesystem, int read_only)
{
    mtd_scan_partitions();
    const MtdPartition *p;
    p = mtd_find_partition_by_name(partition);
    if (p == NULL) {
        return -1;
    }
    return mtd_mount_partition(p, mount_point, filesystem, read_only);
}

int cmd_mtd_get_partition_device(const char *partition, char *device)
{
    mtd_scan_partitions();
    MtdPartition *p = mtd_find_partition_by_name(partition);
    if (p == NULL)
        return -1;
    sprintf(device, "/dev/block/mtdblock%d", p->device_index);
    return 0;
}
