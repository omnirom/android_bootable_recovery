/*
	cbm.c (09.11.10)
	Clusters Bitmap creation code.

	Copyright (C) 2011-2013  Andrew Nayenko

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <limits.h>
#include "cbm.h"
#include "fat.h"
#include "uct.h"
#include "rootdir.h"

static off64_t cbm_alignment(void)
{
	return get_cluster_size();
}

static off64_t cbm_size(void)
{
	return DIV_ROUND_UP(
			(get_volume_size() - get_position(&cbm)) / get_cluster_size(),
			CHAR_BIT);
}

static int cbm_write(struct exfat_dev* dev)
{
	uint32_t allocated_clusters =
			DIV_ROUND_UP(cbm.get_size(), get_cluster_size()) +
			DIV_ROUND_UP(uct.get_size(), get_cluster_size()) +
			DIV_ROUND_UP(rootdir.get_size(), get_cluster_size());
	size_t bitmap_size = DIV_ROUND_UP(allocated_clusters, CHAR_BIT);
	uint8_t* bitmap = malloc(bitmap_size);
	size_t i;

	if (bitmap == NULL)
	{
		exfat_error("failed to allocate bitmap of %zu bytes", bitmap_size);
		return 1;
	}

	for (i = 0; i < bitmap_size * CHAR_BIT; i++)
		if (i < allocated_clusters)
			BMAP_SET(bitmap, i);
		else
			BMAP_CLR(bitmap, i);
	if (exfat_write(dev, bitmap, bitmap_size) < 0)
	{
		exfat_error("failed to write bitmap of %zu bytes", bitmap_size);
		return 1;
	}
	free(bitmap);
	return 0;
}

const struct fs_object cbm =
{
	.get_alignment = cbm_alignment,
	.get_size = cbm_size,
	.write = cbm_write,
};
