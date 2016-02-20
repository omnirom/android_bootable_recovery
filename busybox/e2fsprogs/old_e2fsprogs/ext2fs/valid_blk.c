/* vi: set sw=4 ts=4: */
/*
 * valid_blk.c --- does the inode have valid blocks?
 *
 * Copyright 1997 by Theodore Ts'o
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 *
 */

#include <stdio.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <string.h>
#include <time.h>

#include "ext2_fs.h"
#include "ext2fs.h"

/*
 * This function returns 1 if the inode's block entries actually
 * contain block entries.
 */
int ext2fs_inode_has_valid_blocks(struct ext2_inode *inode)
{
	/*
	 * Only directories, regular files, and some symbolic links
	 * have valid block entries.
	 */
	if (!LINUX_S_ISDIR(inode->i_mode) && !LINUX_S_ISREG(inode->i_mode) &&
	    !LINUX_S_ISLNK(inode->i_mode))
		return 0;

	/*
	 * If the symbolic link is a "fast symlink", then the symlink
	 * target is stored in the block entries.
	 */
	if (LINUX_S_ISLNK (inode->i_mode)) {
		if (inode->i_file_acl == 0) {
			/* With no EA block, we can rely on i_blocks */
			if (inode->i_blocks == 0)
				return 0;
		} else {
			/* With an EA block, life gets more tricky */
			if (inode->i_size >= EXT2_N_BLOCKS*4)
				return 1; /* definitely using i_block[] */
			if (inode->i_size > 4 && inode->i_block[1] == 0)
				return 1; /* definitely using i_block[] */
			return 0; /* Probably a fast symlink */
		}
	}
	return 1;
}
