/*
 * Copyright (c) 2013 a3955269 all rights reversed, no rights reserved.
 */

#define TW_INCLUDE_CRYPTO_SAMSUNG
#include "../ics/cryptfs.c"

int dm_remove_device(const char *name)
{
    int r;
    r = delete_crypto_blk_dev(name);
    if(!r)
        printf("crypto block device '%s' deleted.\n", name);
    else
        printf("deleting crypto block device '%s' failed. [%d - %s]\n", name, r, strerror(errno));
    return r;
}

int ecryptfs_test(const char *pw)
{
   char pwbuf[256];
   int r;

   strcpy(pwbuf, pw);
   // 0: building options without file encryption filtering.
   // 1: building options with media files filtering.
   // 2: building options with all new files filtering.
   r = mount_ecryptfs_drive(pwbuf, "/emmc", "/emmc", 0);
   printf("mount_ecryptfs_drive: %d\n", r);
   r = mount("/dev/block/mmcblk1", "/emmc", "vfat", MS_RDONLY, "");
   printf("mount: %d\n", r);

   r = umount("/emmc");///dev/block/mmcblk1");
   printf("umount: %d\n", r);

   //r = unmount_ecryptfs_drive("/emmc");
   //printf("unmount_ecryptfs_drive: %d\n", r);

   return r;
}

int main(int argc, char* argv[])
{
    if(argc < 2)
    {
        printf("no args!\n");
        return 1;
    }

    property_set("ro.crypto.state", "encrypted");

    property_set("ro.crypto.fs_type", CRYPTO_FS_TYPE);
    property_set("ro.crypto.fs_real_blkdev", CRYPTO_REAL_BLKDEV);
    property_set("ro.crypto.fs_mnt_point", CRYPTO_MNT_POINT);
    property_set("ro.crypto.fs_options", CRYPTO_FS_OPTIONS);
    property_set("ro.crypto.fs_flags", CRYPTO_FS_FLAGS);
    property_set("ro.crypto.keyfile.userdata", CRYPTO_KEY_LOC);

#ifdef CRYPTO_SD_FS_TYPE
    property_set("ro.crypto.sd_fs_type", CRYPTO_SD_FS_TYPE);
    property_set("ro.crypto.sd_fs_real_blkdev", CRYPTO_SD_REAL_BLKDEV);
    property_set("ro.crypto.sd_fs_mnt_point", EXPAND(TW_INTERNAL_STORAGE_PATH));
#endif

    property_set("rw.km_fips_status", "ready");

    delete_crypto_blk_dev("userdata");
    delete_crypto_blk_dev("sdcard");
    delete_crypto_blk_dev("emmc");

    cryptfs_check_passwd(argv[1]);

    return 0;
};
