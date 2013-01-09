#ifndef __STATVFS_H
#define __STATVFS_H

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

#endif
