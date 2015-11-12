/* fsck.fat.c - User interface

   Copyright (C) 1993 Werner Almesberger <werner.almesberger@lrc.di.epfl.ch>
   Copyright (C) 1998 Roman Hodek <Roman.Hodek@informatik.uni-erlangen.de>
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

/* FAT32, VFAT, Atari format support, and various fixes additions May 1998
 * by Roman Hodek <Roman.Hodek@informatik.uni-erlangen.de> */

#include "version.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>

#include "common.h"
#include "fsck.fat.h"
#include "io.h"
#include "boot.h"
#include "fat.h"
#include "file.h"
#include "check.h"

int interactive = 0, rw = 0, list = 0, test = 0, verbose = 0, write_immed = 0;
int atari_format = 0, boot_only = 0;
unsigned n_files = 0;
void *mem_queue = NULL;

static void usage(char *name)
{
    fprintf(stderr, "usage: %s [-aAbflrtvVwy] [-d path -d ...] "
	    "[-u path -u ...]\n%15sdevice\n", name, "");
    fprintf(stderr, "  -a       automatically repair the filesystem\n");
    fprintf(stderr, "  -A       toggle Atari filesystem format\n");
    fprintf(stderr, "  -b       make read-only boot sector check\n");
    fprintf(stderr, "  -d path  drop that file\n");
    fprintf(stderr, "  -f       salvage unused chains to files\n");
    fprintf(stderr, "  -l       list path names\n");
    fprintf(stderr,
	    "  -n       no-op, check non-interactively without changing\n");
    fprintf(stderr, "  -p       same as -a, for compat with other *fsck\n");
    fprintf(stderr, "  -r       interactively repair the filesystem (default)\n");
    fprintf(stderr, "  -t       test for bad clusters\n");
    fprintf(stderr, "  -u path  try to undelete that (non-directory) file\n");
    fprintf(stderr, "  -v       verbose mode\n");
    fprintf(stderr, "  -V       perform a verification pass\n");
    fprintf(stderr, "  -w       write changes to disk immediately\n");
    fprintf(stderr, "  -y       same as -a, for compat with other *fsck\n");
    exit(2);
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

int main(int argc, char **argv)
{
    DOS_FS fs;
    int salvage_files, verify, c;
    uint32_t free_clusters = 0;

    memset(&fs, 0, sizeof(fs));
    salvage_files = verify = 0;
    rw = interactive = 1;
    check_atari();

    while ((c = getopt(argc, argv, "Aad:bflnprtu:vVwy")) != -1)
	switch (c) {
	case 'A':		/* toggle Atari format */
	    atari_format = !atari_format;
	    break;
	case 'a':
	case 'p':
	case 'y':
	    rw = 1;
	    interactive = 0;
	    salvage_files = 1;
	    break;
	case 'b':
	    rw = 0;
	    interactive = 0;
	    boot_only = 1;
	    break;
	case 'd':
	    file_add(optarg, fdt_drop);
	    break;
	case 'f':
	    salvage_files = 1;
	    break;
	case 'l':
	    list = 1;
	    break;
	case 'n':
	    rw = 0;
	    interactive = 0;
	    break;
	case 'r':
	    rw = 1;
	    interactive = 1;
	    break;
	case 't':
	    test = 1;
	    break;
	case 'u':
	    file_add(optarg, fdt_undelete);
	    break;
	case 'v':
	    verbose = 1;
	    break;
	case 'V':
	    verify = 1;
	    break;
	case 'w':
	    write_immed = 1;
	    break;
	default:
	    usage(argv[0]);
	}
    if ((test || write_immed) && !rw) {
	fprintf(stderr, "-t and -w can not be used in read only mode\n");
	exit(2);
    }
    if (optind != argc - 1)
	usage(argv[0]);

    printf("fsck.fat " VERSION " (" VERSION_DATE ")\n");
    fs_open(argv[optind], rw);

    read_boot(&fs);
    if (boot_only)
	goto exit;

    if (verify)
	printf("Starting check/repair pass.\n");
    while (read_fat(&fs), scan_root(&fs))
	qfree(&mem_queue);
    if (test)
	fix_bad(&fs);
    if (salvage_files)
	reclaim_file(&fs);
    else
	reclaim_free(&fs);
    free_clusters = update_free(&fs);
    file_unused();
    qfree(&mem_queue);
    if (verify) {
	n_files = 0;
	printf("Starting verification pass.\n");
	read_fat(&fs);
	scan_root(&fs);
	reclaim_free(&fs);
	qfree(&mem_queue);
    }

exit:
    if (fs_changed()) {
	if (rw) {
	    if (interactive)
		rw = get_key("yn", "Perform changes ? (y/n)") == 'y';
	    else
		printf("Performing changes.\n");
	} else
	    printf("Leaving filesystem unchanged.\n");
    }

    if (!boot_only)
	printf("%s: %u files, %lu/%lu clusters\n", argv[optind],
	       n_files, (unsigned long)fs.clusters - free_clusters, (unsigned long)fs.clusters);

    return fs_close(rw) ? 1 : 0;
}
