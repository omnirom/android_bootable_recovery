/*
	node.c (09.10.09)
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
#include <errno.h>
#include <string.h>
#include <inttypes.h>

/* on-disk nodes iterator */
struct iterator
{
	cluster_t cluster;
	off64_t offset;
	int contiguous;
	char* chunk;
};

struct exfat_node* exfat_get_node(struct exfat_node* node)
{
	/* if we switch to multi-threaded mode we will need atomic
	   increment here and atomic decrement in exfat_put_node() */
	node->references++;
	return node;
}

void exfat_put_node(struct exfat* ef, struct exfat_node* node)
{
	if (--node->references < 0)
	{
		char buffer[EXFAT_NAME_MAX + 1];
		exfat_get_name(node, buffer, EXFAT_NAME_MAX);
		exfat_bug("reference counter of `%s' is below zero", buffer);
	}

	if (node->references == 0)
	{
		if (node->flags & EXFAT_ATTRIB_DIRTY)
			exfat_flush_node(ef, node);
		if (node->flags & EXFAT_ATTRIB_UNLINKED)
		{
			/* free all clusters and node structure itself */
			exfat_truncate(ef, node, 0);
			free(node);
		}
		if (ef->cmap.dirty)
			exfat_flush_cmap(ef);
	}
}

/**
 * Cluster + offset from the beginning of the directory to absolute offset.
 */
static off64_t co2o(struct exfat* ef, cluster_t cluster, off64_t offset)
{
	return exfat_c2o(ef, cluster) + offset % CLUSTER_SIZE(*ef->sb);
}

static int opendir(struct exfat* ef, const struct exfat_node* dir,
		struct iterator* it)
{
	if (!(dir->flags & EXFAT_ATTRIB_DIR))
		exfat_bug("not a directory");
	it->cluster = dir->start_cluster;
	it->offset = 0;
	it->contiguous = IS_CONTIGUOUS(*dir);
	it->chunk = malloc(CLUSTER_SIZE(*ef->sb));
	if (it->chunk == NULL)
	{
		exfat_error("out of memory");
		return -ENOMEM;
	}
	exfat_pread(ef->dev, it->chunk, CLUSTER_SIZE(*ef->sb),
			exfat_c2o(ef, it->cluster));
	return 0;
}

static void closedir(struct iterator* it)
{
	it->cluster = 0;
	it->offset = 0;
	it->contiguous = 0;
	free(it->chunk);
	it->chunk = NULL;
}

static int fetch_next_entry(struct exfat* ef, const struct exfat_node* parent,
		struct iterator* it)
{
	/* move iterator to the next entry in the directory */
	it->offset += sizeof(struct exfat_entry);
	/* fetch the next cluster if needed */
	if ((it->offset & (CLUSTER_SIZE(*ef->sb) - 1)) == 0)
	{
		/* reached the end of directory; the caller should check this
		   condition too */
		if (it->offset >= parent->size)
			return 0;
		it->cluster = exfat_next_cluster(ef, parent, it->cluster);
		if (CLUSTER_INVALID(it->cluster))
		{
			exfat_error("invalid cluster 0x%x while reading directory",
					it->cluster);
			return 1;
		}
		exfat_pread(ef->dev, it->chunk, CLUSTER_SIZE(*ef->sb),
				exfat_c2o(ef, it->cluster));
	}
	return 0;
}

static struct exfat_node* allocate_node(void)
{
	struct exfat_node* node = malloc(sizeof(struct exfat_node));
	if (node == NULL)
	{
		exfat_error("failed to allocate node");
		return NULL;
	}
	memset(node, 0, sizeof(struct exfat_node));
	return node;
}

static void init_node_meta1(struct exfat_node* node,
		const struct exfat_entry_meta1* meta1)
{
	node->flags = le16_to_cpu(meta1->attrib);
	node->mtime = exfat_exfat2unix(meta1->mdate, meta1->mtime,
			meta1->mtime_cs);
	/* there is no centiseconds field for atime */
	node->atime = exfat_exfat2unix(meta1->adate, meta1->atime, 0);
}

static void init_node_meta2(struct exfat_node* node,
		const struct exfat_entry_meta2* meta2)
{
	node->size = le64_to_cpu(meta2->size);
	node->start_cluster = le32_to_cpu(meta2->start_cluster);
	node->fptr_cluster = node->start_cluster;
	if (meta2->flags & EXFAT_FLAG_CONTIGUOUS)
		node->flags |= EXFAT_ATTRIB_CONTIGUOUS;
}

static const struct exfat_entry* get_entry_ptr(const struct exfat* ef,
		const struct iterator* it)
{
	return (const struct exfat_entry*)
			(it->chunk + it->offset % CLUSTER_SIZE(*ef->sb));
}

/*
 * Reads one entry in directory at position pointed by iterator and fills
 * node structure.
 */
static int readdir(struct exfat* ef, const struct exfat_node* parent,
		struct exfat_node** node, struct iterator* it)
{
	int rc = -EIO;
	const struct exfat_entry* entry;
	const struct exfat_entry_meta1* meta1;
	const struct exfat_entry_meta2* meta2;
	const struct exfat_entry_name* file_name;
	const struct exfat_entry_upcase* upcase;
	const struct exfat_entry_bitmap* bitmap;
	const struct exfat_entry_label* label;
	uint8_t continuations = 0;
	le16_t* namep = NULL;
	uint16_t reference_checksum = 0;
	uint16_t actual_checksum = 0;
	uint64_t real_size = 0;

	*node = NULL;

	for (;;)
	{
		if (it->offset >= parent->size)
		{
			if (continuations != 0)
			{
				exfat_error("expected %hhu continuations", continuations);
				goto error;
			}
			return -ENOENT; /* that's OK, means end of directory */
		}

		entry = get_entry_ptr(ef, it);
		switch (entry->type)
		{
		case EXFAT_ENTRY_FILE:
			if (continuations != 0)
			{
				exfat_error("expected %hhu continuations before new entry",
						continuations);
				goto error;
			}
			meta1 = (const struct exfat_entry_meta1*) entry;
			continuations = meta1->continuations;
			/* each file entry must have at least 2 continuations:
			   info and name */
			if (continuations < 2)
			{
				exfat_error("too few continuations (%hhu)", continuations);
				goto error;
			}
			reference_checksum = le16_to_cpu(meta1->checksum);
			actual_checksum = exfat_start_checksum(meta1);
			*node = allocate_node();
			if (*node == NULL)
			{
				rc = -ENOMEM;
				goto error;
			}
			/* new node has zero reference counter */
			(*node)->entry_cluster = it->cluster;
			(*node)->entry_offset = it->offset;
			init_node_meta1(*node, meta1);
			namep = (*node)->name;
			break;

		case EXFAT_ENTRY_FILE_INFO:
			if (continuations < 2)
			{
				exfat_error("unexpected continuation (%hhu)",
						continuations);
				goto error;
			}
			meta2 = (const struct exfat_entry_meta2*) entry;
			if (meta2->flags & ~(EXFAT_FLAG_ALWAYS1 | EXFAT_FLAG_CONTIGUOUS))
			{
				exfat_error("unknown flags in meta2 (0x%hhx)", meta2->flags);
				goto error;
			}
			init_node_meta2(*node, meta2);
			actual_checksum = exfat_add_checksum(entry, actual_checksum);
			real_size = le64_to_cpu(meta2->real_size);
			/* empty files must be marked as non-contiguous */
			if ((*node)->size == 0 && (meta2->flags & EXFAT_FLAG_CONTIGUOUS))
			{
				exfat_error("empty file marked as contiguous (0x%hhx)",
						meta2->flags);
				goto error;
			}
			/* directories must be aligned on at cluster boundary */
			if (((*node)->flags & EXFAT_ATTRIB_DIR) &&
				(*node)->size % CLUSTER_SIZE(*ef->sb) != 0)
			{
				exfat_error("directory has invalid size %"PRIu64" bytes",
						(*node)->size);
				goto error;
			}
			--continuations;
			break;

		case EXFAT_ENTRY_FILE_NAME:
			if (continuations == 0)
			{
				exfat_error("unexpected continuation");
				goto error;
			}
			file_name = (const struct exfat_entry_name*) entry;
			actual_checksum = exfat_add_checksum(entry, actual_checksum);

			memcpy(namep, file_name->name, EXFAT_ENAME_MAX * sizeof(le16_t));
			namep += EXFAT_ENAME_MAX;
			if (--continuations == 0)
			{
				/*
				   There are two fields that contain file size. Maybe they
				   plan to add compression support in the future and one of
				   those fields is visible (uncompressed) size and the other
				   is real (compressed) size. Anyway, currently it looks like
				   exFAT does not support compression and both fields must be
				   equal.

				   There is an exception though: pagefile.sys (its real_size
				   is always 0).
				*/
				if (real_size != (*node)->size)
				{
					char buffer[EXFAT_NAME_MAX + 1];

					exfat_get_name(*node, buffer, EXFAT_NAME_MAX);
					exfat_error("`%s' real size does not equal to size "
							"(%"PRIu64" != %"PRIu64")", buffer,
							real_size, (*node)->size);
					goto error;
				}
				if (actual_checksum != reference_checksum)
				{
					char buffer[EXFAT_NAME_MAX + 1];

					exfat_get_name(*node, buffer, EXFAT_NAME_MAX);
					exfat_error("`%s' has invalid checksum (0x%hx != 0x%hx)",
							buffer, actual_checksum, reference_checksum);
					goto error;
				}
				if (fetch_next_entry(ef, parent, it) != 0)
					goto error;
				return 0; /* entry completed */
			}
			break;

		case EXFAT_ENTRY_UPCASE:
			if (ef->upcase != NULL)
				break;
			upcase = (const struct exfat_entry_upcase*) entry;
			if (CLUSTER_INVALID(le32_to_cpu(upcase->start_cluster)))
			{
				exfat_error("invalid cluster 0x%x in upcase table",
						le32_to_cpu(upcase->start_cluster));
				goto error;
			}
			if (le64_to_cpu(upcase->size) == 0 ||
				le64_to_cpu(upcase->size) > 0xffff * sizeof(uint16_t) ||
				le64_to_cpu(upcase->size) % sizeof(uint16_t) != 0)
			{
				exfat_error("bad upcase table size (%"PRIu64" bytes)",
						le64_to_cpu(upcase->size));
				goto error;
			}
			ef->upcase = malloc(le64_to_cpu(upcase->size));
			if (ef->upcase == NULL)
			{
				exfat_error("failed to allocate upcase table (%"PRIu64" bytes)",
						le64_to_cpu(upcase->size));
				rc = -ENOMEM;
				goto error;
			}
			ef->upcase_chars = le64_to_cpu(upcase->size) / sizeof(le16_t);

			exfat_pread(ef->dev, ef->upcase, le64_to_cpu(upcase->size),
					exfat_c2o(ef, le32_to_cpu(upcase->start_cluster)));
			break;

		case EXFAT_ENTRY_BITMAP:
			bitmap = (const struct exfat_entry_bitmap*) entry;
			ef->cmap.start_cluster = le32_to_cpu(bitmap->start_cluster);
			if (CLUSTER_INVALID(ef->cmap.start_cluster))
			{
				exfat_error("invalid cluster 0x%x in clusters bitmap",
						ef->cmap.start_cluster);
				goto error;
			}
			ef->cmap.size = le32_to_cpu(ef->sb->cluster_count) -
				EXFAT_FIRST_DATA_CLUSTER;
			if (le64_to_cpu(bitmap->size) < (ef->cmap.size + 7) / 8)
			{
				exfat_error("invalid clusters bitmap size: %"PRIu64
						" (expected at least %u)",
						le64_to_cpu(bitmap->size), (ef->cmap.size + 7) / 8);
				goto error;
			}
			/* FIXME bitmap can be rather big, up to 512 MB */
			ef->cmap.chunk_size = ef->cmap.size;
			ef->cmap.chunk = malloc(le64_to_cpu(bitmap->size));
			if (ef->cmap.chunk == NULL)
			{
				exfat_error("failed to allocate clusters bitmap chunk "
						"(%"PRIu64" bytes)", le64_to_cpu(bitmap->size));
				rc = -ENOMEM;
				goto error;
			}

			exfat_pread(ef->dev, ef->cmap.chunk, le64_to_cpu(bitmap->size),
					exfat_c2o(ef, ef->cmap.start_cluster));
			break;

		case EXFAT_ENTRY_LABEL:
			label = (const struct exfat_entry_label*) entry;
			if (label->length > EXFAT_ENAME_MAX)
			{
				exfat_error("too long label (%hhu chars)", label->length);
				goto error;
			}
			if (utf16_to_utf8(ef->label, label->name,
						sizeof(ef->label), EXFAT_ENAME_MAX) != 0)
				goto error;
			break;

		default:
			if (entry->type & EXFAT_ENTRY_VALID)
			{
				exfat_error("unknown entry type 0x%hhx", entry->type);
				goto error;
			}
			break;
		}

		if (fetch_next_entry(ef, parent, it) != 0)
			goto error;
	}
	/* we never reach here */

error:
	free(*node);
	*node = NULL;
	return rc;
}

int exfat_cache_directory(struct exfat* ef, struct exfat_node* dir)
{
	struct iterator it;
	int rc;
	struct exfat_node* node;
	struct exfat_node* current = NULL;

	if (dir->flags & EXFAT_ATTRIB_CACHED)
		return 0; /* already cached */

	rc = opendir(ef, dir, &it);
	if (rc != 0)
		return rc;
	while ((rc = readdir(ef, dir, &node, &it)) == 0)
	{
		node->parent = dir;
		if (current != NULL)
		{
			current->next = node;
			node->prev = current;
		}
		else
			dir->child = node;

		current = node;
	}
	closedir(&it);

	if (rc != -ENOENT)
	{
		/* rollback */
		for (current = dir->child; current; current = node)
		{
			node = current->next;
			free(current);
		}
		dir->child = NULL;
		return rc;
	}

	dir->flags |= EXFAT_ATTRIB_CACHED;
	return 0;
}

static void reset_cache(struct exfat* ef, struct exfat_node* node)
{
	struct exfat_node* child;
	struct exfat_node* next;

	for (child = node->child; child; child = next)
	{
		reset_cache(ef, child);
		next = child->next;
		free(child);
	}
	if (node->references != 0)
	{
		char buffer[EXFAT_NAME_MAX + 1];
		exfat_get_name(node, buffer, EXFAT_NAME_MAX);
		exfat_warn("non-zero reference counter (%d) for `%s'",
				node->references, buffer);
	}
	while (node->references--)
		exfat_put_node(ef, node);
	node->child = NULL;
	node->flags &= ~EXFAT_ATTRIB_CACHED;
}

void exfat_reset_cache(struct exfat* ef)
{
	reset_cache(ef, ef->root);
}

void next_entry(struct exfat* ef, const struct exfat_node* parent,
		cluster_t* cluster, off64_t* offset)
{
	*offset += sizeof(struct exfat_entry);
	if (*offset % CLUSTER_SIZE(*ef->sb) == 0)
		/* next cluster cannot be invalid */
		*cluster = exfat_next_cluster(ef, parent, *cluster);
}

void exfat_flush_node(struct exfat* ef, struct exfat_node* node)
{
	cluster_t cluster;
	off64_t offset;
	off64_t meta1_offset, meta2_offset;
	struct exfat_entry_meta1 meta1;
	struct exfat_entry_meta2 meta2;

	if (ef->ro)
		exfat_bug("unable to flush node to read-only FS");

	if (node->parent == NULL)
		return; /* do not flush unlinked node */

	cluster = node->entry_cluster;
	offset = node->entry_offset;
	meta1_offset = co2o(ef, cluster, offset);
	next_entry(ef, node->parent, &cluster, &offset);
	meta2_offset = co2o(ef, cluster, offset);

	exfat_pread(ef->dev, &meta1, sizeof(meta1), meta1_offset);
	if (meta1.type != EXFAT_ENTRY_FILE)
		exfat_bug("invalid type of meta1: 0x%hhx", meta1.type);
	meta1.attrib = cpu_to_le16(node->flags);
	exfat_unix2exfat(node->mtime, &meta1.mdate, &meta1.mtime, &meta1.mtime_cs);
	exfat_unix2exfat(node->atime, &meta1.adate, &meta1.atime, NULL);

	exfat_pread(ef->dev, &meta2, sizeof(meta2), meta2_offset);
	if (meta2.type != EXFAT_ENTRY_FILE_INFO)
		exfat_bug("invalid type of meta2: 0x%hhx", meta2.type);
	meta2.size = meta2.real_size = cpu_to_le64(node->size);
	meta2.start_cluster = cpu_to_le32(node->start_cluster);
	meta2.flags = EXFAT_FLAG_ALWAYS1;
	/* empty files must not be marked as contiguous */
	if (node->size != 0 && IS_CONTIGUOUS(*node))
		meta2.flags |= EXFAT_FLAG_CONTIGUOUS;
	/* name hash remains unchanged, no need to recalculate it */

	meta1.checksum = exfat_calc_checksum(&meta1, &meta2, node->name);

	exfat_pwrite(ef->dev, &meta1, sizeof(meta1), meta1_offset);
	exfat_pwrite(ef->dev, &meta2, sizeof(meta2), meta2_offset);

	node->flags &= ~EXFAT_ATTRIB_DIRTY;
}

static void erase_entry(struct exfat* ef, struct exfat_node* node)
{
	cluster_t cluster = node->entry_cluster;
	off64_t offset = node->entry_offset;
	int name_entries = DIV_ROUND_UP(utf16_length(node->name), EXFAT_ENAME_MAX);
	uint8_t entry_type;

	entry_type = EXFAT_ENTRY_FILE & ~EXFAT_ENTRY_VALID;
	exfat_pwrite(ef->dev, &entry_type, 1, co2o(ef, cluster, offset));

	next_entry(ef, node->parent, &cluster, &offset);
	entry_type = EXFAT_ENTRY_FILE_INFO & ~EXFAT_ENTRY_VALID;
	exfat_pwrite(ef->dev, &entry_type, 1, co2o(ef, cluster, offset));

	while (name_entries--)
	{
		next_entry(ef, node->parent, &cluster, &offset);
		entry_type = EXFAT_ENTRY_FILE_NAME & ~EXFAT_ENTRY_VALID;
		exfat_pwrite(ef->dev, &entry_type, 1, co2o(ef, cluster, offset));
	}
}

static void tree_detach(struct exfat_node* node)
{
	if (node->prev)
		node->prev->next = node->next;
	else /* this is the first node in the list */
		node->parent->child = node->next;
	if (node->next)
		node->next->prev = node->prev;
	node->parent = NULL;
	node->prev = NULL;
	node->next = NULL;
}

static void tree_attach(struct exfat_node* dir, struct exfat_node* node)
{
	node->parent = dir;
	if (dir->child)
	{
		dir->child->prev = node;
		node->next = dir->child;
	}
	dir->child = node;
}

static int shrink_directory(struct exfat* ef, struct exfat_node* dir,
		off64_t deleted_offset)
{
	const struct exfat_node* node;
	const struct exfat_node* last_node;
	uint64_t entries = 0;
	uint64_t new_size;
	int rc;

	if (!(dir->flags & EXFAT_ATTRIB_DIR))
		exfat_bug("attempted to shrink a file");
	if (!(dir->flags & EXFAT_ATTRIB_CACHED))
		exfat_bug("attempted to shrink uncached directory");

	for (last_node = node = dir->child; node; node = node->next)
	{
		if (deleted_offset < node->entry_offset)
		{
			/* there are other entries after the removed one, no way to shrink
			   this directory */
			return 0;
		}
		if (last_node->entry_offset < node->entry_offset)
			last_node = node;
	}

	if (last_node)
	{
		/* offset of the last entry */
		entries += last_node->entry_offset / sizeof(struct exfat_entry);
		/* two subentries with meta info */
		entries += 2;
		/* subentries with file name */
		entries += DIV_ROUND_UP(utf16_length(last_node->name),
				EXFAT_ENAME_MAX);
	}

	new_size = DIV_ROUND_UP(entries * sizeof(struct exfat_entry),
				 CLUSTER_SIZE(*ef->sb)) * CLUSTER_SIZE(*ef->sb);
	if (new_size == 0) /* directory always has at least 1 cluster */
		new_size = CLUSTER_SIZE(*ef->sb);
	if (new_size == dir->size)
		return 0;
	rc = exfat_truncate(ef, dir, new_size);
	if (rc != 0)
		return rc;
	return 0;
}

static int delete(struct exfat* ef, struct exfat_node* node)
{
	struct exfat_node* parent = node->parent;
	off64_t deleted_offset = node->entry_offset;
	int rc;

	exfat_get_node(parent);
	erase_entry(ef, node);
	exfat_update_mtime(parent);
	tree_detach(node);
	rc = shrink_directory(ef, parent, deleted_offset);
	exfat_put_node(ef, parent);
	/* file clusters will be freed when node reference counter becomes 0 */
	node->flags |= EXFAT_ATTRIB_UNLINKED;
	return rc;
}

int exfat_unlink(struct exfat* ef, struct exfat_node* node)
{
	if (node->flags & EXFAT_ATTRIB_DIR)
		return -EISDIR;
	return delete(ef, node);
}

int exfat_rmdir(struct exfat* ef, struct exfat_node* node)
{
	if (!(node->flags & EXFAT_ATTRIB_DIR))
		return -ENOTDIR;
	/* check that directory is empty */
	exfat_cache_directory(ef, node);
	if (node->child)
		return -ENOTEMPTY;
	return delete(ef, node);
}

static int grow_directory(struct exfat* ef, struct exfat_node* dir,
		uint64_t asize, uint32_t difference)
{
	return exfat_truncate(ef, dir,
			DIV_ROUND_UP(asize + difference, CLUSTER_SIZE(*ef->sb))
				* CLUSTER_SIZE(*ef->sb));
}

static int find_slot(struct exfat* ef, struct exfat_node* dir,
		cluster_t* cluster, off64_t* offset, int subentries)
{
	struct iterator it;
	int rc;
	const struct exfat_entry* entry;
	int contiguous = 0;

	rc = opendir(ef, dir, &it);
	if (rc != 0)
		return rc;
	for (;;)
	{
		if (contiguous == 0)
		{
			*cluster = it.cluster;
			*offset = it.offset;
		}
		entry = get_entry_ptr(ef, &it);
		if (entry->type & EXFAT_ENTRY_VALID)
			contiguous = 0;
		else
			contiguous++;
		if (contiguous == subentries)
			break;	/* suitable slot is found */
		if (it.offset + sizeof(struct exfat_entry) >= dir->size)
		{
			rc = grow_directory(ef, dir, dir->size,
					(subentries - contiguous) * sizeof(struct exfat_entry));
			if (rc != 0)
			{
				closedir(&it);
				return rc;
			}
		}
		if (fetch_next_entry(ef, dir, &it) != 0)
		{
			closedir(&it);
			return -EIO;
		}
	}
	closedir(&it);
	return 0;
}

static int write_entry(struct exfat* ef, struct exfat_node* dir,
		const le16_t* name, cluster_t cluster, off64_t offset, uint16_t attrib)
{
	struct exfat_node* node;
	struct exfat_entry_meta1 meta1;
	struct exfat_entry_meta2 meta2;
	const size_t name_length = utf16_length(name);
	const int name_entries = DIV_ROUND_UP(name_length, EXFAT_ENAME_MAX);
	int i;

	node = allocate_node();
	if (node == NULL)
		return -ENOMEM;
	node->entry_cluster = cluster;
	node->entry_offset = offset;
	memcpy(node->name, name, name_length * sizeof(le16_t));

	memset(&meta1, 0, sizeof(meta1));
	meta1.type = EXFAT_ENTRY_FILE;
	meta1.continuations = 1 + name_entries;
	meta1.attrib = cpu_to_le16(attrib);
	exfat_unix2exfat(time(NULL), &meta1.crdate, &meta1.crtime,
			&meta1.crtime_cs);
	meta1.adate = meta1.mdate = meta1.crdate;
	meta1.atime = meta1.mtime = meta1.crtime;
	meta1.mtime_cs = meta1.crtime_cs; /* there is no atime_cs */

	memset(&meta2, 0, sizeof(meta2));
	meta2.type = EXFAT_ENTRY_FILE_INFO;
	meta2.flags = EXFAT_FLAG_ALWAYS1;
	meta2.name_length = name_length;
	meta2.name_hash = exfat_calc_name_hash(ef, node->name);
	meta2.start_cluster = cpu_to_le32(EXFAT_CLUSTER_FREE);

	meta1.checksum = exfat_calc_checksum(&meta1, &meta2, node->name);

	exfat_pwrite(ef->dev, &meta1, sizeof(meta1), co2o(ef, cluster, offset));
	next_entry(ef, dir, &cluster, &offset);
	exfat_pwrite(ef->dev, &meta2, sizeof(meta2), co2o(ef, cluster, offset));
	for (i = 0; i < name_entries; i++)
	{
		struct exfat_entry_name name_entry = {EXFAT_ENTRY_FILE_NAME, 0};
		memcpy(name_entry.name, node->name + i * EXFAT_ENAME_MAX,
				EXFAT_ENAME_MAX * sizeof(le16_t));
		next_entry(ef, dir, &cluster, &offset);
		exfat_pwrite(ef->dev, &name_entry, sizeof(name_entry),
				co2o(ef, cluster, offset));
	}

	init_node_meta1(node, &meta1);
	init_node_meta2(node, &meta2);

	tree_attach(dir, node);
	exfat_update_mtime(dir);
	return 0;
}

static int create(struct exfat* ef, const char* path, uint16_t attrib)
{
	struct exfat_node* dir;
	struct exfat_node* existing;
	cluster_t cluster = EXFAT_CLUSTER_BAD;
	off64_t offset = -1;
	le16_t name[EXFAT_NAME_MAX + 1];
	int rc;

	rc = exfat_split(ef, &dir, &existing, name, path);
	if (rc != 0)
		return rc;
	if (existing != NULL)
	{
		exfat_put_node(ef, existing);
		exfat_put_node(ef, dir);
		return -EEXIST;
	}

	rc = find_slot(ef, dir, &cluster, &offset,
			2 + DIV_ROUND_UP(utf16_length(name), EXFAT_ENAME_MAX));
	if (rc != 0)
	{
		exfat_put_node(ef, dir);
		return rc;
	}
	rc = write_entry(ef, dir, name, cluster, offset, attrib);
	exfat_put_node(ef, dir);
	return rc;
}

int exfat_mknod(struct exfat* ef, const char* path)
{
	return create(ef, path, EXFAT_ATTRIB_ARCH);
}

int exfat_mkdir(struct exfat* ef, const char* path)
{
	int rc;
	struct exfat_node* node;

	rc = create(ef, path, EXFAT_ATTRIB_ARCH | EXFAT_ATTRIB_DIR);
	if (rc != 0)
		return rc;
	rc = exfat_lookup(ef, &node, path);
	if (rc != 0)
		return 0;
	/* directories always have at least one cluster */
	rc = exfat_truncate(ef, node, CLUSTER_SIZE(*ef->sb));
	if (rc != 0)
	{
		delete(ef, node);
		exfat_put_node(ef, node);
		return rc;
	}
	exfat_put_node(ef, node);
	return 0;
}

static void rename_entry(struct exfat* ef, struct exfat_node* dir,
		struct exfat_node* node, const le16_t* name, cluster_t new_cluster,
		off64_t new_offset)
{
	struct exfat_entry_meta1 meta1;
	struct exfat_entry_meta2 meta2;
	cluster_t old_cluster = node->entry_cluster;
	off64_t old_offset = node->entry_offset;
	const size_t name_length = utf16_length(name);
	const int name_entries = DIV_ROUND_UP(name_length, EXFAT_ENAME_MAX);
	int i;

	exfat_pread(ef->dev, &meta1, sizeof(meta1),
			co2o(ef, old_cluster, old_offset));
	next_entry(ef, node->parent, &old_cluster, &old_offset);
	exfat_pread(ef->dev, &meta2, sizeof(meta2),
			co2o(ef, old_cluster, old_offset));
	meta1.continuations = 1 + name_entries;
	meta2.name_hash = exfat_calc_name_hash(ef, name);
	meta2.name_length = name_length;
	meta1.checksum = exfat_calc_checksum(&meta1, &meta2, name);

	erase_entry(ef, node);

	node->entry_cluster = new_cluster;
	node->entry_offset = new_offset;

	exfat_pwrite(ef->dev, &meta1, sizeof(meta1),
			co2o(ef, new_cluster, new_offset));
	next_entry(ef, dir, &new_cluster, &new_offset);
	exfat_pwrite(ef->dev, &meta2, sizeof(meta2),
			co2o(ef, new_cluster, new_offset));

	for (i = 0; i < name_entries; i++)
	{
		struct exfat_entry_name name_entry = {EXFAT_ENTRY_FILE_NAME, 0};
		memcpy(name_entry.name, name + i * EXFAT_ENAME_MAX,
				EXFAT_ENAME_MAX * sizeof(le16_t));
		next_entry(ef, dir, &new_cluster, &new_offset);
		exfat_pwrite(ef->dev, &name_entry, sizeof(name_entry),
				co2o(ef, new_cluster, new_offset));
	}

	memcpy(node->name, name, (EXFAT_NAME_MAX + 1) * sizeof(le16_t));
	tree_detach(node);
	tree_attach(dir, node);
}

int exfat_rename(struct exfat* ef, const char* old_path, const char* new_path)
{
	struct exfat_node* node;
	struct exfat_node* existing;
	struct exfat_node* dir;
	cluster_t cluster = EXFAT_CLUSTER_BAD;
	off64_t offset = -1;
	le16_t name[EXFAT_NAME_MAX + 1];
	int rc;

	rc = exfat_lookup(ef, &node, old_path);
	if (rc != 0)
		return rc;

	rc = exfat_split(ef, &dir, &existing, name, new_path);
	if (rc != 0)
	{
		exfat_put_node(ef, node);
		return rc;
	}
	if (existing != NULL)
	{
		/* remove target if it's not the same node as source */
		if (existing != node)
		{
			if (existing->flags & EXFAT_ATTRIB_DIR)
			{
				if (node->flags & EXFAT_ATTRIB_DIR)
					rc = exfat_rmdir(ef, existing);
				else
					rc = -ENOTDIR;
			}
			else
			{
				if (!(node->flags & EXFAT_ATTRIB_DIR))
					rc = exfat_unlink(ef, existing);
				else
					rc = -EISDIR;
			}
			exfat_put_node(ef, existing);
			if (rc != 0)
			{
				exfat_put_node(ef, dir);
				exfat_put_node(ef, node);
				return rc;
			}
		}
		else
			exfat_put_node(ef, existing);
	}

	rc = find_slot(ef, dir, &cluster, &offset,
			2 + DIV_ROUND_UP(utf16_length(name), EXFAT_ENAME_MAX));
	if (rc != 0)
	{
		exfat_put_node(ef, dir);
		exfat_put_node(ef, node);
		return rc;
	}
	rename_entry(ef, dir, node, name, cluster, offset);
	exfat_put_node(ef, dir);
	exfat_put_node(ef, node);
	return 0;
}

void exfat_utimes(struct exfat_node* node, const struct timespec tv[2])
{
	node->atime = tv[0].tv_sec;
	node->mtime = tv[1].tv_sec;
	node->flags |= EXFAT_ATTRIB_DIRTY;
}

void exfat_update_atime(struct exfat_node* node)
{
	node->atime = time(NULL);
	node->flags |= EXFAT_ATTRIB_DIRTY;
}

void exfat_update_mtime(struct exfat_node* node)
{
	node->mtime = time(NULL);
	node->flags |= EXFAT_ATTRIB_DIRTY;
}

const char* exfat_get_label(struct exfat* ef)
{
	return ef->label;
}

static int find_label(struct exfat* ef, cluster_t* cluster, off64_t* offset)
{
	struct iterator it;
	int rc;

	rc = opendir(ef, ef->root, &it);
	if (rc != 0)
		return rc;

	for (;;)
	{
		if (it.offset >= ef->root->size)
		{
			closedir(&it);
			return -ENOENT;
		}

		if (get_entry_ptr(ef, &it)->type == EXFAT_ENTRY_LABEL)
		{
			*cluster = it.cluster;
			*offset = it.offset;
			closedir(&it);
			return 0;
		}

		if (fetch_next_entry(ef, ef->root, &it) != 0)
		{
			closedir(&it);
			return -EIO;
		}
	}
}

int exfat_set_label(struct exfat* ef, const char* label)
{
	le16_t label_utf16[EXFAT_ENAME_MAX + 1];
	int rc;
	cluster_t cluster;
	off64_t offset;
	struct exfat_entry_label entry;

	memset(label_utf16, 0, sizeof(label_utf16));
	rc = utf8_to_utf16(label_utf16, label, EXFAT_ENAME_MAX, strlen(label));
	if (rc != 0)
		return rc;

	rc = find_label(ef, &cluster, &offset);
	if (rc == -ENOENT)
		rc = find_slot(ef, ef->root, &cluster, &offset, 1);
	if (rc != 0)
		return rc;

	entry.type = EXFAT_ENTRY_LABEL;
	entry.length = utf16_length(label_utf16);
	memcpy(entry.name, label_utf16, sizeof(entry.name));
	if (entry.length == 0)
		entry.type ^= EXFAT_ENTRY_VALID;

	exfat_pwrite(ef->dev, &entry, sizeof(struct exfat_entry_label),
			co2o(ef, cluster, offset));
	return 0;
}
