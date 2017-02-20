/*
**  Copyright 1998-2003 University of Illinois Board of Trustees
**  Copyright 1998-2003 Mark D. Roth
**  All rights reserved.
**
**  block.c - libtar code to handle tar archive header blocks
**
**  Mark D. Roth <roth@uiuc.edu>
**  Campus Information Technologies and Educational Services
**  University of Illinois at Urbana-Champaign
*/

#include <internal.h>
#include <errno.h>

#ifdef STDC_HEADERS
# include <string.h>
# include <stdlib.h>
#endif

#define BIT_ISSET(bitmask, bit) ((bitmask) & (bit))

// Used to identify selinux_context in extended ('x')
// metadata. From RedHat implementation.
#define SELINUX_TAG "RHT.security.selinux="
#define SELINUX_TAG_LEN strlen(SELINUX_TAG)

// Used to identify e4crypt_policy in extended ('x')
#define E4CRYPT_TAG "TWRP.security.e4crypt="
#define E4CRYPT_TAG_LEN strlen(E4CRYPT_TAG)

// Used to identify Posix capabilities in extended ('x')
#define CAPABILITIES_TAG "SCHILY.xattr.security.capability="
#define CAPABILITIES_TAG_LEN strlen(CAPABILITIES_TAG)

// Used to identify Android user.default xattr in extended ('x')
#define ANDROID_USER_DEFAULT_TAG "ANDROID.user.default"
#define ANDROID_USER_DEFAULT_TAG_LEN strlen(ANDROID_USER_DEFAULT_TAG)

// Used to identify Android user.inode_cache xattr in extended ('x')
#define ANDROID_USER_CACHE_TAG "ANDROID.user.inode_cache"
#define ANDROID_USER_CACHE_TAG_LEN strlen(ANDROID_USER_CACHE_TAG)

// Used to identify Android user.inode_code_cache xattr in extended ('x')
#define ANDROID_USER_CODE_CACHE_TAG "ANDROID.user.inode_code_cache"
#define ANDROID_USER_CODE_CACHE_TAG_LEN strlen(ANDROID_USER_CODE_CACHE_TAG)

/* read a header block */
/* FIXME: the return value of this function should match the return value
	  of tar_block_read(), which is a macro which references a prototype
	  that returns a ssize_t.  So far, this is safe, since tar_block_read()
	  only ever reads 512 (T_BLOCKSIZE) bytes at a time, so any difference
	  in size of ssize_t and int is of negligible risk.  BUT, if
	  T_BLOCKSIZE ever changes, or ever becomes a variable parameter
	  controllable by the user, all the code that calls it,
	  including this function and all code that calls it, should be
	  fixed for security reasons.
	  Thanks to Chris Palmer for the critique.
*/
int
th_read_internal(TAR *t)
{
	int i;
	int num_zero_blocks = 0;

#ifdef DEBUG
	printf("==> th_read_internal(TAR=\"%s\")\n", t->pathname);
#endif

	while ((i = tar_block_read(t, &(t->th_buf))) == T_BLOCKSIZE)
	{
		/* two all-zero blocks mark EOF */
		if (t->th_buf.name[0] == '\0')
		{
			num_zero_blocks++;
			if (!BIT_ISSET(t->options, TAR_IGNORE_EOT)
			    && num_zero_blocks >= 2)
				return 0;	/* EOF */
			else
				continue;
		}

		/* verify magic and version */
		if (BIT_ISSET(t->options, TAR_CHECK_MAGIC)
		    && strncmp(t->th_buf.magic, TMAGIC, TMAGLEN - 1) != 0)
		{
#ifdef DEBUG
			puts("!!! unknown magic value in tar header");
#endif
			return -2;
		}

		if (BIT_ISSET(t->options, TAR_CHECK_VERSION)
		    && strncmp(t->th_buf.version, TVERSION, TVERSLEN) != 0)
		{
#ifdef DEBUG
			puts("!!! unknown version value in tar header");
#endif
			return -2;
		}

		/* check chksum */
		if (!BIT_ISSET(t->options, TAR_IGNORE_CRC)
		    && !th_crc_ok(t))
		{
#ifdef DEBUG
			puts("!!! tar header checksum error");
#endif
			return -2;
		}

		break;
	}

#ifdef DEBUG
	printf("<== th_read_internal(): returning %d\n", i);
#endif
	return i;
}


/* wrapper function for th_read_internal() to handle GNU extensions */
int
th_read(TAR *t)
{
	int i;
	size_t sz, j, blocks;
	char *ptr;

#ifdef DEBUG
	printf("==> th_read(t=0x%lx)\n", t);
#endif

	if (t->th_buf.gnu_longname != NULL)
		free(t->th_buf.gnu_longname);
	if (t->th_buf.gnu_longlink != NULL)
		free(t->th_buf.gnu_longlink);
	if (t->th_buf.selinux_context != NULL)
		free(t->th_buf.selinux_context);
#ifdef HAVE_EXT4_CRYPT
	if (t->th_buf.e4crypt_policy != NULL) {
		free(t->th_buf.e4crypt_policy);
	}
#endif
	if (t->th_buf.has_cap_data)
	{
		memset(&t->th_buf.cap_data, 0, sizeof(struct vfs_cap_data));
		t->th_buf.has_cap_data = 0;
	}
	t->th_buf.has_user_default = 0;
	t->th_buf.has_user_cache = 0;
	t->th_buf.has_user_code_cache = 0;

	memset(&(t->th_buf), 0, sizeof(struct tar_header));

	i = th_read_internal(t);
	if (i == 0)
		return 1;
	else if (i != T_BLOCKSIZE)
	{
		if (i != -1)
			errno = EINVAL;
		return -1;
	}

	/* check for GNU long link extention */
	if (TH_ISLONGLINK(t))
	{
		sz = th_get_size(t);
		blocks = (sz / T_BLOCKSIZE) + (sz % T_BLOCKSIZE ? 1 : 0);
		if (blocks > ((size_t)-1 / T_BLOCKSIZE))
		{
			errno = E2BIG;
			return -1;
		}
#ifdef DEBUG
		printf("    th_read(): GNU long linkname detected "
		       "(%ld bytes, %d blocks)\n", sz, blocks);
#endif
		t->th_buf.gnu_longlink = (char *)malloc(blocks * T_BLOCKSIZE);
		if (t->th_buf.gnu_longlink == NULL)
			return -1;

		for (j = 0, ptr = t->th_buf.gnu_longlink; j < blocks;
		     j++, ptr += T_BLOCKSIZE)
		{
#ifdef DEBUG
			printf("    th_read(): reading long linkname "
			       "(%d blocks left, ptr == %ld)\n", blocks-j, ptr);
#endif
			i = tar_block_read(t, ptr);
			if (i != T_BLOCKSIZE)
			{
				if (i != -1)
					errno = EINVAL;
				return -1;
			}
#ifdef DEBUG
			printf("    th_read(): read block == \"%s\"\n", ptr);
#endif
		}
#ifdef DEBUG
		printf("    th_read(): t->th_buf.gnu_longlink == \"%s\"\n",
		       t->th_buf.gnu_longlink);
#endif

		i = th_read_internal(t);
		if (i != T_BLOCKSIZE)
		{
			if (i != -1)
				errno = EINVAL;
			return -1;
		}
	}

	/* check for GNU long name extention */
	if (TH_ISLONGNAME(t))
	{
		sz = th_get_size(t);
		blocks = (sz / T_BLOCKSIZE) + (sz % T_BLOCKSIZE ? 1 : 0);
		if (blocks > ((size_t)-1 / T_BLOCKSIZE))
		{
			errno = E2BIG;
			return -1;
		}
#ifdef DEBUG
		printf("    th_read(): GNU long filename detected "
		       "(%ld bytes, %d blocks)\n", sz, blocks);
#endif
		t->th_buf.gnu_longname = (char *)malloc(blocks * T_BLOCKSIZE);
		if (t->th_buf.gnu_longname == NULL)
			return -1;

		for (j = 0, ptr = t->th_buf.gnu_longname; j < blocks;
		     j++, ptr += T_BLOCKSIZE)
		{
#ifdef DEBUG
			printf("    th_read(): reading long filename "
			       "(%d blocks left, ptr == %ld)\n", blocks-j, ptr);
#endif
			i = tar_block_read(t, ptr);
			if (i != T_BLOCKSIZE)
			{
				if (i != -1)
					errno = EINVAL;
				return -1;
			}
#ifdef DEBUG
			printf("    th_read(): read block == \"%s\"\n", ptr);
#endif
		}
#ifdef DEBUG
		printf("    th_read(): t->th_buf.gnu_longname == \"%s\"\n",
		       t->th_buf.gnu_longname);
#endif

		i = th_read_internal(t);
		if (i != T_BLOCKSIZE)
		{
			if (i != -1)
				errno = EINVAL;
			return -1;
		}
	}

	// Extended headers (selinux contexts, posix file capabilities, ext4 encryption policies)
	while(TH_ISEXTHEADER(t) || TH_ISPOLHEADER(t))
	{
		sz = th_get_size(t);

		if(sz >= T_BLOCKSIZE) // Not supported
		{
#ifdef DEBUG
			printf("    th_read(): Extended header is too long!\n");
#endif
		}
		else
		{
			char buf[T_BLOCKSIZE];
			i = tar_block_read(t, buf);
			if (i != T_BLOCKSIZE)
			{
				if (i != -1)
					errno = EINVAL;
				return -1;
			}

			// To be sure
			buf[T_BLOCKSIZE-1] = 0;

			int len = strlen(buf);
			// posix capabilities
			char *start = strstr(buf, CAPABILITIES_TAG);
			if (start && start+CAPABILITIES_TAG_LEN < buf+len)
			{
				start += CAPABILITIES_TAG_LEN;
				memcpy(&t->th_buf.cap_data, start, sizeof(struct vfs_cap_data));
				t->th_buf.has_cap_data = 1;
#ifdef DEBUG
				printf("    th_read(): Posix capabilities detected\n");
#endif
			} // end posix capabilities
			// selinux contexts
			start = strstr(buf, SELINUX_TAG);
			if (start && start+SELINUX_TAG_LEN < buf+len)
			{
				start += SELINUX_TAG_LEN;
				char *end = strchr(start, '\n');
				if(end)
				{
					t->th_buf.selinux_context = strndup(start, end-start);
#ifdef DEBUG
					printf("    th_read(): SELinux context xattr detected: %s\n", t->th_buf.selinux_context);
#endif
				}
			} // end selinux contexts
			// android user.default xattr
			start = strstr(buf, ANDROID_USER_DEFAULT_TAG);
			if (start)
			{
				t->th_buf.has_user_default = 1;
#ifdef DEBUG
				printf("    th_read(): android user.default xattr detected\n");
#endif
			} // end android user.default xattr
			// android user.inode_cache xattr
			start = strstr(buf, ANDROID_USER_CACHE_TAG);
			if (start)
			{
				t->th_buf.has_user_cache = 1;
#ifdef DEBUG
				printf("    th_read(): android user.inode_cache xattr detected\n");
#endif
			} // end android user.inode_cache xattr
			// android user.inode_code_cache xattr
			start = strstr(buf, ANDROID_USER_CODE_CACHE_TAG);
			if (start)
			{
				t->th_buf.has_user_code_cache = 1;
#ifdef DEBUG
				printf("    th_read(): android user.inode_code_cache xattr detected\n");
#endif
			} // end android user.inode_code_cache xattr
#ifdef HAVE_EXT4_CRYPT
			start = strstr(buf, E4CRYPT_TAG);
			if (start && start+E4CRYPT_TAG_LEN < buf+len)
			{
				start += E4CRYPT_TAG_LEN;
				char *end = strchr(start, '\n');
				if(end)
				{
				    t->th_buf.e4crypt_policy = strndup(start, end-start);
#ifdef DEBUG
				    printf("    th_read(): E4Crypt policy detected: %s\n", t->th_buf.e4crypt_policy);
#endif
				}
			}
#endif // HAVE_EXT4_CRYPT
		}

		i = th_read_internal(t);
		if (i != T_BLOCKSIZE)
		{
			if (i != -1)
				errno = EINVAL;
			return -1;
		}
	}

	return 0;
}

/* write an extended block */
static int
th_write_extended(TAR *t, char* buf, uint64_t sz)
{
	char type2;
	uint64_t sz2;
	int i;

	/* save old size and type */
	type2 = t->th_buf.typeflag;
	sz2 = th_get_size(t);

	/* write out initial header block with fake size and type */
	t->th_buf.typeflag = TH_EXT_TYPE;

	if(sz >= T_BLOCKSIZE) // impossible
	{
		errno = EINVAL;
		return -1;
	}

	th_set_size(t, sz);
	th_finish(t);
	i = tar_block_write(t, &(t->th_buf));
	if (i != T_BLOCKSIZE)
	{
		if (i != -1)
			errno = EINVAL;
		return -1;
	}

	i = tar_block_write(t, buf);
	if (i != T_BLOCKSIZE)
	{
		if (i != -1)
			errno = EINVAL;
		return -1;
	}

	/* reset type and size to original values */
	t->th_buf.typeflag = type2;
	th_set_size(t, sz2);
	memset(buf, 0, T_BLOCKSIZE);
	return 0;
}

/* write a header block */
int
th_write(TAR *t)
{
	int i, j;
	char type2;
	uint64_t sz, sz2, total_sz = 0;
	char *ptr;
	char buf[T_BLOCKSIZE];

#ifdef DEBUG
	printf("==> th_write(TAR=\"%s\")\n", t->pathname);
	th_print(t);
#endif

	if ((t->options & TAR_GNU) && t->th_buf.gnu_longlink != NULL)
	{
#ifdef DEBUG
		printf("th_write(): using gnu_longlink (\"%s\")\n",
		       t->th_buf.gnu_longlink);
#endif
		/* save old size and type */
		type2 = t->th_buf.typeflag;
		sz2 = th_get_size(t);

		/* write out initial header block with fake size and type */
		t->th_buf.typeflag = GNU_LONGLINK_TYPE;
		sz = strlen(t->th_buf.gnu_longlink);
		th_set_size(t, sz);
		th_finish(t);
		i = tar_block_write(t, &(t->th_buf));
		if (i != T_BLOCKSIZE)
		{
			if (i != -1)
				errno = EINVAL;
			return -1;
		}

		/* write out extra blocks containing long name */
		for (j = (sz / T_BLOCKSIZE) + (sz % T_BLOCKSIZE ? 1 : 0),
		     ptr = t->th_buf.gnu_longlink; j > 1;
		     j--, ptr += T_BLOCKSIZE)
		{
			i = tar_block_write(t, ptr);
			if (i != T_BLOCKSIZE)
			{
				if (i != -1)
					errno = EINVAL;
				return -1;
			}
		}
		memset(buf, 0, T_BLOCKSIZE);
		strncpy(buf, ptr, T_BLOCKSIZE);
		i = tar_block_write(t, &buf);
		if (i != T_BLOCKSIZE)
		{
			if (i != -1)
				errno = EINVAL;
			return -1;
		}

		/* reset type and size to original values */
		t->th_buf.typeflag = type2;
		th_set_size(t, sz2);
	}

	if ((t->options & TAR_GNU) && t->th_buf.gnu_longname != NULL)
	{
#ifdef DEBUG
		printf("th_write(): using gnu_longname (\"%s\")\n",
		       t->th_buf.gnu_longname);
#endif
		/* save old size and type */
		type2 = t->th_buf.typeflag;
		sz2 = th_get_size(t);

		/* write out initial header block with fake size and type */
		t->th_buf.typeflag = GNU_LONGNAME_TYPE;
		sz = strlen(t->th_buf.gnu_longname);
		th_set_size(t, sz);
		th_finish(t);
		i = tar_block_write(t, &(t->th_buf));
		if (i != T_BLOCKSIZE)
		{
			if (i != -1)
				errno = EINVAL;
			return -1;
		}

		/* write out extra blocks containing long name */
		for (j = (sz / T_BLOCKSIZE) + (sz % T_BLOCKSIZE ? 1 : 0),
		     ptr = t->th_buf.gnu_longname; j > 1;
		     j--, ptr += T_BLOCKSIZE)
		{
			i = tar_block_write(t, ptr);
			if (i != T_BLOCKSIZE)
			{
				if (i != -1)
					errno = EINVAL;
				return -1;
			}
		}
		memset(buf, 0, T_BLOCKSIZE);
		strncpy(buf, ptr, T_BLOCKSIZE);
		i = tar_block_write(t, &buf);
		if (i != T_BLOCKSIZE)
		{
			if (i != -1)
				errno = EINVAL;
			return -1;
		}

		/* reset type and size to original values */
		t->th_buf.typeflag = type2;
		th_set_size(t, sz2);
	}

	memset(buf, 0, T_BLOCKSIZE);
	ptr = buf;

	if((t->options & TAR_STORE_SELINUX) && t->th_buf.selinux_context != NULL)
	{
#ifdef DEBUG
		printf("th_write(): using selinux_context (\"%s\")\n",
		       t->th_buf.selinux_context);
#endif
		/* setup size - EXT header has format "*size of this whole tag as ascii numbers* *space* *content* *newline* */
		//                                                       size   newline
		sz = SELINUX_TAG_LEN + strlen(t->th_buf.selinux_context) + 3  +    1;

		if(sz >= 100) // another ascci digit for size
			++sz;

		total_sz += sz;
		snprintf(ptr, T_BLOCKSIZE, "%d "SELINUX_TAG"%s\n", (int)sz, t->th_buf.selinux_context);
		ptr += sz;
	}

#ifdef HAVE_EXT4_CRYPT
	if((t->options & TAR_STORE_EXT4_POL) && t->th_buf.e4crypt_policy != NULL)
	{
#ifdef DEBUG
		printf("th_write(): using e4crypt_policy %s\n",
		       t->th_buf.e4crypt_policy);
#endif
		/* setup size - EXT header has format "*size of this whole tag as ascii numbers* *space* *content* *newline* */
		//                                                       size   newline
		sz = E4CRYPT_TAG_LEN + EXT4_KEY_DESCRIPTOR_HEX + 3  +    1;

		if(sz >= 100) // another ascci digit for size
			++sz;

		if (total_sz + sz >= T_BLOCKSIZE)
		{
			if (th_write_extended(t, &buf, total_sz))
				return -1;
			ptr = buf;
			total_sz = sz;
		}
		else
			total_sz += sz;

		snprintf(ptr, T_BLOCKSIZE, "%d "E4CRYPT_TAG"%s", (int)sz, t->th_buf.e4crypt_policy);
		char *nlptr = ptr + sz - 1;
		*nlptr = '\n';
		ptr += sz;
	}
#endif

	if((t->options & TAR_STORE_POSIX_CAP) && t->th_buf.has_cap_data)
	{
#ifdef DEBUG
		printf("th_write(): has a posix capability\n");
#endif
		sz = CAPABILITIES_TAG_LEN + sizeof(struct vfs_cap_data) + 3 + 1;

		if(sz >= 100) // another ascci digit for size
			++sz;

		if (total_sz + sz >= T_BLOCKSIZE)
		{
			if (th_write_extended(t, &buf, total_sz))
				return -1;
			ptr = buf;
			total_sz = sz;
		}
		else
			total_sz += sz;

		snprintf(ptr, T_BLOCKSIZE, "%d "CAPABILITIES_TAG, (int)sz);
		memcpy(ptr + CAPABILITIES_TAG_LEN + 3, &t->th_buf.cap_data, sizeof(struct vfs_cap_data));
		char *nlptr = ptr + sz - 1;
		*nlptr = '\n';
		ptr += sz;
	}
	if (t->options & TAR_STORE_ANDROID_USER_XATTR)
	{
		if (t->th_buf.has_user_default) {
#ifdef DEBUG
			printf("th_write(): has android user.default xattr\n");
#endif
			sz = ANDROID_USER_DEFAULT_TAG_LEN + 3 + 1;

			if (total_sz + sz >= T_BLOCKSIZE)
			{
				if (th_write_extended(t, &buf, total_sz))
					return -1;
				ptr = buf;
				total_sz = sz;
			}
			else
				total_sz += sz;

			snprintf(ptr, T_BLOCKSIZE, "%d "ANDROID_USER_DEFAULT_TAG, (int)sz);
			char *nlptr = ptr + sz - 1;
			*nlptr = '\n';
			ptr += sz;
		}
		if (t->th_buf.has_user_cache) {
#ifdef DEBUG
			printf("th_write(): has android user.inode_cache xattr\n");
#endif
			sz = ANDROID_USER_CACHE_TAG_LEN + 3 + 1;

			if (total_sz + sz >= T_BLOCKSIZE)
			{
				if (th_write_extended(t, &buf, total_sz))
					return -1;
				ptr = buf;
				total_sz = sz;
			}
			else
				total_sz += sz;

			snprintf(ptr, T_BLOCKSIZE, "%d "ANDROID_USER_CACHE_TAG, (int)sz);
			char *nlptr = ptr + sz - 1;
			*nlptr = '\n';
			ptr += sz;
		}
		if (t->th_buf.has_user_code_cache) {
#ifdef DEBUG
			printf("th_write(): has android user.inode_code_cache xattr\n");
#endif
			sz = ANDROID_USER_CODE_CACHE_TAG_LEN + 3 + 1;

			if (total_sz + sz >= T_BLOCKSIZE)
			{
				if (th_write_extended(t, &buf, total_sz))
					return -1;
				ptr = buf;
				total_sz = sz;
			}
			else
				total_sz += sz;

			snprintf(ptr, T_BLOCKSIZE, "%d "ANDROID_USER_CODE_CACHE_TAG, (int)sz);
			char *nlptr = ptr + sz - 1;
			*nlptr = '\n';
			ptr += sz;
		}
	}
	if (total_sz > 0 && th_write_extended(t, &buf, total_sz)) // write any outstanding tar extended header
		return -1;

	th_finish(t);

#ifdef DEBUG
	/* print tar header */
	th_print(t);
#endif

	i = tar_block_write(t, &(t->th_buf));
	if (i != T_BLOCKSIZE)
	{
		if (i != -1)
			errno = EINVAL;
		return -1;
	}

#ifdef DEBUG
	puts("th_write(): returning 0");
#endif
	return 0;
}


