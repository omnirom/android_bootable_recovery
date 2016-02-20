/* vi: set sw=4 ts=4: */
/*
 * read_bb --- read the bad blocks inode
 *
 * Copyright (C) 1994 Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

#include <stdio.h>
#include <string.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <fcntl.h>
#include <time.h>
#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#include "ext2_fs.h"
#include "ext2fs.h"

struct read_bb_record {
	ext2_badblocks_list	bb_list;
	errcode_t	err;
};

/*
 * Helper function for ext2fs_read_bb_inode()
 */
#ifdef __TURBOC__
# pragma argsused
#endif
static int mark_bad_block(ext2_filsys fs, blk_t *block_nr,
			  e2_blkcnt_t blockcnt EXT2FS_ATTR((unused)),
			  blk_t ref_block EXT2FS_ATTR((unused)),
			  int ref_offset EXT2FS_ATTR((unused)),
			  void *priv_data)
{
	struct read_bb_record *rb = (struct read_bb_record *) priv_data;

	if (blockcnt < 0)
		return 0;

	if ((*block_nr < fs->super->s_first_data_block) ||
	    (*block_nr >= fs->super->s_blocks_count))
		return 0;	/* Ignore illegal blocks */

	rb->err = ext2fs_badblocks_list_add(rb->bb_list, *block_nr);
	if (rb->err)
		return BLOCK_ABORT;
	return 0;
}

/*
 * Reads the current bad blocks from the bad blocks inode.
 */
errcode_t ext2fs_read_bb_inode(ext2_filsys fs, ext2_badblocks_list *bb_list)
{
	errcode_t	retval;
	struct read_bb_record rb;
	struct ext2_inode inode;
	blk_t	numblocks;

	EXT2_CHECK_MAGIC(fs, EXT2_ET_MAGIC_EXT2FS_FILSYS);

	if (!*bb_list) {
		retval = ext2fs_read_inode(fs, EXT2_BAD_INO, &inode);
		if (retval)
			return retval;
		if (inode.i_blocks < 500)
			numblocks = (inode.i_blocks /
				     (fs->blocksize / 512)) + 20;
		else
			numblocks = 500;
		retval = ext2fs_badblocks_list_create(bb_list, numblocks);
		if (retval)
			return retval;
	}

	rb.bb_list = *bb_list;
	rb.err = 0;
	retval = ext2fs_block_iterate2(fs, EXT2_BAD_INO, 0, 0,
				      mark_bad_block, &rb);
	if (retval)
		return retval;

	return rb.err;
}
