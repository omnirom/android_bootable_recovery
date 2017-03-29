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

#include "roots.h"

#include <errno.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>

#include <android-base/logging.h>
#include <ext4_utils/make_ext4fs.h>
#include <ext4_utils/wipe.h>
#include <fs_mgr.h>

#include "common.h"
#include "mounts.h"
#include "cryptfs.h"

static struct fstab *fstab = NULL;

extern struct selabel_handle *sehandle;

void load_volume_table()
{
    int i;
    int ret;

    fstab = fs_mgr_read_fstab_default();
    if (!fstab) {
        LOG(ERROR) << "failed to read default fstab";
        return;
    }

    ret = fs_mgr_add_entry(fstab, "/tmp", "ramdisk", "ramdisk");
    if (ret < 0 ) {
        LOG(ERROR) << "failed to add /tmp entry to fstab";
        fs_mgr_free_fstab(fstab);
        fstab = NULL;
        return;
    }

    printf("recovery filesystem table\n");
    printf("=========================\n");
    for (i = 0; i < fstab->num_entries; ++i) {
        Volume* v = &fstab->recs[i];
        printf("  %d %s %s %s %lld\n", i, v->mount_point, v->fs_type,
               v->blk_device, v->length);
    }
    printf("\n");
}

Volume* volume_for_path(const char* path) {
    return fs_mgr_get_entry_for_mount_point(fstab, path);
}

// Mount the volume specified by path at the given mount_point.
int ensure_path_mounted_at(const char* path, const char* mount_point) {
    Volume* v = volume_for_path(path);
    if (v == NULL) {
        LOG(ERROR) << "unknown volume for path [" << path << "]";
        return -1;
    }
    if (strcmp(v->fs_type, "ramdisk") == 0) {
        // the ramdisk is always mounted.
        return 0;
    }

    if (!scan_mounted_volumes()) {
        LOG(ERROR) << "failed to scan mounted volumes";
        return -1;
    }

    if (!mount_point) {
        mount_point = v->mount_point;
    }

    MountedVolume* mv = find_mounted_volume_by_mount_point(mount_point);
    if (mv) {
        // volume is already mounted
        return 0;
    }

    mkdir(mount_point, 0755);  // in case it doesn't already exist

    if (strcmp(v->fs_type, "ext4") == 0 ||
               strcmp(v->fs_type, "squashfs") == 0 ||
               strcmp(v->fs_type, "vfat") == 0) {
        int result = mount(v->blk_device, mount_point, v->fs_type, v->flags, v->fs_options);
        if (result == -1 && fs_mgr_is_formattable(v)) {
            LOG(ERROR) << "failed to mount " << mount_point << " (" << strerror(errno)
                       << ") , formatting.....";
            bool crypt_footer = fs_mgr_is_encryptable(v) && !strcmp(v->key_loc, "footer");
            if (fs_mgr_do_format(v, crypt_footer) == 0) {
                result = mount(v->blk_device, mount_point, v->fs_type, v->flags, v->fs_options);
            } else {
                PLOG(ERROR) << "failed to format " << mount_point;
                return -1;
            }
        }

        if (result == -1) {
            PLOG(ERROR) << "failed to mount " << mount_point;
            return -1;
        }
        return 0;
    }

    LOG(ERROR) << "unknown fs_type \"" << v->fs_type << "\" for " << mount_point;
    return -1;
}

int ensure_path_mounted(const char* path) {
    // Mount at the default mount point.
    return ensure_path_mounted_at(path, nullptr);
}

int ensure_path_unmounted(const char* path) {
    Volume* v = volume_for_path(path);
    if (v == NULL) {
        LOG(ERROR) << "unknown volume for path [" << path << "]";
        return -1;
    }
    if (strcmp(v->fs_type, "ramdisk") == 0) {
        // the ramdisk is always mounted; you can't unmount it.
        return -1;
    }

    if (!scan_mounted_volumes()) {
        LOG(ERROR) << "failed to scan mounted volumes";
        return -1;
    }

    MountedVolume* mv = find_mounted_volume_by_mount_point(v->mount_point);
    if (mv == NULL) {
        // volume is already unmounted
        return 0;
    }

    return unmount_mounted_volume(mv);
}

static int exec_cmd(const char* path, char* const argv[]) {
    int status;
    pid_t child;
    if ((child = vfork()) == 0) {
        execv(path, argv);
        _exit(EXIT_FAILURE);
    }
    waitpid(child, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        LOG(ERROR) << path << " failed with status " << WEXITSTATUS(status);
    }
    return WEXITSTATUS(status);
}

int format_volume(const char* volume, const char* directory) {
    Volume* v = volume_for_path(volume);
    if (v == NULL) {
        LOG(ERROR) << "unknown volume \"" << volume << "\"";
        return -1;
    }
    if (strcmp(v->fs_type, "ramdisk") == 0) {
        // you can't format the ramdisk.
        LOG(ERROR) << "can't format_volume \"" << volume << "\"";
        return -1;
    }
    if (strcmp(v->mount_point, volume) != 0) {
        LOG(ERROR) << "can't give path \"" << volume << "\" to format_volume";
        return -1;
    }

    if (ensure_path_unmounted(volume) != 0) {
        LOG(ERROR) << "format_volume failed to unmount \"" << v->mount_point << "\"";
        return -1;
    }

    if (strcmp(v->fs_type, "ext4") == 0 || strcmp(v->fs_type, "f2fs") == 0) {
        // if there's a key_loc that looks like a path, it should be a
        // block device for storing encryption metadata.  wipe it too.
        if (v->key_loc != NULL && v->key_loc[0] == '/') {
            LOG(INFO) << "wiping " << v->key_loc;
            int fd = open(v->key_loc, O_WRONLY | O_CREAT, 0644);
            if (fd < 0) {
                LOG(ERROR) << "format_volume: failed to open " << v->key_loc;
                return -1;
            }
            wipe_block_device(fd, get_file_size(fd));
            close(fd);
        }

        ssize_t length = 0;
        if (v->length != 0) {
            length = v->length;
        } else if (v->key_loc != NULL && strcmp(v->key_loc, "footer") == 0) {
            length = -CRYPT_FOOTER_OFFSET;
        }
        int result;
        if (strcmp(v->fs_type, "ext4") == 0) {
            if (v->erase_blk_size != 0 && v->logical_blk_size != 0) {
                result = make_ext4fs_directory_align(v->blk_device, length, volume, sehandle,
                        directory, v->erase_blk_size, v->logical_blk_size);
            } else {
                result = make_ext4fs_directory(v->blk_device, length, volume, sehandle, directory);
            }
        } else {   /* Has to be f2fs because we checked earlier. */
            if (v->key_loc != NULL && strcmp(v->key_loc, "footer") == 0 && length < 0) {
                LOG(ERROR) << "format_volume: crypt footer + negative length (" << length
                           << ") not supported on " << v->fs_type;
                return -1;
            }
            if (length < 0) {
                LOG(ERROR) << "format_volume: negative length (" << length
                           << ") not supported on " << v->fs_type;
                return -1;
            }
            char *num_sectors;
            if (asprintf(&num_sectors, "%zd", length / 512) <= 0) {
                LOG(ERROR) << "format_volume: failed to create " << v->fs_type
                           << " command for " << v->blk_device;
                return -1;
            }
            const char *f2fs_path = "/sbin/mkfs.f2fs";
            const char* const f2fs_argv[] = {"mkfs.f2fs", "-t", "-d1", v->blk_device, num_sectors, NULL};

            result = exec_cmd(f2fs_path, (char* const*)f2fs_argv);
            free(num_sectors);
        }
        if (result != 0) {
            PLOG(ERROR) << "format_volume: make " << v->fs_type << " failed on " << v->blk_device;
            return -1;
        }
        return 0;
    }

    LOG(ERROR) << "format_volume: fs_type \"" << v->fs_type << "\" unsupported";
    return -1;
}

int format_volume(const char* volume) {
    return format_volume(volume, NULL);
}

int setup_install_mounts() {
    if (fstab == NULL) {
        LOG(ERROR) << "can't set up install mounts: no fstab loaded";
        return -1;
    }
    for (int i = 0; i < fstab->num_entries; ++i) {
        Volume* v = fstab->recs + i;

        if (strcmp(v->mount_point, "/tmp") == 0 ||
            strcmp(v->mount_point, "/cache") == 0) {
            if (ensure_path_mounted(v->mount_point) != 0) {
                LOG(ERROR) << "failed to mount " << v->mount_point;
                return -1;
            }

        } else {
            if (ensure_path_unmounted(v->mount_point) != 0) {
                LOG(ERROR) << "failed to unmount " << v->mount_point;
                return -1;
            }
        }
    }
    return 0;
}
