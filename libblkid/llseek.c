/*
 * llseek.c -- stub calling the llseek system call
 *
 * Copyright (C) 1994, 1995, 1996, 1997 Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 * %End-Header%
 */

#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef __MSDOS__
#include <io.h>
#endif

#include "blkidP.h"

#ifdef __linux__

#define my_llseek lseek64
#include <linux/unistd.h>

#ifndef __NR__llseek
#define __NR__llseek            140
#endif

blkid_loff_t blkid_llseek(int fd, blkid_loff_t offset, int whence)
{
	blkid_loff_t result;
	static int do_compat = 0;

	if ((sizeof(off_t) >= sizeof(blkid_loff_t)) ||
	    (offset < ((blkid_loff_t) 1 << ((sizeof(off_t)*8) -1))))
		return lseek(fd, (off_t) offset, whence);

	if (do_compat) {
		errno = EOVERFLOW;
		return -1;
	}

	if (result == -1 && errno == ENOSYS) {
		/*
		 * Just in case this code runs on top of an old kernel
		 * which does not support the llseek system call
		 */
		do_compat++;
		errno = EOVERFLOW;
	}
	return result;
}

#else /* !linux */

#ifndef EOVERFLOW
#ifdef EXT2_ET_INVALID_ARGUMENT
#define EOVERFLOW EXT2_ET_INVALID_ARGUMENT
#else
#define EOVERFLOW 112
#endif
#endif

blkid_loff_t blkid_llseek(int fd, blkid_loff_t offset, int origin)
{
#if defined(HAVE_LSEEK64) && defined(HAVE_LSEEK64_PROTOTYPE)
	return lseek64 (fd, offset, origin);
#else
	if ((sizeof(off_t) < sizeof(blkid_loff_t)) &&
	    (offset >= ((blkid_loff_t) 1 << ((sizeof(off_t)*8) - 1)))) {
		errno = EOVERFLOW;
		return -1;
	}
	return lseek(fd, (off_t) offset, origin);
#endif
}

#endif	/* linux */


