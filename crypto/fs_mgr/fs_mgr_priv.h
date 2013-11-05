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

#ifndef __CORE_FS_MGR_PRIV_H
#define __CORE_FS_MGR_PRIV_H

#include <cutils/klog.h>
#include <fs_mgr.h>

#define INFO(x...)    KLOG_INFO("fs_mgr", x)
#define ERROR(x...)   KLOG_ERROR("fs_mgr", x)

#define CRYPTO_TMPFS_OPTIONS "size=128m,mode=0771,uid=1000,gid=1000"

#define WAIT_TIMEOUT 20

/* fstab has the following format:
 *
 * Any line starting with a # is a comment and ignored
 *
 * Any blank line is ignored
 *
 * All other lines must be in this format:
 *   <source>  <mount_point> <fs_type> <mount_flags> <fs_options> <fs_mgr_options>
 *
 *   <mount_flags> is a comma separated list of flags that can be passed to the
 *                 mount command.  The list includes noatime, nosuid, nodev, nodiratime,
 *                 ro, rw, remount, defaults.
 *
 *   <fs_options> is a comma separated list of options accepted by the filesystem being
 *                mounted.  It is passed directly to mount without being parsed
 *
 *   <fs_mgr_options> is a comma separated list of flags that control the operation of
 *                     the fs_mgr program.  The list includes "wait", which will wait till
 *                     the <source> file exists, and "check", which requests that the fs_mgr 
 *                     run an fscheck program on the <source> before mounting the filesystem.
 *                     If check is specifed on a read-only filesystem, it is ignored.
 *                     Also, "encryptable" means that filesystem can be encrypted.
 *                     The "encryptable" flag _MUST_ be followed by a = and a string which
 *                     is the location of the encryption keys.  It can either be a path
 *                     to a file or partition which contains the keys, or the word "footer"
 *                     which means the keys are in the last 16 Kbytes of the partition
 *                     containing the filesystem.
 *
 * When the fs_mgr is requested to mount all filesystems, it will first mount all the
 * filesystems that do _NOT_ specify check (including filesystems that are read-only and
 * specify check, because check is ignored in that case) and then it will check and mount
 * filesystem marked with check.
 *
 */

#define MF_WAIT         0x1
#define MF_CHECK        0x2
#define MF_CRYPT        0x4
#define MF_NONREMOVABLE 0x8
#define MF_VOLDMANAGED  0x10
#define MF_LENGTH       0x20
#define MF_RECOVERYONLY 0x40
#define MF_SWAPPRIO     0x80
#define MF_ZRAMSIZE     0x100
#define MF_VERIFY       0x200
/*
 * There is no emulated sdcard daemon running on /data/media on this device,
 * so treat the physical SD card as the only external storage device,
 * a la the Nexus One.
 */
#define MF_NOEMULATEDSD 0x400

#define DM_BUF_SIZE 4096

#endif /* __CORE_FS_MGR_PRIV_H */

