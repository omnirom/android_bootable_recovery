/*
 * Copyright (C) 2012 The Android Open Source Project
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

#ifndef __CORE_FS_MGR_H
#define __CORE_FS_MGR_H

#include <stdint.h>
#include <linux/dm-ioctl.h>

#ifdef __cplusplus
extern "C" {
#endif

struct fstab {
    int num_entries;
    struct fstab_rec *recs;
    char *fstab_filename;
};

struct fstab_rec {
    char *blk_device;
    char *mount_point;
    char *fs_type;
    unsigned long flags;
    char *fs_options;
    int fs_mgr_flags;
    char *key_loc;
    char *verity_loc;
    long long length;
    char *label;
    int partnum;
    int swap_prio;
    unsigned int zram_size;
};

struct fstab *fs_mgr_read_fstab(const char *fstab_path);
void fs_mgr_free_fstab(struct fstab *fstab);
int fs_mgr_mount_all(struct fstab *fstab);
int fs_mgr_do_mount(struct fstab *fstab, char *n_name, char *n_blk_device,
                    char *tmp_mount_point);
int fs_mgr_do_tmpfs_mount(char *n_name);
int fs_mgr_unmount_all(struct fstab *fstab);
int fs_mgr_get_crypt_info(struct fstab *fstab, char *key_loc,
                          char *real_blk_device, int size);
int fs_mgr_add_entry(struct fstab *fstab,
                     const char *mount_point, const char *fs_type,
                     const char *blk_device, long long length);
struct fstab_rec *fs_mgr_get_entry_for_mount_point(struct fstab *fstab, const char *path);
int fs_mgr_is_voldmanaged(struct fstab_rec *fstab);
int fs_mgr_is_nonremovable(struct fstab_rec *fstab);
int fs_mgr_is_encryptable(struct fstab_rec *fstab);
int fs_mgr_is_noemulatedsd(struct fstab_rec *fstab);
int fs_mgr_swapon_all(struct fstab *fstab);
#ifdef __cplusplus
}
#endif

#endif /* __CORE_FS_MGR_H */

