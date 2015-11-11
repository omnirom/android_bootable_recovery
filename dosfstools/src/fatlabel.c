/* fatlabel.c - User interface

   Copyright (C) 1993 Werner Almesberger <werner.almesberger@lrc.di.epfl.ch>
   Copyright (C) 1998 Roman Hodek <Roman.Hodek@informatik.uni-erlangen.de>
   Copyright (C) 2007 Red Hat, Inc.
   Copyright (C) 2008-2014 Daniel Baumann <mail@daniel-baumann.ch>

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program. If not, see <http://www.gnu.org/licenses/>.

   The complete text of the GNU General Public License
   can be found in /usr/share/common-licenses/GPL-3 file.
*/

#include "version.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <ctype.h>

#include "common.h"
#include "fsck.fat.h"
#include "io.h"
#include "boot.h"
#include "fat.h"
#include "file.h"
#include "check.h"

int interactive = 0, rw = 0, list = 0, test = 0, verbose = 0, write_immed = 0;
int atari_format = 0;
unsigned n_files = 0;
void *mem_queue = NULL;

static void usage(int error)
{
    FILE *f = error ? stderr : stdout;
    int status = error ? 1 : 0;

    fprintf(f, "usage: fatlabel device [label]\n");
    exit(status);
}

/*
 * ++roman: On m68k, check if this is an Atari; if yes, turn on Atari variant
 * of MS-DOS filesystem by default.
 */
static void check_atari(void)
{
#ifdef __mc68000__
    FILE *f;
    char line[128], *p;

    if (!(f = fopen("/proc/hardware", "r"))) {
	perror("/proc/hardware");
	return;
    }

    while (fgets(line, sizeof(line), f)) {
	if (strncmp(line, "Model:", 6) == 0) {
	    p = line + 6;
	    p += strspn(p, " \t");
	    if (strncmp(p, "Atari ", 6) == 0)
		atari_format = 1;
	    break;
	}
    }
    fclose(f);
#endif
}

int main(int argc, char *argv[])
{
    DOS_FS fs = { 0 };
    rw = 0;

    int i;

    char *device = NULL;
    char label[12] = { 0 };

    loff_t offset;
    DIR_ENT de;

    check_atari();

    if (argc < 2 || argc > 3)
	usage(1);

    if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))
	usage(0);
    else if (!strcmp(argv[1], "-V") || !strcmp(argv[1], "--version")) {
	printf("fatlabel " VERSION " (" VERSION_DATE ")\n");
	exit(0);
    }

    device = argv[1];
    if (argc == 3) {
	strncpy(label, argv[2], 11);
	if (strlen(argv[2]) > 11) {
	    fprintf(stderr,
		    "fatlabel: labels can be no longer than 11 characters\n");
	    exit(1);
	}
	for (i = 0; label[i] && i < 11; i++)
	    /* don't know if here should be more strict !uppercase(label[i]) */
	    if (islower(label[i])) {
		fprintf(stderr,
			"fatlabel: warning - lowercase labels might not work properly with DOS or Windows\n");
		break;
	    }
	rw = 1;
    }

    fs_open(device, rw);
    read_boot(&fs);
    if (fs.fat_bits == 32)
	read_fat(&fs);
    if (!rw) {
	offset = find_volume_de(&fs, &de);
	if (offset == 0)
	    fprintf(stdout, "%s\n", fs.label);
	else
	    fprintf(stdout, "%.8s%.3s\n", de.name, de.ext);
	exit(0);
    }

    write_label(&fs, label);
    fs_close(rw);
    return 0;
}
