/*
 * Minix partition parsing code
 *
 * Copyright (C) 2009 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "partitions.h"
#include "minix.h"

static int probe_minix_pt(blkid_probe pr,
		const struct blkid_idmag *mag __attribute__((__unused__)))
{
	struct dos_partition *p;
	blkid_parttable tab = NULL;
	blkid_partition parent;
	blkid_partlist ls;
	unsigned char *data;
	int i;

	data = blkid_probe_get_sector(pr, 0);
	if (!data) {
		if (errno)
			return -errno;
		goto nothing;
	}

	ls = blkid_probe_get_partlist(pr);
	if (!ls)
		goto nothing;

	/* Parent is required, because Minix uses the same PT as DOS and
	 * difference is only in primary partition (parent) type.
	 */
	parent = blkid_partlist_get_parent(ls);
	if (!parent)
		goto nothing;

	if (blkid_partition_get_type(parent) != MBR_MINIX_PARTITION)
		goto nothing;

	if (blkid_partitions_need_typeonly(pr))
		/* caller does not ask for details about partitions */
		return BLKID_PROBE_OK;

	tab = blkid_partlist_new_parttable(ls, "minix", MBR_PT_OFFSET);
	if (!tab)
		goto err;

	for (i = 0, p = mbr_get_partition(data, 0);
			i < MINIX_MAXPARTITIONS; i++, p++) {

		uint32_t start, size;
		blkid_partition par;

		if (p->sys_ind != MBR_MINIX_PARTITION)
			continue;

		start = dos_partition_get_start(p);
		size = dos_partition_get_size(p);

		if (parent && !blkid_is_nested_dimension(parent, start, size)) {
			DBG(LOWPROBE, ul_debug(
				"WARNING: minix partition (%d) overflow "
				"detected, ignore", i));
			continue;
		}

		par = blkid_partlist_add_partition(ls, tab, start, size);
		if (!par)
			goto err;

		blkid_partition_set_type(par, p->sys_ind);
		blkid_partition_set_flags(par, p->boot_ind);
	}

	return BLKID_PROBE_OK;

nothing:
	return BLKID_PROBE_NONE;
err:
	return -ENOMEM;
}

/* same as DOS */
const struct blkid_idinfo minix_pt_idinfo =
{
	.name		= "minix",
	.probefunc	= probe_minix_pt,
	.magics		=
	{
		{ .magic = "\x55\xAA", .len = 2, .sboff = 510 },
		{ NULL }
	}
};

