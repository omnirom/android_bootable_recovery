/*
	io.c (02.09.09)
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
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#ifdef __APPLE__
#include <sys/disk.h>
#endif
#ifdef USE_UBLIO
#include <sys/uio.h>
#include <ublio.h>
#endif

struct exfat_dev
{
	int fd;
	enum exfat_mode mode;
	off64_t size; /* in bytes */
#ifdef USE_UBLIO
	off64_t pos;
	ublio_filehandle_t ufh;
#endif
};

static int open_ro(const char* spec)
{
	return open(spec, O_RDONLY);
}

static int open_rw(const char* spec)
{
	int fd = open(spec, O_RDWR);
#ifdef __linux__
	int ro = 0;

	/*
	   This ioctl is needed because after "blockdev --setro" kernel still
	   allows to open the device in read-write mode but fails writes.
	*/
	if (fd != -1 && ioctl(fd, BLKROGET, &ro) == 0 && ro)
	{
		close(fd);
		return -1;
	}
#endif
	return fd;
}

struct exfat_dev* exfat_open(const char* spec, enum exfat_mode mode)
{
	struct exfat_dev* dev;
	struct stat stbuf;
#ifdef USE_UBLIO
	struct ublio_param up;
#endif

	dev = malloc(sizeof(struct exfat_dev));
	if (dev == NULL)
	{
		exfat_error("failed to allocate memory for device structure");
		return NULL;
	}

	switch (mode)
	{
	case EXFAT_MODE_RO:
		dev->fd = open_ro(spec);
		if (dev->fd == -1)
		{
			free(dev);
			exfat_error("failed to open `%s' in read-only mode", spec);
			return NULL;
		}
		dev->mode = EXFAT_MODE_RO;
		break;
	case EXFAT_MODE_RW:
		dev->fd = open_rw(spec);
		if (dev->fd == -1)
		{
			free(dev);
			exfat_error("failed to open `%s' in read-write mode", spec);
			return NULL;
		}
		dev->mode = EXFAT_MODE_RW;
		break;
	case EXFAT_MODE_ANY:
		dev->fd = open_rw(spec);
		if (dev->fd != -1)
		{
			dev->mode = EXFAT_MODE_RW;
			break;
		}
		dev->fd = open_ro(spec);
		if (dev->fd != -1)
		{
			dev->mode = EXFAT_MODE_RO;
			exfat_warn("`%s' is write-protected, mounting read-only", spec);
			break;
		}
		free(dev);
		exfat_error("failed to open `%s'", spec);
		return NULL;
	}

	if (fstat(dev->fd, &stbuf) != 0)
	{
		close(dev->fd);
		free(dev);
		exfat_error("failed to fstat `%s'", spec);
		return NULL;
	}
	if (!S_ISBLK(stbuf.st_mode) &&
		!S_ISCHR(stbuf.st_mode) &&
		!S_ISREG(stbuf.st_mode))
	{
		close(dev->fd);
		free(dev);
		exfat_error("`%s' is neither a device, nor a regular file", spec);
		return NULL;
	}

#ifdef __APPLE__
	if (!S_ISREG(stbuf.st_mode))
	{
		uint32_t block_size = 0;
		uint64_t blocks = 0;

		if (ioctl(dev->fd, DKIOCGETBLOCKSIZE, &block_size) != 0)
		{
			close(dev->fd);
			free(dev);
			exfat_error("failed to get block size");
			return NULL;
		}
		if (ioctl(dev->fd, DKIOCGETBLOCKCOUNT, &blocks) != 0)
		{
			close(dev->fd);
			free(dev);
			exfat_error("failed to get blocks count");
			return NULL;
		}
		dev->size = blocks * block_size;
	}
	else
#endif
	{
		/* works for Linux, FreeBSD, Solaris */
		dev->size = exfat_seek(dev, 0, SEEK_END);
		if (dev->size <= 0)
		{
			close(dev->fd);
			free(dev);
			exfat_error("failed to get size of `%s'", spec);
			return NULL;
		}
		if (exfat_seek(dev, 0, SEEK_SET) == -1)
		{
			close(dev->fd);
			free(dev);
			exfat_error("failed to seek to the beginning of `%s'", spec);
			return NULL;
		}
	}

#ifdef USE_UBLIO
	memset(&up, 0, sizeof(struct ublio_param));
	up.up_blocksize = 256 * 1024;
	up.up_items = 64;
	up.up_grace = 32;
	up.up_priv = &dev->fd;

	dev->pos = 0;
	dev->ufh = ublio_open(&up);
	if (dev->ufh == NULL)
	{
		close(dev->fd);
		free(dev);
		exfat_error("failed to initialize ublio");
		return NULL;
	}
#endif

	return dev;
}

int exfat_close(struct exfat_dev* dev)
{
#ifdef USE_UBLIO
	if (ublio_close(dev->ufh) != 0)
		exfat_error("failed to close ublio");
#endif
	if (close(dev->fd) != 0)
	{
		free(dev);
		exfat_error("failed to close device");
		return 1;
	}
	free(dev);
	return 0;
}

int exfat_fsync(struct exfat_dev* dev)
{
#ifdef USE_UBLIO
	if (ublio_fsync(dev->ufh) != 0)
#else
	if (fsync(dev->fd) != 0)
#endif
	{
		exfat_error("fsync failed");
		return 1;
	}
	return 0;
}

enum exfat_mode exfat_get_mode(const struct exfat_dev* dev)
{
	return dev->mode;
}

off64_t exfat_get_size(const struct exfat_dev* dev)
{
	return dev->size;
}

off64_t exfat_seek(struct exfat_dev* dev, off64_t offset, int whence)
{
#ifdef USE_UBLIO
	/* XXX SEEK_CUR will be handled incorrectly */
	return dev->pos = lseek64(dev->fd, offset, whence);
#else
	return lseek64(dev->fd, offset, whence);
#endif
}

ssize_t exfat_read(struct exfat_dev* dev, void* buffer, size_t size)
{
#ifdef USE_UBLIO
	ssize_t result = ublio_pread(dev->ufh, buffer, size, dev->pos);
	if (result >= 0)
		dev->pos += size;
	return result;
#else
	return read(dev->fd, buffer, size);
#endif
}

ssize_t exfat_write(struct exfat_dev* dev, const void* buffer, size_t size)
{
#ifdef USE_UBLIO
	ssize_t result = ublio_pwrite(dev->ufh, buffer, size, dev->pos);
	if (result >= 0)
		dev->pos += size;
	return result;
#else
	return write(dev->fd, buffer, size);
#endif
}

void exfat_pread(struct exfat_dev* dev, void* buffer, size_t size,
		off64_t offset)
{
#ifdef USE_UBLIO
	if (ublio_pread(dev->ufh, buffer, size, offset) != size)
#else
	if (pread64(dev->fd, buffer, size, offset) != size)
#endif
		exfat_bug("failed to read %zu bytes from file at %"PRIu64, size,
				(uint64_t) offset);
}

void exfat_pwrite(struct exfat_dev* dev, const void* buffer, size_t size,
		off64_t offset)
{
#ifdef USE_UBLIO
	if (ublio_pwrite(dev->ufh, buffer, size, offset) != size)
#else
	if (pwrite64(dev->fd, buffer, size, offset) != size)
#endif
		exfat_bug("failed to write %zu bytes to file at %"PRIu64, size,
				(uint64_t) offset);
}

ssize_t exfat_generic_pread(const struct exfat* ef, struct exfat_node* node,
		void* buffer, size_t size, off64_t offset)
{
	cluster_t cluster;
	char* bufp = buffer;
	off64_t lsize, loffset, remainder;

	if (offset >= node->size)
		return 0;
	if (size == 0)
		return 0;

	cluster = exfat_advance_cluster(ef, node, offset / CLUSTER_SIZE(*ef->sb));
	if (CLUSTER_INVALID(cluster))
	{
		exfat_error("invalid cluster 0x%x while reading", cluster);
		return -1;
	}

	loffset = offset % CLUSTER_SIZE(*ef->sb);
	remainder = MIN(size, node->size - offset);
	while (remainder > 0)
	{
		if (CLUSTER_INVALID(cluster))
		{
			exfat_error("invalid cluster 0x%x while reading", cluster);
			return -1;
		}
		lsize = MIN(CLUSTER_SIZE(*ef->sb) - loffset, remainder);
		exfat_pread(ef->dev, bufp, lsize, exfat_c2o(ef, cluster) + loffset);
		bufp += lsize;
		loffset = 0;
		remainder -= lsize;
		cluster = exfat_next_cluster(ef, node, cluster);
	}
	if (!ef->ro && !ef->noatime)
		exfat_update_atime(node);
	return size - remainder;
}

ssize_t exfat_generic_pwrite(struct exfat* ef, struct exfat_node* node,
		const void* buffer, size_t size, off64_t offset)
{
	cluster_t cluster;
	const char* bufp = buffer;
	off64_t lsize, loffset, remainder;
	printf("node: %s\n", node);
	if (offset + size > node->size)
		if (exfat_truncate(ef, node, offset + size) != 0)
			return -1;
	if (size == 0)
		return 0;

	cluster = exfat_advance_cluster(ef, node, offset / CLUSTER_SIZE(*ef->sb));
	if (CLUSTER_INVALID(cluster))
	{
		exfat_error("invalid cluster 0x%x while writing", cluster);
		return -1;
	}

	loffset = offset % CLUSTER_SIZE(*ef->sb);
	remainder = size;
	while (remainder > 0)
	{
		if (CLUSTER_INVALID(cluster))
		{
			exfat_error("invalid cluster 0x%x while writing", cluster);
			return -1;
		}
		lsize = MIN(CLUSTER_SIZE(*ef->sb) - loffset, remainder);
		exfat_pwrite(ef->dev, bufp, lsize, exfat_c2o(ef, cluster) + loffset);
		bufp += lsize;
		loffset = 0;
		remainder -= lsize;
		cluster = exfat_next_cluster(ef, node, cluster);
	}
	exfat_update_mtime(node);
	return size - remainder;
}
