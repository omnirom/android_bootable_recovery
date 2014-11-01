/*
	main.c (08.11.10)
	Prints detailed information about exFAT volume.

	Free exFAT implementation.
	Copyright (C) 2011-2013  Andrew Nayenko

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License along
	with this program; if not, write to the Free Software Foundation, Inc.,
	51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include <fcntl.h>
#include <unistd.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <exfat.h>

static void print_generic_info(const struct exfat_super_block* sb)
{
	printf("Volume serial number      0x%08x\n",
			le32_to_cpu(sb->volume_serial));
	printf("FS version                       %hhu.%hhu\n",
			sb->version.major, sb->version.minor);
	printf("Sector size               %10u\n",
			SECTOR_SIZE(*sb));
	printf("Cluster size              %10u\n",
			CLUSTER_SIZE(*sb));
}

static void print_sector_info(const struct exfat_super_block* sb)
{
	printf("Sectors count             %10"PRIu64"\n",
			le64_to_cpu(sb->sector_count));
}

static void print_cluster_info(const struct exfat_super_block* sb)
{
	printf("Clusters count            %10u\n",
			le32_to_cpu(sb->cluster_count));
}

static void print_other_info(const struct exfat_super_block* sb)
{
	printf("First sector              %10"PRIu64"\n",
			le64_to_cpu(sb->sector_start));
	printf("FAT first sector          %10u\n",
			le32_to_cpu(sb->fat_sector_start));
	printf("FAT sectors count         %10u\n",
			le32_to_cpu(sb->fat_sector_count));
	printf("First cluster sector      %10u\n",
			le32_to_cpu(sb->cluster_sector_start));
	printf("Root directory cluster    %10u\n",
			le32_to_cpu(sb->rootdir_cluster));
	printf("Volume state                  0x%04hx\n",
			le16_to_cpu(sb->volume_state));
	printf("FATs count                %10hhu\n",
			sb->fat_count);
	printf("Drive number                    0x%02hhx\n",
			sb->drive_no);
	printf("Allocated space           %9hhu%%\n",
			sb->allocated_percent);
}

static int dump_sb(const char* spec)
{
	struct exfat_dev* dev;
	struct exfat_super_block sb;

	dev = exfat_open(spec, EXFAT_MODE_RO);
	if (dev == NULL)
		return 1;

	if (exfat_read(dev, &sb, sizeof(struct exfat_super_block)) < 0)
	{
		exfat_close(dev);
		exfat_error("failed to read from `%s'", spec);
		return 1;
	}
	if (memcmp(sb.oem_name, "EXFAT   ", sizeof(sb.oem_name)) != 0)
	{
		exfat_close(dev);
		exfat_error("exFAT file system is not found on `%s'", spec);
		return 1;
	}

	print_generic_info(&sb);
	print_sector_info(&sb);
	print_cluster_info(&sb);
	print_other_info(&sb);

	exfat_close(dev);
	return 0;
}

static void dump_sectors(struct exfat* ef)
{
	off_t a = 0, b = 0;

	printf("Used sectors ");
	while (exfat_find_used_sectors(ef, &a, &b) == 0)
		printf(" %"PRIu64"-%"PRIu64, a, b);
	puts("");
}

static int dump_full(const char* spec, bool used_sectors)
{
	struct exfat ef;
	uint32_t free_clusters;
	uint64_t free_sectors;

	if (exfat_mount(&ef, spec, "ro") != 0)
		return 1;

	free_clusters = exfat_count_free_clusters(&ef);
	free_sectors = (uint64_t) free_clusters << ef.sb->spc_bits;

	printf("Volume label         %15s\n", exfat_get_label(&ef));
	print_generic_info(ef.sb);
	print_sector_info(ef.sb);
	printf("Free sectors              %10"PRIu64"\n", free_sectors);
	print_cluster_info(ef.sb);
	printf("Free clusters             %10u\n", free_clusters);
	print_other_info(ef.sb);
	if (used_sectors)
		dump_sectors(&ef);

	exfat_unmount(&ef);
	return 0;
}

static void usage(const char* prog)
{
	fprintf(stderr, "Usage: %s [-s] [-u] [-V] <device>\n", prog);
	exit(1);
}

int main(int argc, char* argv[])
{
	int opt;
	const char* spec = NULL;
	bool sb_only = false;
	bool used_sectors = false;

	printf("dumpexfat %u.%u.%u\n",
			EXFAT_VERSION_MAJOR, EXFAT_VERSION_MINOR, EXFAT_VERSION_PATCH);

	while ((opt = getopt(argc, argv, "suV")) != -1)
	{
		switch (opt)
		{
		case 's':
			sb_only = true;
			break;
		case 'u':
			used_sectors = true;
			break;
		case 'V':
			puts("Copyright (C) 2011-2013  Andrew Nayenko");
			return 0;
		default:
			usage(argv[0]);
		}
	}
	if (argc - optind != 1)
		usage(argv[0]);
	spec = argv[optind];

	if (sb_only)
		return dump_sb(spec);

	return dump_full(spec, used_sectors);
}
