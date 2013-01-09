/*
	uct.c (09.11.10)
	Upper Case Table creation code.

	Copyright (C) 2011, 2012  Andrew Nayenko

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

#include "uct.h"
#include "uctc.h"

static off64_t uct_alignment(void)
{
	return get_cluster_size();
}

static off64_t uct_size(void)
{
	return sizeof(upcase_table);
}

static int uct_write(struct exfat_dev* dev)
{
	if (exfat_write(dev, upcase_table, sizeof(upcase_table)) < 0)
	{
		exfat_error("failed to write upcase table of %zu bytes",
				sizeof(upcase_table));
		return 1;
	}
	return 0;
}

const struct fs_object uct =
{
	.get_alignment = uct_alignment,
	.get_size = uct_size,
	.write = uct_write,
};
