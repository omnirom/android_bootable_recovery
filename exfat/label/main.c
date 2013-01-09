/*
	main.c (20.01.11)
	Prints or changes exFAT volume label.

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

#include <stdio.h>
#include <string.h>
#include <exfat.h>

int main(int argc, char* argv[])
{
	char** pp;
	struct exfat ef;
	int rc = 0;

	for (pp = argv + 1; *pp; pp++)
		if (strcmp(*pp, "-v") == 0)
		{
			printf("exfatlabel %u.%u.%u\n", EXFAT_VERSION_MAJOR,
					EXFAT_VERSION_MINOR, EXFAT_VERSION_PATCH);
			puts("Copyright (C) 2011, 2012  Andrew Nayenko");
			return 0;
		}

	if (argc != 2 && argc != 3)
	{
		fprintf(stderr, "Usage: %s [-v] <device> [label]\n", argv[0]);
		return 1;
	}

	if (argv[2])
	{
		if (exfat_mount(&ef, argv[1], "") != 0)
			return 1;
		rc = (exfat_set_label(&ef, argv[2]) != 0);
	}
	else
	{
		if (exfat_mount(&ef, argv[1], "ro") != 0)
			return 1;
		puts(exfat_get_label(&ef));
	}

	exfat_unmount(&ef);
	return rc;
}
