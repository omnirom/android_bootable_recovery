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

#ifndef _SYS_STATVFS_H_
#define _SYS_STATVFS_H_
#include <sys/types.h>

struct statvfs {
	unsigned long  f_bsize;    /* file system block size */
	unsigned long  f_frsize;   /* fragment size */
	fsblkcnt_t     f_blocks;   /* size of fs in f_frsize units */
	fsblkcnt_t     f_bfree;    /* # free blocks */
	fsblkcnt_t     f_bavail;   /* # free blocks for non-root */
	fsfilcnt_t     f_files;    /* # inodes */
	fsfilcnt_t     f_ffree;    /* # free inodes */
	fsfilcnt_t     f_favail;   /* # free inodes for non-root */
	unsigned long  f_fsid;     /* file system ID */
	unsigned long  f_flag;     /* mount flags */
	unsigned long  f_namemax;  /* maximum filename length */
};

int statvfs(const char *, struct statvfs *);
#endif
