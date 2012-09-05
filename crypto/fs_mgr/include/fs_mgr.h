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

int fs_mgr_mount_all(char *fstab_file);
int fs_mgr_do_mount(char *fstab_file, char *n_name, char *n_blk_dev, char *tmp_mnt_point);
int fs_mgr_do_tmpfs_mount(char *n_name);
int fs_mgr_unmount_all(char *fstab_file);
int fs_mgr_get_crypt_info(char *fstab_file, char *key_loc, char *real_blk_dev, int size);

#endif /* __CORE_FS_MGR_H */

