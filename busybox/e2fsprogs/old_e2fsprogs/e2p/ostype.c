/* vi: set sw=4 ts=4: */
/*
 * getostype.c          - Get the Filesystem OS type
 *
 * Copyright (C) 2004,2005  Theodore Ts'o <tytso@mit.edu>
 *
 * This file can be redistributed under the terms of the GNU Library General
 * Public License
 */

#include "e2p.h"
#include <string.h>
#include <stdlib.h>

static const char *const os_tab[] =
	{ "Linux",
	  "Hurd",
	  "Masix",
	  "FreeBSD",
	  "Lites",
	  0 };

/*
 * Convert an os_type to a string
 */
char *e2p_os2string(int os_type)
{
	const char *os;
	char *ret;

	if (os_type <= EXT2_OS_LITES)
		os = os_tab[os_type];
	else
		os = "(unknown os)";

	ret = xstrdup(os);
	return ret;
}

/*
 * Convert an os_type to a string
 */
int e2p_string2os(char *str)
{
	const char *const *cpp;
	int i = 0;

	for (cpp = os_tab; *cpp; cpp++, i++) {
		if (!strcasecmp(str, *cpp))
			return i;
	}
	return -1;
}

#ifdef TEST_PROGRAM
int main(int argc, char **argv)
{
	char	*s;
	int	i, os;

	for (i=0; i <= EXT2_OS_LITES; i++) {
		s = e2p_os2string(i);
		os = e2p_string2os(s);
		printf("%d: %s (%d)\n", i, s, os);
		if (i != os) {
			fprintf(stderr, "Failure!\n");
			exit(1);
		}
	}
	exit(0);
}
#endif
