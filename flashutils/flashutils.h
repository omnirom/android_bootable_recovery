#ifndef FLASHUTILS_H
#define FLASHUTILS_H

int restore_raw_partition(const char* partitionType, const char *partition, const char *filename);
int backup_raw_partition(const char* partitionType, const char *partition, const char *filename);
int erase_raw_partition(const char* partitionType, const char *partition);
int erase_partition(const char *partition, const char *filesystem);
int mount_partition(const char *partition, const char *mount_point, const char *filesystem, int read_only);
int get_partition_device(const char *partition, char *device);

#define FLASH_MTD 0
#define FLASH_MMC 1
#define FLASH_BML 2

int is_mtd_device();
char* get_default_filesystem();

extern int cmd_mtd_restore_raw_partition(const char *partition, const char *filename);
extern int cmd_mtd_backup_raw_partition(const char *partition, const char *filename);
extern int cmd_mtd_erase_raw_partition(const char *partition);
extern int cmd_mtd_erase_partition(const char *partition, const char *filesystem);
extern int cmd_mtd_mount_partition(const char *partition, const char *mount_point, const char *filesystem, int read_only);
extern int cmd_mtd_get_partition_device(const char *partition, char *device);

extern int cmd_mmc_restore_raw_partition(const char *partition, const char *filename);
extern int cmd_mmc_backup_raw_partition(const char *partition, const char *filename);
extern int cmd_mmc_erase_raw_partition(const char *partition);
extern int cmd_mmc_erase_partition(const char *partition, const char *filesystem);
extern int cmd_mmc_mount_partition(const char *partition, const char *mount_point, const char *filesystem, int read_only);
extern int cmd_mmc_get_partition_device(const char *partition, char *device);

extern int cmd_bml_restore_raw_partition(const char *partition, const char *filename);
extern int cmd_bml_backup_raw_partition(const char *partition, const char *filename);
extern int cmd_bml_erase_raw_partition(const char *partition);
extern int cmd_bml_erase_partition(const char *partition, const char *filesystem);
extern int cmd_bml_mount_partition(const char *partition, const char *mount_point, const char *filesystem, int read_only);
extern int cmd_bml_get_partition_device(const char *partition, char *device);

extern int device_flash_type();
extern int get_flash_type(const char* fs_type);

enum flash_type {
    UNSUPPORTED = -1,
    UNKNOWN = 0,
    MTD = 1,
    MMC = 2,
    BML = 3
};

#endif