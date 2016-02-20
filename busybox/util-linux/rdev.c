/* vi: set sw=4 ts=4: */
/*
 * rdev - print device node associated with a filesystem
 *
 * Copyright (c) 2008 Nuovation System Designs, LLC
 *   Grant Erickson <gerickson@nuovations.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 *
 */

//usage:#define rdev_trivial_usage
//usage:       ""
//usage:#define rdev_full_usage "\n\n"
//usage:       "Print the device node associated with the filesystem mounted at '/'"
//usage:
//usage:#define rdev_example_usage
//usage:       "$ rdev\n"
//usage:       "/dev/mtdblock9 /\n"

#include "libbb.h"

int rdev_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int rdev_main(int argc UNUSED_PARAM, char **argv UNUSED_PARAM)
{
	const char *root_device = find_block_device("/");

	if (root_device) {
		printf("%s /\n", root_device);
		return EXIT_SUCCESS;
	}
	return EXIT_FAILURE;
}
