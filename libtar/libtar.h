/*
**  Copyright 1998-2003 University of Illinois Board of Trustees
**  Copyright 1998-2003 Mark D. Roth
**  All rights reserved.
**
**  libtar.h - header file for libtar library
**
**  Mark D. Roth <roth@uiuc.edu>
**  Campus Information Technologies and Educational Services
**  University of Illinois at Urbana-Champaign
*/

#ifndef LIBTAR_H
#define LIBTAR_H

#include <sys/types.h>
#include <sys/stat.h>
#include "tar.h"

#include "libtar_listhash.h"

#ifdef __cplusplus
extern "C"
{
#endif


/* useful constants */
/* see FIXME note in block.c regarding T_BLOCKSIZE */
#define T_BLOCKSIZE		512
#define T_NAMELEN		100
#define T_PREFIXLEN		155
#define T_MAXPATHLEN		(T_NAMELEN + T_PREFIXLEN)

/* GNU extensions for typeflag */
#define GNU_LONGNAME_TYPE	'L'
#define GNU_LONGLINK_TYPE	'K'

/* extended metadata for next file - used to store selinux_context */
#define TH_EXT_TYPE		'x'

/* our version of the tar header structure */
struct tar_header
{
	char name[100];
	char mode[8];
	char uid[8];
	char gid[8];
	char size[12];
	char mtime[12];
	char chksum[8];
	char typeflag;
	char linkname[100];
	char magic[6];
	char version[2];
	char uname[32];
	char gname[32];
	char devmajor[8];
	char devminor[8];
	char prefix[155];
	char padding[12];
	char *gnu_longname;
	char *gnu_longlink;
#ifdef HAVE_SELINUX
	char *selinux_context;
#endif
};


/***** handle.c ************************************************************/

typedef int (*openfunc_t)(const char *, int, ...);
typedef int (*closefunc_t)(int);
typedef ssize_t (*readfunc_t)(int, void *, size_t);
typedef ssize_t (*writefunc_t)(int, const void *, size_t);

typedef struct
{
	openfunc_t openfunc;
	closefunc_t closefunc;
	readfunc_t readfunc;
	writefunc_t writefunc;
}
tartype_t;

typedef struct
{
	tartype_t *type;
	const char *pathname;
	long fd;
	int oflags;
	int options;
	struct tar_header th_buf;
	libtar_hash_t *h;

	/* introduced in libtar 1.2.21 */
	char *th_pathname;
}
TAR;

/* constant values for the TAR options field */
#define TAR_GNU			 1	/* use GNU extensions */
#define TAR_VERBOSE		 2	/* output file info to stdout */
#define TAR_NOOVERWRITE		 4	/* don't overwrite existing files */
#define TAR_IGNORE_EOT		 8	/* ignore double zero blocks as EOF */
#define TAR_CHECK_MAGIC		16	/* check magic in file header */
#define TAR_CHECK_VERSION	32	/* check version in file header */
#define TAR_IGNORE_CRC		64	/* ignore CRC in file header */
#define TAR_STORE_SELINUX	128	/* store selinux context */
#define TAR_USE_NUMERIC_ID	256	/* favor numeric owner over names */

/* this is obsolete - it's here for backwards-compatibility only */
#define TAR_IGNORE_MAGIC	0

extern const char libtar_version[];


/* open a new tarfile handle */
int tar_open(TAR **t, const char *pathname, tartype_t *type,
	     int oflags, int mode, int options);

/* make a tarfile handle out of a previously-opened descriptor */
int tar_fdopen(TAR **t, int fd, const char *pathname, tartype_t *type,
	       int oflags, int mode, int options);

/* returns the descriptor associated with t */
int tar_fd(TAR *t);

/* close tarfile handle */
int tar_close(TAR *t);


/***** append.c ************************************************************/

/* forward declaration to appease the compiler */
struct tar_dev;

/* cleanup function */
void tar_dev_free(struct tar_dev *tdp);

/* Appends a file to the tar archive.
 * Arguments:
 *    t        = TAR handle to append to
 *    realname = path of file to append
 *    savename = name to save the file under in the archive
 */
int tar_append_file(TAR *t, const char *realname, const char *savename);

/* write EOF indicator */
int tar_append_eof(TAR *t);

/* add file contents to a tarchive */
int tar_append_regfile(TAR *t, const char *realname);

/* Appends in-memory file contents to a tarchive.
 * Arguments:
 *    t        = TAR handle to append to
 *    savename = name to save the file under in the archive
 *    mode     = mode
 *    uid, gid = owner
 *    buf, len = in-memory buffer
 */
int tar_append_file_contents(TAR *t, const char *savename, mode_t mode,
                             uid_t uid, gid_t gid, void *buf, size_t len);

/* add buffer to a tarchive */
int tar_append_buffer(TAR *t, void *buf, size_t len);

/***** block.c *************************************************************/

/* macros for reading/writing tarchive blocks */
#define tar_block_read(t, buf) \
	(*((t)->type->readfunc))((t)->fd, (char *)(buf), T_BLOCKSIZE)
#define tar_block_write(t, buf) \
	(*((t)->type->writefunc))((t)->fd, (char *)(buf), T_BLOCKSIZE)

/* read/write a header block */
int th_read(TAR *t);
int th_write(TAR *t);


/***** decode.c ************************************************************/

/* determine file type */
#define TH_ISREG(t)	((t)->th_buf.typeflag == REGTYPE \
			 || (t)->th_buf.typeflag == AREGTYPE \
			 || (t)->th_buf.typeflag == CONTTYPE \
			 || (S_ISREG((mode_t)oct_to_int((t)->th_buf.mode, sizeof((t)->th_buf.mode))) \
			     && (t)->th_buf.typeflag != LNKTYPE))
#define TH_ISLNK(t)	((t)->th_buf.typeflag == LNKTYPE)
#define TH_ISSYM(t)	((t)->th_buf.typeflag == SYMTYPE \
			 || S_ISLNK((mode_t)oct_to_int((t)->th_buf.mode, sizeof((t)->th_buf.mode))))
#define TH_ISCHR(t)	((t)->th_buf.typeflag == CHRTYPE \
			 || S_ISCHR((mode_t)oct_to_int((t)->th_buf.mode, sizeof((t)->th_buf.mode))))
#define TH_ISBLK(t)	((t)->th_buf.typeflag == BLKTYPE \
			 || S_ISBLK((mode_t)oct_to_int((t)->th_buf.mode, sizeof((t)->th_buf.mode))))
#define TH_ISDIR(t)	((t)->th_buf.typeflag == DIRTYPE \
			 || S_ISDIR((mode_t)oct_to_int((t)->th_buf.mode, sizeof((t)->th_buf.mode))) \
			 || ((t)->th_buf.typeflag == AREGTYPE \
			     && strnlen((t)->th_buf.name, T_NAMELEN) \
			     && ((t)->th_buf.name[strnlen((t)->th_buf.name, T_NAMELEN) - 1] == '/')))
#define TH_ISFIFO(t)	((t)->th_buf.typeflag == FIFOTYPE \
			 || S_ISFIFO((mode_t)oct_to_int((t)->th_buf.mode, sizeof((t)->th_buf.mode))))
#define TH_ISLONGNAME(t)	((t)->th_buf.typeflag == GNU_LONGNAME_TYPE)
#define TH_ISLONGLINK(t)	((t)->th_buf.typeflag == GNU_LONGLINK_TYPE)
#define TH_ISEXTHEADER(t)	((t)->th_buf.typeflag == TH_EXT_TYPE)

/* decode tar header info */
#define th_get_crc(t) oct_to_int((t)->th_buf.chksum, sizeof((t)->th_buf.chksum))
#define th_get_size(t) oct_to_int_ex((t)->th_buf.size, sizeof((t)->th_buf.size))
#define th_get_mtime(t) oct_to_int_ex((t)->th_buf.mtime, sizeof((t)->th_buf.mtime))
#define th_get_devmajor(t) oct_to_int((t)->th_buf.devmajor, sizeof((t)->th_buf.devmajor))
#define th_get_devminor(t) oct_to_int((t)->th_buf.devminor, sizeof((t)->th_buf.devminor))
#define th_get_linkname(t) ((t)->th_buf.gnu_longlink \
                            ? (t)->th_buf.gnu_longlink \
                            : (t)->th_buf.linkname)
char *th_get_pathname(TAR *t);
mode_t th_get_mode(TAR *t);
uid_t th_get_uid(TAR *t);
gid_t th_get_gid(TAR *t);


/***** encode.c ************************************************************/

/* encode file info in th_header */
void th_set_type(TAR *t, mode_t mode);
void th_set_path(TAR *t, const char *pathname);
void th_set_link(TAR *t, const char *linkname);
void th_set_device(TAR *t, dev_t device);
void th_set_user(TAR *t, uid_t uid);
void th_set_group(TAR *t, gid_t gid);
void th_set_mode(TAR *t, mode_t fmode);
#define th_set_mtime(t, fmtime) \
	int_to_oct_ex((fmtime), (t)->th_buf.mtime, sizeof((t)->th_buf.mtime))
#define th_set_size(t, fsize) \
	int_to_oct_ex((fsize), (t)->th_buf.size, sizeof((t)->th_buf.size))

/* encode everything at once (except the pathname and linkname) */
void th_set_from_stat(TAR *t, struct stat *s);

/* encode magic, version, and crc - must be done after everything else is set */
void th_finish(TAR *t);


/***** extract.c ***********************************************************/

/* sequentially extract next file from t */
int tar_extract_file(TAR *t, const char *realname, const char *prefix, const int *progress_fd);

/* extract different file types */
int tar_extract_dir(TAR *t, const char *realname);
int tar_extract_hardlink(TAR *t, const char *realname, const char *prefix);
int tar_extract_symlink(TAR *t, const char *realname);
int tar_extract_chardev(TAR *t, const char *realname);
int tar_extract_blockdev(TAR *t, const char *realname);
int tar_extract_fifo(TAR *t, const char *realname);

/* for regfiles, we need to extract the content blocks as well */
int tar_extract_regfile(TAR *t, const char *realname, const int *progress_fd);
int tar_skip_regfile(TAR *t);

/* extract regfile to buffer */
int tar_extract_file_contents(TAR *t, void *buf, size_t *lenp);

/***** output.c ************************************************************/

/* print the tar header */
void th_print(TAR *t);

/* print "ls -l"-like output for the file described by th */
void th_print_long_ls(TAR *t);


/***** util.c *************************************************************/

/* hashing function for pathnames */
int path_hashfunc(char *key, int numbuckets);

/* matching function for dev_t's */
int dev_match(dev_t *dev1, dev_t *dev2);

/* matching function for ino_t's */
int ino_match(ino_t *ino1, ino_t *ino2);

/* hashing function for dev_t's */
int dev_hash(dev_t *dev);

/* hashing function for ino_t's */
int ino_hash(ino_t *inode);

/* create any necessary dirs */
int mkdirhier(char *path);

/* calculate header checksum */
int th_crc_calc(TAR *t);

/* calculate a signed header checksum */
int th_signed_crc_calc(TAR *t);

/* compare checksums in a forgiving way */
#define th_crc_ok(t) (th_get_crc(t) == th_crc_calc(t) || th_get_crc(t) == th_signed_crc_calc(t))

/* string-octal to integer conversion */
int64_t oct_to_int(char *oct, size_t len);

/* string-octal or binary to integer conversion */
int64_t oct_to_int_ex(char *oct, size_t len);

/* integer to NULL-terminated string-octal conversion */
void int_to_oct(int64_t num, char *oct, size_t octlen);

/* integer to string-octal conversion, or binary as necessary */
void int_to_oct_ex(int64_t num, char *oct, size_t octlen);


/***** wrapper.c **********************************************************/

/* extract groups of files */
int tar_extract_glob(TAR *t, char *globname, char *prefix);
int tar_extract_all(TAR *t, char *prefix, const int *progress_fd);

/* add a whole tree of files */
int tar_append_tree(TAR *t, char *realdir, char *savedir);

/* find an entry */
int tar_find(TAR *t, char *searchstr);

#ifdef __cplusplus
}
#endif

#endif /* ! LIBTAR_H */

