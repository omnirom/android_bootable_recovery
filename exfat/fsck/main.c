/*
	main.c (02.09.09)
	exFAT file system checker.

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

#include <stdio.h>
#include <string.h>
#include <exfat.h>
#include <exfatfs.h>
#include <inttypes.h>

#define exfat_debug(format, ...)

uint64_t files_count, directories_count;

static int nodeck(struct exfat* ef, struct exfat_node* node)
{
	const cluster_t cluster_size = CLUSTER_SIZE(*ef->sb);
	cluster_t clusters = (node->size + cluster_size - 1) / cluster_size;
	cluster_t c = node->start_cluster;
	int rc = 0;

	while (clusters--)
	{
		if (CLUSTER_INVALID(c))
		{
			char name[EXFAT_NAME_MAX + 1];

			exfat_get_name(node, name, EXFAT_NAME_MAX);
			exfat_error("file `%s' has invalid cluster 0x%x", name, c);
			rc = 1;
			break;
		}
		if (BMAP_GET(ef->cmap.chunk, c - EXFAT_FIRST_DATA_CLUSTER) == 0)
		{
			char name[EXFAT_NAME_MAX + 1];

			exfat_get_name(node, name, EXFAT_NAME_MAX);
			exfat_error("cluster 0x%x of file `%s' is not allocated", c, name);
			rc = 1;
		}
		c = exfat_next_cluster(ef, node, c);
	}
	return rc;
}

static void dirck(struct exfat* ef, const char* path)
{
	struct exfat_node* parent;
	struct exfat_node* node;
	struct exfat_iterator it;
	int rc;
	size_t path_length;
	char* entry_path;

	if (exfat_lookup(ef, &parent, path) != 0)
		exfat_bug("directory `%s' is not found", path);
	if (!(parent->flags & EXFAT_ATTRIB_DIR))
		exfat_bug("`%s' is not a directory (0x%x)", path, parent->flags);
	if (nodeck(ef, parent) != 0)
		return;

	path_length = strlen(path);
	entry_path = malloc(path_length + 1 + EXFAT_NAME_MAX);
	if (entry_path == NULL)
	{
		exfat_error("out of memory");
		return;
	}
	strcpy(entry_path, path);
	strcat(entry_path, "/");

	rc = exfat_opendir(ef, parent, &it);
	if (rc != 0)
	{
		free(entry_path);
		exfat_put_node(ef, parent);
		exfat_error("failed to open directory `%s'", path);
		return;
	}
	while ((node = exfat_readdir(ef, &it)))
	{
		exfat_get_name(node, entry_path + path_length + 1, EXFAT_NAME_MAX);
		exfat_debug("%s: %s, %"PRIu64" bytes, cluster %u", entry_path,
				IS_CONTIGUOUS(*node) ? "contiguous" : "fragmented",
				node->size, node->start_cluster);
		if (node->flags & EXFAT_ATTRIB_DIR)
		{
			directories_count++;
			dirck(ef, entry_path);
		}
		else
		{
			files_count++;
			nodeck(ef, node);
		}
		exfat_put_node(ef, node);
	}
	exfat_closedir(ef, &it);
	exfat_put_node(ef, parent);
	free(entry_path);
}

static void fsck(struct exfat* ef)
{
	exfat_print_info(ef->sb, exfat_count_free_clusters(ef));
	dirck(ef, "");
}

static void usage(const char* prog)
{
	fprintf(stderr, "Usage: %s [-v] <device>\n", prog);
	exit(1);
}

int main(int argc, char* argv[])
{
	char** pp;
	const char* spec = NULL;
	struct exfat ef;

	printf("exfatfsck %u.%u.%u\n",
			EXFAT_VERSION_MAJOR, EXFAT_VERSION_MINOR, EXFAT_VERSION_PATCH);

	for (pp = argv + 1; *pp; pp++)
	{
		if (strcmp(*pp, "-v") == 0)
		{
			puts("Copyright (C) 2011-2013  Andrew Nayenko");
			return 0;
		}
		else if (spec == NULL)
			spec = *pp;
		else
			usage(argv[0]);
	}
	if (spec == NULL)
		usage(argv[0]);

	if (exfat_mount(&ef, spec, "ro") != 0)
		return 1;

	printf("Checking file system on %s.\n", spec);
	fsck(&ef);
	exfat_unmount(&ef);
	printf("Totally %"PRIu64" directories and %"PRIu64" files.\n",
			directories_count, files_count);

	fputs("File system checking finished. ", stdout);
	if (exfat_errors != 0)
	{
		printf("ERRORS FOUND: %d.\n", exfat_errors);
		return 1;
	}
	puts("No errors found.");
	return 0;
}
