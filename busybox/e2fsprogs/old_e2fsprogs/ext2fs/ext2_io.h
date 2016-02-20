/* vi: set sw=4 ts=4: */
/*
 * io.h --- the I/O manager abstraction
 *
 * Copyright (C) 1993, 1994, 1995, 1996 Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */
#ifndef EXT2FS_EXT2_IO_H
#define EXT2FS_EXT2_IO_H 1

/*
 * ext2_loff_t is defined here since unix_io.c needs it.
 */
#if defined(__GNUC__) || defined(HAS_LONG_LONG)
typedef long long	ext2_loff_t;
#else
typedef long		ext2_loff_t;
#endif

/* llseek.c */
/* ext2_loff_t ext2fs_llseek (int, ext2_loff_t, int); */
#ifdef CONFIG_LFS
# define ext2fs_llseek lseek64
#else
# define ext2fs_llseek lseek
#endif

typedef struct struct_io_manager *io_manager;
typedef struct struct_io_channel *io_channel;

#define CHANNEL_FLAGS_WRITETHROUGH	0x01

struct struct_io_channel {
	errcode_t	magic;
	io_manager	manager;
	char		*name;
	int		block_size;
	errcode_t	(*read_error)(io_channel channel,
				      unsigned long block,
				      int count,
				      void *data,
				      size_t size,
				      int actual_bytes_read,
				      errcode_t error);
	errcode_t       (*write_error)(io_channel channel,
				       unsigned long block,
				       int count,
				       const void *data,
				       size_t size,
				       int actual_bytes_written,
				       errcode_t error);
	int		refcount;
	int		flags;
	int		reserved[14];
	void		*private_data;
	void		*app_data;
};

struct struct_io_manager {
	errcode_t magic;
	const char *name;
	errcode_t (*open)(const char *name, int flags, io_channel *channel);
	errcode_t (*close)(io_channel channel);
	errcode_t (*set_blksize)(io_channel channel, int blksize);
	errcode_t (*read_blk)(io_channel channel, unsigned long block,
			      int count, void *data);
	errcode_t (*write_blk)(io_channel channel, unsigned long block,
			       int count, const void *data);
	errcode_t (*flush)(io_channel channel);
	errcode_t (*write_byte)(io_channel channel, unsigned long offset,
				int count, const void *data);
	errcode_t (*set_option)(io_channel channel, const char *option,
				const char *arg);
	int             reserved[14];
};

#define IO_FLAG_RW	1

/*
 * Convenience functions....
 */
#define io_channel_close(c)		((c)->manager->close((c)))
#define io_channel_set_blksize(c,s)	((c)->manager->set_blksize((c),s))
#define io_channel_read_blk(c,b,n,d)	((c)->manager->read_blk((c),b,n,d))
#define io_channel_write_blk(c,b,n,d)	((c)->manager->write_blk((c),b,n,d))
#define io_channel_flush(c)		((c)->manager->flush((c)))
#define io_channel_bumpcount(c)		((c)->refcount++)

/* io_manager.c */
extern errcode_t io_channel_set_options(io_channel channel,
					const char *options);
extern errcode_t io_channel_write_byte(io_channel channel,
				       unsigned long offset,
				       int count, const void *data);

/* unix_io.c */
extern io_manager unix_io_manager;

/* test_io.c */
extern io_manager test_io_manager, test_io_backing_manager;
extern void (*test_io_cb_read_blk)
	(unsigned long block, int count, errcode_t err);
extern void (*test_io_cb_write_blk)
	(unsigned long block, int count, errcode_t err);
extern void (*test_io_cb_set_blksize)
	(int blksize, errcode_t err);

#endif
