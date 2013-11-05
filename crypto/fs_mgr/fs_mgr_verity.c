/*
 * Copyright (C) 2013 The Android Open Source Project
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
#include <ctype.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <libgen.h>
#include <time.h>

#include <private/android_filesystem_config.h>
#include <logwrap/logwrap.h>

#include "mincrypt/rsa.h"
#include "mincrypt/sha.h"
#include "mincrypt/sha256.h"

#include "ext4_utils.h"
#include "ext4.h"

#include "fs_mgr_priv.h"
#include "fs_mgr_priv_verity.h"

#define VERITY_METADATA_SIZE 32768
#define VERITY_METADATA_MAGIC_NUMBER 0xb001b001
#define VERITY_TABLE_RSA_KEY "/verity_key"

extern struct fs_info info;

static RSAPublicKey *load_key(char *path)
{
    FILE *f;
    RSAPublicKey *key;

    key = malloc(sizeof(RSAPublicKey));
    if (!key) {
        ERROR("Can't malloc key\n");
        return NULL;
    }

    f = fopen(path, "r");
    if (!f) {
        ERROR("Can't open '%s'\n", path);
        free(key);
        return NULL;
    }

    if (!fread(key, sizeof(*key), 1, f)) {
        ERROR("Could not read key!");
        fclose(f);
        free(key);
        return NULL;
    }

    if (key->len != RSANUMWORDS) {
        ERROR("Invalid key length %d\n", key->len);
        fclose(f);
        free(key);
        return NULL;
    }

    fclose(f);
    return key;
}

static int verify_table(char *signature, char *table, int table_length)
{
    int fd;
    RSAPublicKey *key;
    uint8_t hash_buf[SHA_DIGEST_SIZE];
    int retval = -1;

    // Hash the table
    SHA_hash((uint8_t*)table, table_length, hash_buf);

    // Now get the public key from the keyfile
    key = load_key(VERITY_TABLE_RSA_KEY);
    if (!key) {
        ERROR("Couldn't load verity keys");
        goto out;
    }

    // verify the result
    if (!RSA_verify(key,
                    (uint8_t*) signature,
                    RSANUMBYTES,
                    (uint8_t*) hash_buf,
                    SHA_DIGEST_SIZE)) {
        ERROR("Couldn't verify table.");
        goto out;
    }

    retval = 0;

out:
    free(key);
    return retval;
}

static int get_target_device_size(char *blk_device, uint64_t *device_size)
{
    int data_device;
    struct ext4_super_block sb;

    data_device = open(blk_device, O_RDONLY);
    if (data_device < 0) {
        ERROR("Error opening block device (%s)", strerror(errno));
        return -1;
    }

    if (lseek64(data_device, 1024, SEEK_SET) < 0) {
        ERROR("Error seeking to superblock");
        close(data_device);
        return -1;
    }

    if (read(data_device, &sb, sizeof(sb)) != sizeof(sb)) {
        ERROR("Error reading superblock");
        close(data_device);
        return -1;
    }

    ext4_parse_sb(&sb);
    *device_size = info.len;

    close(data_device);
    return 0;
}

static int read_verity_metadata(char *block_device, char **signature, char **table)
{
    unsigned magic_number;
    unsigned table_length;
    uint64_t device_length;
    int protocol_version;
    FILE *device;
    int retval = -1;

    device = fopen(block_device, "r");
    if (!device) {
        ERROR("Could not open block device %s (%s).\n", block_device, strerror(errno));
        goto out;
    }

    // find the start of the verity metadata
    if (get_target_device_size(block_device, &device_length) < 0) {
        ERROR("Could not get target device size.\n");
        goto out;
    }
    if (fseek(device, device_length, SEEK_SET) < 0) {
        ERROR("Could not seek to start of verity metadata block.\n");
        goto out;
    }

    // check the magic number
    if (!fread(&magic_number, sizeof(int), 1, device)) {
        ERROR("Couldn't read magic number!\n");
        goto out;
    }
    if (magic_number != VERITY_METADATA_MAGIC_NUMBER) {
        ERROR("Couldn't find verity metadata at offset %llu!\n", device_length);
        goto out;
    }

    // check the protocol version
    if (!fread(&protocol_version, sizeof(int), 1, device)) {
        ERROR("Couldn't read verity metadata protocol version!\n");
        goto out;
    }
    if (protocol_version != 0) {
        ERROR("Got unknown verity metadata protocol version %d!\n", protocol_version);
        goto out;
    }

    // get the signature
    *signature = (char*) malloc(RSANUMBYTES * sizeof(char));
    if (!*signature) {
        ERROR("Couldn't allocate memory for signature!\n");
        goto out;
    }
    if (!fread(*signature, RSANUMBYTES, 1, device)) {
        ERROR("Couldn't read signature from verity metadata!\n");
        free(*signature);
        goto out;
    }

    // get the size of the table
    if (!fread(&table_length, sizeof(int), 1, device)) {
        ERROR("Couldn't get the size of the verity table from metadata!\n");
        free(*signature);
        goto out;
    }

    // get the table + null terminator
    table_length += 1;
    *table = malloc(table_length);
    if(!*table) {
        ERROR("Couldn't allocate memory for verity table!\n");
        goto out;
    }
    if (!fgets(*table, table_length, device)) {
        ERROR("Couldn't read the verity table from metadata!\n");
        free(*table);
        free(*signature);
        goto out;
    }

    retval = 0;

out:
    if (device)
        fclose(device);
    return retval;
}

static void verity_ioctl_init(struct dm_ioctl *io, char *name, unsigned flags)
{
    memset(io, 0, DM_BUF_SIZE);
    io->data_size = DM_BUF_SIZE;
    io->data_start = sizeof(struct dm_ioctl);
    io->version[0] = 4;
    io->version[1] = 0;
    io->version[2] = 0;
    io->flags = flags | DM_READONLY_FLAG;
    if (name) {
        strlcpy(io->name, name, sizeof(io->name));
    }
}

static int create_verity_device(struct dm_ioctl *io, char *name, int fd)
{
    verity_ioctl_init(io, name, 1);
    if (ioctl(fd, DM_DEV_CREATE, io)) {
        ERROR("Error creating device mapping (%s)", strerror(errno));
        return -1;
    }
    return 0;
}

static int get_verity_device_name(struct dm_ioctl *io, char *name, int fd, char **dev_name)
{
    verity_ioctl_init(io, name, 0);
    if (ioctl(fd, DM_DEV_STATUS, io)) {
        ERROR("Error fetching verity device number (%s)", strerror(errno));
        return -1;
    }
    int dev_num = (io->dev & 0xff) | ((io->dev >> 12) & 0xfff00);
    if (asprintf(dev_name, "/dev/block/dm-%u", dev_num) < 0) {
        ERROR("Error getting verity block device name (%s)", strerror(errno));
        return -1;
    }
    return 0;
}

static int load_verity_table(struct dm_ioctl *io, char *name, char *blockdev, int fd, char *table)
{
    char *verity_params;
    char *buffer = (char*) io;
    uint64_t device_size = 0;

    if (get_target_device_size(blockdev, &device_size) < 0) {
        return -1;
    }

    verity_ioctl_init(io, name, DM_STATUS_TABLE_FLAG);

    struct dm_target_spec *tgt = (struct dm_target_spec *) &buffer[sizeof(struct dm_ioctl)];

    // set tgt arguments here
    io->target_count = 1;
    tgt->status=0;
    tgt->sector_start=0;
    tgt->length=device_size/512;
    strcpy(tgt->target_type, "verity");

    // build the verity params here
    verity_params = buffer + sizeof(struct dm_ioctl) + sizeof(struct dm_target_spec);
    if (sprintf(verity_params, "%s", table) < 0) {
        return -1;
    }

    // set next target boundary
    verity_params += strlen(verity_params) + 1;
    verity_params = (char*) (((unsigned long)verity_params + 7) & ~8);
    tgt->next = verity_params - buffer;

    // send the ioctl to load the verity table
    if (ioctl(fd, DM_TABLE_LOAD, io)) {
        ERROR("Error loading verity table (%s)", strerror(errno));
        return -1;
    }

    return 0;
}

static int resume_verity_table(struct dm_ioctl *io, char *name, int fd)
{
    verity_ioctl_init(io, name, 0);
    if (ioctl(fd, DM_DEV_SUSPEND, io)) {
        ERROR("Error activating verity device (%s)", strerror(errno));
        return -1;
    }
    return 0;
}

static int test_access(char *device) {
    int tries = 25;
    while (tries--) {
        if (!access(device, F_OK) || errno != ENOENT) {
            return 0;
        }
        usleep(40 * 1000);
    }
    return -1;
}

int fs_mgr_setup_verity(struct fstab_rec *fstab) {

    int retval = -1;

    char *verity_blk_name;
    char *verity_table;
    char *verity_table_signature;

    char buffer[DM_BUF_SIZE];
    struct dm_ioctl *io = (struct dm_ioctl *) buffer;
    char *mount_point = basename(fstab->mount_point);

    // set the dm_ioctl flags
    io->flags |= 1;
    io->target_count = 1;

    // get the device mapper fd
    int fd;
    if ((fd = open("/dev/device-mapper", O_RDWR)) < 0) {
        ERROR("Error opening device mapper (%s)", strerror(errno));
        return retval;
    }

    // create the device
    if (create_verity_device(io, mount_point, fd) < 0) {
        ERROR("Couldn't create verity device!");
        goto out;
    }

    // get the name of the device file
    if (get_verity_device_name(io, mount_point, fd, &verity_blk_name) < 0) {
        ERROR("Couldn't get verity device number!");
        goto out;
    }

    // read the verity block at the end of the block device
    if (read_verity_metadata(fstab->blk_device,
                                    &verity_table_signature,
                                    &verity_table) < 0) {
        goto out;
    }

    // verify the signature on the table
    if (verify_table(verity_table_signature,
                            verity_table,
                            strlen(verity_table)) < 0) {
        goto out;
    }

    // load the verity mapping table
    if (load_verity_table(io, mount_point, fstab->blk_device, fd, verity_table) < 0) {
        goto out;
    }

    // activate the device
    if (resume_verity_table(io, mount_point, fd) < 0) {
        goto out;
    }

    // assign the new verity block device as the block device
    free(fstab->blk_device);
    fstab->blk_device = verity_blk_name;

    // make sure we've set everything up properly
    if (test_access(fstab->blk_device) < 0) {
        goto out;
    }

    retval = 0;

out:
    close(fd);
    return retval;
}
