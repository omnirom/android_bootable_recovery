/*
**  Copyright 1998-2003 University of Illinois Board of Trustees
**  Copyright 1998-2003 Mark D. Roth
**  All rights reserved.
**
**  util.c - miscellaneous utility code for libtar
**
**  Mark D. Roth <roth@uiuc.edu>
**  Campus Information Technologies and Educational Services
**  University of Illinois at Urbana-Champaign
*/

#include <internal.h>

#include <stdio.h>
#include <sys/param.h>
#include <errno.h>

#ifdef STDC_HEADERS
# include <string.h>
#endif


/* hashing function for pathnames */
int
path_hashfunc(char *key, int numbuckets)
{
	char buf[MAXPATHLEN];
	char *p;

	strcpy(buf, key);
	p = basename(buf);

	return (((unsigned int)p[0]) % numbuckets);
}


/* matching function for dev_t's */
int
dev_match(dev_t *dev1, dev_t *dev2)
{
	return !memcmp(dev1, dev2, sizeof(dev_t));
}


/* matching function for ino_t's */
int
ino_match(ino_t *ino1, ino_t *ino2)
{
	return !memcmp(ino1, ino2, sizeof(ino_t));
}


/* hashing function for dev_t's */
int
dev_hash(dev_t *dev)
{
	return *dev % 16;
}


/* hashing function for ino_t's */
int
ino_hash(ino_t *inode)
{
	return *inode % 256;
}


/*
** mkdirhier() - create all directories in a given path
** returns:
**	0			success
**	1			all directories already exist
**	-1 (and sets errno)	error
*/
int
mkdirhier(char *path)
{
	char src[MAXPATHLEN], dst[MAXPATHLEN] = "";
	char *dirp, *nextp = src;
	int retval = 1;

	if (strlcpy(src, path, sizeof(src)) > sizeof(src))
	{
		errno = ENAMETOOLONG;
		return -1;
	}

	if (path[0] == '/')
		strcpy(dst, "/");

	while ((dirp = strsep(&nextp, "/")) != NULL)
	{
		if (*dirp == '\0')
			continue;

		if (dst[0] != '\0')
			strcat(dst, "/");
		strcat(dst, dirp);

		if (mkdir(dst, 0777) == -1)
		{
			if (errno != EEXIST)
				return -1;
		}
		else
			retval = 0;
	}

	return retval;
}


/* calculate header checksum */
int
th_crc_calc(TAR *t)
{
	int i, sum = 0;

	for (i = 0; i < T_BLOCKSIZE; i++)
		sum += ((unsigned char *)(&(t->th_buf)))[i];
	for (i = 0; i < 8; i++)
		sum += (' ' - (unsigned char)t->th_buf.chksum[i]);

	return sum;
}

/* calculate a signed header checksum */
int
th_signed_crc_calc(TAR *t)
{
	int i, sum = 0;

	for (i = 0; i < T_BLOCKSIZE; i++)
		sum += ((signed char *)(&(t->th_buf)))[i];
	for (i = 0; i < 8; i++)
		sum += (' ' - (signed char)t->th_buf.chksum[i]);

	return sum;
}

/* string-octal to integer conversion */
int64_t
oct_to_int(char *oct, size_t octlen)
{
	long long int val;
	char tmp[octlen + 1];

	memcpy(tmp, oct, octlen);
	tmp[octlen] = '\0';
	return sscanf(oct, "%llo", &val) == 1 ? (int64_t)val : 0;
}


/* string-octal or binary to integer conversion */
int64_t oct_to_int_ex(char *oct, size_t octlen)
{
	if (*(unsigned char *)oct & 0x80) {
		int64_t val = 0;
		char tmp[octlen];
		unsigned char *p;
		unsigned int i;

		memcpy(tmp, oct, octlen);
		*tmp &= 0x7f;
		p = (unsigned char *)tmp + octlen - sizeof(val);
		for (i = 0; i < sizeof(val); ++i) {
			val <<= 8;
			val |= *(p++);
		}
		return val;
	}
	return oct_to_int(oct, octlen);
}


/* integer to NULL-terminated string-octal conversion */
void int_to_oct(int64_t num, char *oct, size_t octlen)
{
	char tmp[sizeof(num)*3 + 1];
	int olen;

	olen = sprintf(tmp, "%0*llo", (int)octlen, (long long)num);
	memcpy(oct, tmp + olen - octlen + 1, octlen);
}


/* integer to string-octal conversion, or binary as necessary */
void
int_to_oct_ex(int64_t num, char *oct, size_t octlen)
{
	if (num < 0 || num >= ((int64_t)1 << ((octlen - 1) * 3))) {
		unsigned char *p;
		unsigned int i;

		memset(oct, 0, octlen);
		p = (unsigned char *)oct + octlen;
		for (i = 0; i < sizeof(num); ++i) {
			*(--p) = num & 0xff;
			num >>= 8;
		}
		if (num < 0) {
			for (; i < octlen; ++i) {
				*(--p) = 0xff;
			}
		}
		*(unsigned char *)oct |= 0x80;
		return;
	}
	int_to_oct(num, oct, octlen);
}
