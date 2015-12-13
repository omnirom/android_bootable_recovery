/*
 * Copyright (C) 2012 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include "sys/statvfs.h"
#include <sys/statfs.h>

#define MAP(to,from) vfs->to = sfs.from

int statvfs(const char *path, struct statvfs *vfs) {
    struct statfs sfs;
    int ret;
    int *fsid;
    if ((ret = statfs(path, &sfs)) != 0)
        return ret;

    MAP(f_bsize,   f_bsize);
    MAP(f_frsize,  f_frsize);
    MAP(f_blocks,  f_blocks);
    MAP(f_bfree,   f_bfree);
    MAP(f_bavail,  f_bavail);
    MAP(f_files,   f_files);
    MAP(f_ffree,   f_ffree);
    MAP(f_namemax, f_namelen);

    vfs->f_favail = 0;
    vfs->f_flag   = 0;

    fsid = (int *)&sfs.f_fsid;
    vfs->f_fsid   = (fsid[0] << sizeof(fsid[0])) | fsid[1];

    return ret;
}
