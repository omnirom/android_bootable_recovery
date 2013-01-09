/*
	main.c (01.09.09)
	FUSE-based exFAT implementation. Requires FUSE 2.6 or later.

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

#define FUSE_USE_VERSION 26
#include <fuse.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <exfat.h>
#include <inttypes.h>
#include <limits.h>
#include <sys/types.h>
#include <pwd.h>
#include <unistd.h>

#define exfat_debug(format, ...)

#if !defined(FUSE_VERSION) || (FUSE_VERSION < 26)
	#error FUSE 2.6 or later is required
#endif

const char* default_options = "ro_fallback,allow_other,blkdev,big_writes";

struct exfat ef;

static struct exfat_node* get_node(const struct fuse_file_info* fi)
{
	return (struct exfat_node*) (size_t) fi->fh;
}

static void set_node(struct fuse_file_info* fi, struct exfat_node* node)
{
	fi->fh = (uint64_t) (size_t) node;
}

static int fuse_exfat_getattr(const char* path, struct stat* stbuf)
{
	struct exfat_node* node;
	int rc;

	exfat_debug("[%s] %s", __func__, path);

	rc = exfat_lookup(&ef, &node, path);
	if (rc != 0)
		return rc;

	exfat_stat(&ef, node, stbuf);
	exfat_put_node(&ef, node);
	return 0;
}

static int fuse_exfat_truncate(const char* path, off64_t size)
{
	struct exfat_node* node;
	int rc;

	exfat_debug("[%s] %s, %"PRId64, __func__, path, size);

	rc = exfat_lookup(&ef, &node, path);
	if (rc != 0)
		return rc;

	rc = exfat_truncate(&ef, node, size);
	exfat_put_node(&ef, node);
	return rc;
}

static int fuse_exfat_readdir(const char* path, void* buffer,
		fuse_fill_dir_t filler, off64_t offset, struct fuse_file_info* fi)
{
	struct exfat_node* parent;
	struct exfat_node* node;
	struct exfat_iterator it;
	int rc;
	char name[EXFAT_NAME_MAX + 1];

	exfat_debug("[%s] %s", __func__, path);

	rc = exfat_lookup(&ef, &parent, path);
	if (rc != 0)
		return rc;
	if (!(parent->flags & EXFAT_ATTRIB_DIR))
	{
		exfat_put_node(&ef, parent);
		exfat_error("`%s' is not a directory (0x%x)", path, parent->flags);
		return -ENOTDIR;
	}

	filler(buffer, ".", NULL, 0);
	filler(buffer, "..", NULL, 0);

	rc = exfat_opendir(&ef, parent, &it);
	if (rc != 0)
	{
		exfat_put_node(&ef, parent);
		exfat_error("failed to open directory `%s'", path);
		return rc;
	}
	while ((node = exfat_readdir(&ef, &it)))
	{
		exfat_get_name(node, name, EXFAT_NAME_MAX);
		exfat_debug("[%s] %s: %s, %"PRId64" bytes, cluster 0x%x", __func__,
				name, IS_CONTIGUOUS(*node) ? "contiguous" : "fragmented",
				node->size, node->start_cluster);
		filler(buffer, name, NULL, 0);
		exfat_put_node(&ef, node);
	}
	exfat_closedir(&ef, &it);
	exfat_put_node(&ef, parent);
	return 0;
}

static int fuse_exfat_open(const char* path, struct fuse_file_info* fi)
{
	struct exfat_node* node;
	int rc;

	exfat_debug("[%s] %s", __func__, path);

	rc = exfat_lookup(&ef, &node, path);
	if (rc != 0)
		return rc;
	set_node(fi, node);
	fi->keep_cache = 1;
	return 0;
}

static int fuse_exfat_release(const char* path, struct fuse_file_info* fi)
{
	exfat_debug("[%s] %s", __func__, path);
	exfat_put_node(&ef, get_node(fi));
	return 0;
}

static int fuse_exfat_read(const char* path, char* buffer, size_t size,
		off64_t offset, struct fuse_file_info* fi)
{
	exfat_debug("[%s] %s (%zu bytes)", __func__, path, size);
	if (exfat_generic_pread(&ef, get_node(fi), buffer, size, offset) != size)
		return EOF;
	return size;
}

static int fuse_exfat_write(const char* path, const char* buffer, size_t size,
		off64_t offset, struct fuse_file_info* fi)
{
	exfat_debug("[%s] %s (%zu bytes)", __func__, path, size);
	if (exfat_generic_pwrite(&ef, get_node(fi), buffer, size, offset) != size)
		return EOF;
	return size;
}

static int fuse_exfat_unlink(const char* path)
{
	struct exfat_node* node;
	int rc;

	exfat_debug("[%s] %s", __func__, path);

	rc = exfat_lookup(&ef, &node, path);
	if (rc != 0)
		return rc;

	rc = exfat_unlink(&ef, node);
	exfat_put_node(&ef, node);
	return rc;
}

static int fuse_exfat_rmdir(const char* path)
{
	struct exfat_node* node;
	int rc;

	exfat_debug("[%s] %s", __func__, path);

	rc = exfat_lookup(&ef, &node, path);
	if (rc != 0)
		return rc;

	rc = exfat_rmdir(&ef, node);
	exfat_put_node(&ef, node);
	return rc;
}

static int fuse_exfat_mknod(const char* path, mode_t mode, dev_t dev)
{
	exfat_debug("[%s] %s 0%ho", __func__, path, mode);
	return exfat_mknod(&ef, path);
}

static int fuse_exfat_mkdir(const char* path, mode_t mode)
{
	exfat_debug("[%s] %s 0%ho", __func__, path, mode);
	return exfat_mkdir(&ef, path);
}

static int fuse_exfat_rename(const char* old_path, const char* new_path)
{
	exfat_debug("[%s] %s => %s", __func__, old_path, new_path);
	return exfat_rename(&ef, old_path, new_path);
}

static int fuse_exfat_utimens(const char* path, const struct timespec tv[2])
{
	struct exfat_node* node;
	int rc;

	exfat_debug("[%s] %s", __func__, path);

	rc = exfat_lookup(&ef, &node, path);
	if (rc != 0)
		return rc;

	exfat_utimes(node, tv);
	exfat_put_node(&ef, node);
	return 0;
}

#ifdef __APPLE__
static int fuse_exfat_chmod(const char* path, mode_t mode)
{
	exfat_debug("[%s] %s 0%ho", __func__, path, mode);
	/* make OS X utilities happy */
	return 0;
}
#endif

static int fuse_exfat_statfs(const char* path, struct statvfs* sfs)
{
	exfat_debug("[%s]", __func__);

	sfs->f_bsize = CLUSTER_SIZE(*ef.sb);
	sfs->f_frsize = CLUSTER_SIZE(*ef.sb);
	sfs->f_blocks = le64_to_cpu(ef.sb->sector_count) >> ef.sb->spc_bits;
	sfs->f_bavail = exfat_count_free_clusters(&ef);
	sfs->f_bfree = sfs->f_bavail;
	sfs->f_namemax = EXFAT_NAME_MAX;

	/*
	   Below are fake values because in exFAT there is
	   a) no simple way to count files;
	   b) no such thing as inode;
	   So here we assume that inode = cluster.
	*/
	sfs->f_files = (sfs->f_blocks - sfs->f_bfree) >> ef.sb->spc_bits;
	sfs->f_favail = sfs->f_bfree >> ef.sb->spc_bits;
	sfs->f_ffree = sfs->f_bavail;

	return 0;
}

static void* fuse_exfat_init(struct fuse_conn_info* fci)
{
	exfat_debug("[%s]", __func__);
#ifdef FUSE_CAP_BIG_WRITES
	fci->want |= FUSE_CAP_BIG_WRITES;
#endif
	return NULL;
}

static void fuse_exfat_destroy(void* unused)
{
	exfat_debug("[%s]", __func__);
	exfat_unmount(&ef);
}

static void usage(const char* prog)
{
	fprintf(stderr, "Usage: %s [-d] [-o options] [-v] <device> <dir>\n", prog);
	exit(1);
}

static struct fuse_operations fuse_exfat_ops =
{
	.getattr	= fuse_exfat_getattr,
	.truncate	= fuse_exfat_truncate,
	.readdir	= fuse_exfat_readdir,
	.open		= fuse_exfat_open,
	.release	= fuse_exfat_release,
	.read		= fuse_exfat_read,
	.write		= fuse_exfat_write,
	.unlink		= fuse_exfat_unlink,
	.rmdir		= fuse_exfat_rmdir,
	.mknod		= fuse_exfat_mknod,
	.mkdir		= fuse_exfat_mkdir,
	.rename		= fuse_exfat_rename,
	.utimens	= fuse_exfat_utimens,
#ifdef __APPLE__
	.chmod		= fuse_exfat_chmod,
#endif
	.statfs		= fuse_exfat_statfs,
	.init		= fuse_exfat_init,
	.destroy	= fuse_exfat_destroy,
};

static char* add_option(char* options, const char* name, const char* value)
{
	size_t size;

	if (value)
		size = strlen(options) + strlen(name) + strlen(value) + 3;
	else
		size = strlen(options) + strlen(name) + 2;

	options = realloc(options, size);
	if (options == NULL)
	{
		exfat_error("failed to reallocate options string");
		return NULL;
	}
	strcat(options, ",");
	strcat(options, name);
	if (value)
	{
		strcat(options, "=");
		strcat(options, value);
	}
	return options;
}

static char* add_fsname_option(char* options, const char* spec)
{
	char spec_abs[PATH_MAX];

	if (realpath(spec, spec_abs) == NULL)
	{
		free(options);
		exfat_error("failed to get absolute path for `%s'", spec);
		return NULL;
	}
	return add_option(options, "fsname", spec_abs);
}

static char* add_user_option(char* options)
{
	struct passwd* pw;

	if (getuid() == 0)
		return options;

	pw = getpwuid(getuid());
	if (pw == NULL || pw->pw_name == NULL)
	{
		free(options);
		exfat_error("failed to determine username");
		return NULL;
	}
	return add_option(options, "user", pw->pw_name);
}

static char* add_blksize_option(char* options, long cluster_size)
{
	long page_size = sysconf(_SC_PAGESIZE);
	char blksize[20];

	if (page_size < 1)
		page_size = 0x1000;

	snprintf(blksize, sizeof(blksize), "%ld", MIN(page_size, cluster_size));
	return add_option(options, "blksize", blksize);
}

static char* add_fuse_options(char* options, const char* spec)
{
	options = add_fsname_option(options, spec);
	if (options == NULL)
		return NULL;
	options = add_user_option(options);
	if (options == NULL)
		return NULL;
	options = add_blksize_option(options, CLUSTER_SIZE(*ef.sb));
	if (options == NULL)
		return NULL;

	return options;
}

int main(int argc, char* argv[])
{
	struct fuse_args mount_args = FUSE_ARGS_INIT(0, NULL);
	struct fuse_args newfs_args = FUSE_ARGS_INIT(0, NULL);
	const char* spec = NULL;
	const char* mount_point = NULL;
	char* mount_options;
	int debug = 0;
	struct fuse_chan* fc = NULL;
	struct fuse* fh = NULL;
	char** pp;

	printf("FUSE exfat %u.%u.%u\n",
			EXFAT_VERSION_MAJOR, EXFAT_VERSION_MINOR, EXFAT_VERSION_PATCH);

	mount_options = strdup(default_options);
	if (mount_options == NULL)
	{
		exfat_error("failed to allocate options string");
		return 1;
	}

	for (pp = argv + 1; *pp; pp++)
	{
		if (strcmp(*pp, "-o") == 0)
		{
			pp++;
			if (*pp == NULL)
				usage(argv[0]);
			mount_options = add_option(mount_options, *pp, NULL);
			if (mount_options == NULL)
				return 1;
		}
		else if (strcmp(*pp, "-d") == 0)
			debug = 1;
		else if (strcmp(*pp, "-v") == 0)
		{
			free(mount_options);
			puts("Copyright (C) 2010-2012  Andrew Nayenko");
			return 0;
		}
		else if (spec == NULL)
			spec = *pp;
		else if (mount_point == NULL)
			mount_point = *pp;
		else
		{
			free(mount_options);
			usage(argv[0]);
		}
	}
	if (spec == NULL || mount_point == NULL)
	{
		free(mount_options);
		usage(argv[0]);
	}

	if (exfat_mount(&ef, spec, mount_options) != 0)
	{
		free(mount_options);
		return 1;
	}

	if (ef.ro == -1) /* read-only fallback was used */
	{
		mount_options = add_option(mount_options, "ro", NULL);
		if (mount_options == NULL)
		{
			exfat_unmount(&ef);
			return 1;
		}
	}

	mount_options = add_fuse_options(mount_options, spec);
	if (mount_options == NULL)
	{
		exfat_unmount(&ef);
		return 1;
	}

	/* create arguments for fuse_mount() */
	if (fuse_opt_add_arg(&mount_args, "exfat") != 0 ||
		fuse_opt_add_arg(&mount_args, "-o") != 0 ||
		fuse_opt_add_arg(&mount_args, mount_options) != 0)
	{
		exfat_unmount(&ef);
		free(mount_options);
		return 1;
	}

	free(mount_options);

	/* create FUSE mount point */
	fc = fuse_mount(mount_point, &mount_args);
	fuse_opt_free_args(&mount_args);
	if (fc == NULL)
	{
		exfat_unmount(&ef);
		return 1;
	}

	/* create arguments for fuse_new() */
	if (fuse_opt_add_arg(&newfs_args, "") != 0 ||
		(debug && fuse_opt_add_arg(&newfs_args, "-d") != 0))
	{
		fuse_unmount(mount_point, fc);
		exfat_unmount(&ef);
		return 1;
	}

	/* create new FUSE file system */
	fh = fuse_new(fc, &newfs_args, &fuse_exfat_ops,
			sizeof(struct fuse_operations), NULL);
	fuse_opt_free_args(&newfs_args);
	if (fh == NULL)
	{
		fuse_unmount(mount_point, fc);
		exfat_unmount(&ef);
		return 1;
	}

	/* exit session on HUP, TERM and INT signals and ignore PIPE signal */
	if (fuse_set_signal_handlers(fuse_get_session(fh)) != 0)
	{
		fuse_unmount(mount_point, fc);
		fuse_destroy(fh);
		exfat_unmount(&ef);
		exfat_error("failed to set signal handlers");
		return 1;
	}

	/* go to background (unless "-d" option is passed) and run FUSE
	   main loop */
	if (fuse_daemonize(debug) == 0)
	{
		if (fuse_loop(fh) != 0)
			exfat_error("FUSE loop failure");
	}
	else
		exfat_error("failed to daemonize");

	fuse_remove_signal_handlers(fuse_get_session(fh));
	/* note that fuse_unmount() must be called BEFORE fuse_destroy() */
	fuse_unmount(mount_point, fc);
	fuse_destroy(fh);
	return 0;
}
