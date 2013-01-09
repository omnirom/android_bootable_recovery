/*
	utils.c (04.09.09)
	exFAT file system implementation library.

	Copyright (C) 2010-2012  Andrew Nayenko

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

#include "exfat.h"
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

void exfat_stat(const struct exfat* ef, const struct exfat_node* node,
		struct stat* stbuf)
{
	memset(stbuf, 0, sizeof(struct stat));
	if (node->flags & EXFAT_ATTRIB_DIR)
		stbuf->st_mode = S_IFDIR | (0777 & ~ef->dmask);
	else
		stbuf->st_mode = S_IFREG | (0777 & ~ef->fmask);
	stbuf->st_nlink = 1;
	stbuf->st_uid = ef->uid;
	stbuf->st_gid = ef->gid;
	stbuf->st_size = node->size;
	stbuf->st_blocks = DIV_ROUND_UP(node->size, CLUSTER_SIZE(*ef->sb)) *
		CLUSTER_SIZE(*ef->sb) / 512;
	stbuf->st_mtime = node->mtime;
	stbuf->st_atime = node->atime;
	/* set ctime to mtime to ensure we don't break programs that rely on ctime
	   (e.g. rsync) */
	stbuf->st_ctime = node->mtime;
}

void exfat_get_name(const struct exfat_node* node, char* buffer, size_t n)
{
	if (utf16_to_utf8(buffer, node->name, n, EXFAT_NAME_MAX) != 0)
		exfat_bug("failed to convert name to UTF-8");
}

uint16_t exfat_start_checksum(const struct exfat_entry_meta1* entry)
{
	uint16_t sum = 0;
	int i;

	for (i = 0; i < sizeof(struct exfat_entry); i++)
		if (i != 2 && i != 3) /* skip checksum field itself */
			sum = ((sum << 15) | (sum >> 1)) + ((const uint8_t*) entry)[i];
	return sum;
}

uint16_t exfat_add_checksum(const void* entry, uint16_t sum)
{
	int i;

	for (i = 0; i < sizeof(struct exfat_entry); i++)
		sum = ((sum << 15) | (sum >> 1)) + ((const uint8_t*) entry)[i];
	return sum;
}

le16_t exfat_calc_checksum(const struct exfat_entry_meta1* meta1,
		const struct exfat_entry_meta2* meta2, const le16_t* name)
{
	uint16_t checksum;
	const int name_entries = DIV_ROUND_UP(utf16_length(name), EXFAT_ENAME_MAX);
	int i;

	checksum = exfat_start_checksum(meta1);
	checksum = exfat_add_checksum(meta2, checksum);
	for (i = 0; i < name_entries; i++)
	{
		struct exfat_entry_name name_entry = {EXFAT_ENTRY_FILE_NAME, 0};
		memcpy(name_entry.name, name + i * EXFAT_ENAME_MAX,
				EXFAT_ENAME_MAX * sizeof(le16_t));
		checksum = exfat_add_checksum(&name_entry, checksum);
	}
	return cpu_to_le16(checksum);
}

uint32_t exfat_vbr_start_checksum(const void* sector, size_t size)
{
	size_t i;
	uint32_t sum = 0;

	for (i = 0; i < size; i++)
		/* skip volume_state and allocated_percent fields */
		if (i != 0x6a && i != 0x6b && i != 0x70)
			sum = ((sum << 31) | (sum >> 1)) + ((const uint8_t*) sector)[i];
	return sum;
}

uint32_t exfat_vbr_add_checksum(const void* sector, size_t size, uint32_t sum)
{
	size_t i;

	for (i = 0; i < size; i++)
		sum = ((sum << 31) | (sum >> 1)) + ((const uint8_t*) sector)[i];
	return sum;
}

le16_t exfat_calc_name_hash(const struct exfat* ef, const le16_t* name)
{
	size_t i;
	size_t length = utf16_length(name);
	uint16_t hash = 0;

	for (i = 0; i < length; i++)
	{
		uint16_t c = le16_to_cpu(name[i]);

		/* convert to upper case */
		if (c < ef->upcase_chars)
			c = le16_to_cpu(ef->upcase[c]);

		hash = ((hash << 15) | (hash >> 1)) + (c & 0xff);
		hash = ((hash << 15) | (hash >> 1)) + (c >> 8);
	}
	return cpu_to_le16(hash);
}

void exfat_humanize_bytes(uint64_t value, struct exfat_human_bytes* hb)
{
	size_t i;
	/* 16 EB (minus 1 byte) is the largest size that can be represented by
	   uint64_t */
	const char* units[] = {"bytes", "KB", "MB", "GB", "TB", "PB", "EB"};
	uint64_t divisor = 1;
	uint64_t temp = 0;

	for (i = 0; ; i++, divisor *= 1024)
	{
		temp = (value + divisor / 2) / divisor;

		if (temp == 0)
			break;
		if (temp / 1024 * 1024 == temp)
			continue;
		if (temp < 10240)
			break;
	}
	hb->value = temp;
	hb->unit = units[i];
}

void exfat_print_info(const struct exfat_super_block* sb,
		uint32_t free_clusters)
{
	struct exfat_human_bytes hb;
	off64_t total_space = le64_to_cpu(sb->sector_count) * SECTOR_SIZE(*sb);
	off64_t avail_space = (off64_t) free_clusters * CLUSTER_SIZE(*sb);

	printf("File system version           %hhu.%hhu\n",
			sb->version.major, sb->version.minor);
	exfat_humanize_bytes(SECTOR_SIZE(*sb), &hb);
	printf("Sector size          %10"PRIu64" %s\n", hb.value, hb.unit);
	exfat_humanize_bytes(CLUSTER_SIZE(*sb), &hb);
	printf("Cluster size         %10"PRIu64" %s\n", hb.value, hb.unit);
	exfat_humanize_bytes(total_space, &hb);
	printf("Volume size          %10"PRIu64" %s\n", hb.value, hb.unit);
	exfat_humanize_bytes(total_space - avail_space, &hb);
	printf("Used space           %10"PRIu64" %s\n", hb.value, hb.unit);
	exfat_humanize_bytes(avail_space, &hb);
	printf("Available space      %10"PRIu64" %s\n", hb.value, hb.unit);
}
