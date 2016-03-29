#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <string.h>

#include "flashutils/flashutils.h"

#ifndef BOARD_BML_BOOT
#define BOARD_BML_BOOT              "/dev/block/bml7"
#endif

#ifndef BOARD_BML_RECOVERY
#define BOARD_BML_RECOVERY          "/dev/block/bml8"
#endif

int the_flash_type = UNKNOWN;

int device_flash_type()
{
    if (the_flash_type == UNKNOWN) {
        if (access(BOARD_BML_BOOT, F_OK) == 0) {
            the_flash_type = BML;
        } else if (access("/proc/emmc", F_OK) == 0) {
            the_flash_type = MMC;
        } else if (access("/proc/mtd", F_OK) == 0) {
            the_flash_type = MTD;
        } else {
            the_flash_type = UNSUPPORTED;
        }
    }
    return the_flash_type;
}

char* get_default_filesystem()
{
    return device_flash_type() == MMC ? "ext3" : "yaffs2";
}

int get_flash_type(const char* partitionType) {
    int type = UNSUPPORTED;
    if (strcmp(partitionType, "mtd") == 0)
        type = MTD;
    else if (strcmp(partitionType, "emmc") == 0)
        type = MMC;
    else if (strcmp(partitionType, "bml") == 0)
        type = BML;
    return type;
}

static int detect_partition(const char *partitionType, const char *partition)
{
    int type = device_flash_type();
    if (strstr(partition, "/dev/block/mtd") != NULL)
        type = MTD;
    else if (strstr(partition, "/dev/block/mmc") != NULL || strstr(partition, "/dev/block/sd") != NULL)
        type = MMC;
    else if (strstr(partition, "/dev/block/bml") != NULL)
        type = BML;

    if (partitionType != NULL) {
        type = get_flash_type(partitionType);
    }

    return type;
}
int restore_raw_partition(const char* partitionType, const char *partition, const char *filename)
{
    int type = detect_partition(partitionType, partition);
    switch (type) {
        case MTD:
            return cmd_mtd_restore_raw_partition(partition, filename);
        case MMC:
            return cmd_mmc_restore_raw_partition(partition, filename);
        case BML:
            return cmd_bml_restore_raw_partition(partition, filename);
        default:
            return -1;
    }
}

int backup_raw_partition(const char* partitionType, const char *partition, const char *filename)
{
    int type = detect_partition(partitionType, partition);
    switch (type) {
        case MTD:
            return cmd_mtd_backup_raw_partition(partition, filename);
        case MMC:
            return cmd_mmc_backup_raw_partition(partition, filename);
        case BML:
            return cmd_bml_backup_raw_partition(partition, filename);
        default:
            printf("unable to detect device type");
            return -1;
    }
}

int erase_raw_partition(const char* partitionType, const char *partition)
{
    int type = detect_partition(partitionType, partition);
    switch (type) {
        case MTD:
            return cmd_mtd_erase_raw_partition(partition);
        case MMC:
            return cmd_mmc_erase_raw_partition(partition);
        case BML:
            return cmd_bml_erase_raw_partition(partition);
        default:
            return -1;
    }
}

int erase_partition(const char *partition, const char *filesystem)
{
    int type = detect_partition(NULL, partition);
    switch (type) {
        case MTD:
            return cmd_mtd_erase_partition(partition, filesystem);
        case MMC:
            return cmd_mmc_erase_partition(partition, filesystem);
        case BML:
            return cmd_bml_erase_partition(partition, filesystem);
        default:
            return -1;
    }
}

int mount_partition(const char *partition, const char *mount_point, const char *filesystem, int read_only)
{
    int type = detect_partition(NULL, partition);
    switch (type) {
        case MTD:
            return cmd_mtd_mount_partition(partition, mount_point, filesystem, read_only);
        case MMC:
            return cmd_mmc_mount_partition(partition, mount_point, filesystem, read_only);
        case BML:
            return cmd_bml_mount_partition(partition, mount_point, filesystem, read_only);
        default:
            return -1;
    }
}

int get_partition_device(const char *partition, char *device)
{
    int type = device_flash_type();
    switch (type) {
        case MTD:
            return cmd_mtd_get_partition_device(partition, device);
        case MMC:
            return cmd_mmc_get_partition_device(partition, device);
        case BML:
            return cmd_bml_get_partition_device(partition, device);
        default:
            return -1;
    }
}
