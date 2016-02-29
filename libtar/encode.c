/*
**  Copyright 1998-2003 University of Illinois Board of Trustees
**  Copyright 1998-2003 Mark D. Roth
**  All rights reserved.
**
**  encode.c - libtar code to encode tar header blocks
**
**  Mark D. Roth <roth@uiuc.edu>
**  Campus Information Technologies and Educational Services
**  University of Illinois at Urbana-Champaign
*/

#include <internal.h>

#include <stdio.h>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>

#ifdef STDC_HEADERS
# include <string.h>
# include <stdlib.h>
#endif


/* magic, version, and checksum */
void
th_finish(TAR *t)
{
	if (t->options & TAR_GNU)
	{
		/* we're aiming for this result, but must do it in
		 * two calls to avoid FORTIFY segfaults on some Linux
		 * systems:
		 *      strncpy(t->th_buf.magic, "ustar  ", 8);
		 */
		strncpy(t->th_buf.magic, "ustar ", 6);
		strncpy(t->th_buf.version, " ", 2);
	}
	else
	{
		strncpy(t->th_buf.version, TVERSION, TVERSLEN);
		strncpy(t->th_buf.magic, TMAGIC, TMAGLEN);
	}

	int_to_oct(th_crc_calc(t), t->th_buf.chksum, 8);
}


/* map a file mode to a typeflag */
void
th_set_type(TAR *t, mode_t mode)
{
	if (S_ISLNK(mode))
		t->th_buf.typeflag = SYMTYPE;
	if (S_ISREG(mode))
		t->th_buf.typeflag = REGTYPE;
	if (S_ISDIR(mode))
		t->th_buf.typeflag = DIRTYPE;
	if (S_ISCHR(mode))
		t->th_buf.typeflag = CHRTYPE;
	if (S_ISBLK(mode))
		t->th_buf.typeflag = BLKTYPE;
	if (S_ISFIFO(mode) || S_ISSOCK(mode))
		t->th_buf.typeflag = FIFOTYPE;
}


/* encode file path */
void
th_set_path(TAR *t, const char *pathname)
{
	char suffix[2] = "";
	char *tmp;
	size_t pathname_len = strlen(pathname);

#ifdef DEBUG
	printf("in th_set_path(th, pathname=\"%s\")\n", pathname);
#endif

	if (t->th_buf.gnu_longname != NULL)
		free(t->th_buf.gnu_longname);
	t->th_buf.gnu_longname = NULL;

	/* old archive compatibility (not needed for gnu): add trailing / to directories */
	if (pathname[pathname_len - 1] != '/' && TH_ISDIR(t))
		strcpy(suffix, "/");

	if (pathname_len >= T_NAMELEN && (t->options & TAR_GNU))
	{
		/* GNU-style long name (no file name length limit) */
		t->th_buf.gnu_longname = strdup(pathname);
		strncpy(t->th_buf.name, t->th_buf.gnu_longname, T_NAMELEN);
	}
	else if (pathname_len >= T_NAMELEN)
	{
		/* POSIX-style prefix field:
		 *   The maximum length of a file name is limited to 256 characters,
		 *   provided that the file name can be split at a directory separator
		 *   in two parts.  The first part being at most 155 bytes long and
		 *   the second part being at most 100 bytes long.  So, in most cases
		 *   the maximum file name length will be shorter than 256 characters.
		 */
		char tail_path[T_NAMELEN + 1];
		tmp = strchr(&(pathname[pathname_len - T_NAMELEN]), '/');
		if (tmp == NULL)
		{
			printf("!!! '/' not found in \"%s\"\n", pathname);
			return;
		}
		snprintf(tail_path, T_NAMELEN + 1, "%s%s", &tmp[1], suffix);
		strncpy(t->th_buf.name, tail_path, T_NAMELEN);

		/*
		 * first part, max = 155 == sizeof(t->th_buf.prefix) , include NULL if it fits
		 * trailing '/' is added during decode: decode.c/th_get_pathname()
		 */
		if (tmp - pathname >= 155) {
			strncpy(t->th_buf.prefix, pathname, 155);
		} else {
			snprintf(t->th_buf.prefix, (tmp - pathname + 1), "%s", pathname);
		}
	}
	else {
		/* any short name for all formats, or classic tar format (99 chars max) */
		snprintf(t->th_buf.name, T_NAMELEN, "%s%s", pathname, suffix);
	}

#ifdef DEBUG
	puts("returning from th_set_path()...");
#endif
}


/* encode link path */
void
th_set_link(TAR *t, const char *linkname)
{
#ifdef DEBUG
	printf("==> th_set_link(th, linkname=\"%s\")\n", linkname);
#endif

	if (strlen(linkname) >= T_NAMELEN && (t->options & TAR_GNU))
	{
		/* --format=gnu: GNU-style long name (no file name length limit) */
		t->th_buf.gnu_longlink = strdup(linkname);
		strcpy(t->th_buf.linkname, "././@LongLink");
	}
	else if (strlen(linkname) >= T_NAMELEN)
	{
		/* --format=ustar: 100 chars max limit for symbolic links */
		strncpy(t->th_buf.linkname, linkname, T_NAMELEN);
		if (t->th_buf.gnu_longlink != NULL)
			free(t->th_buf.gnu_longlink);
		t->th_buf.gnu_longlink = NULL;
	} else {
		/* all short links or v7 tar format: The maximum length of a symbolic link name is limited to 99 characters */
		snprintf(t->th_buf.linkname, T_NAMELEN, "%s", linkname);
		if (t->th_buf.gnu_longlink != NULL)
			free(t->th_buf.gnu_longlink);
		t->th_buf.gnu_longlink = NULL;
	}
}


/* encode device info */
void
th_set_device(TAR *t, dev_t device)
{
#ifdef DEBUG
	printf("th_set_device(): major = %d, minor = %d\n",
	       major(device), minor(device));
#endif
	int_to_oct(major(device), t->th_buf.devmajor, 8);
	int_to_oct(minor(device), t->th_buf.devminor, 8);
}


/* encode user info */
void
th_set_user(TAR *t, uid_t uid)
{
	struct passwd *pw;

	if (!(t->options & TAR_USE_NUMERIC_ID)) {
		pw = getpwuid(uid);
		if (pw != NULL)
			strlcpy(t->th_buf.uname, pw->pw_name, sizeof(t->th_buf.uname));
	}

	int_to_oct(uid, t->th_buf.uid, 8);
}


/* encode group info */
void
th_set_group(TAR *t, gid_t gid)
{
	struct group *gr;

	if (!(t->options & TAR_USE_NUMERIC_ID)) {
		gr = getgrgid(gid);
		if (gr != NULL)
			strlcpy(t->th_buf.gname, gr->gr_name, sizeof(t->th_buf.gname));
	}

	int_to_oct(gid, t->th_buf.gid, 8);
}


/* encode file mode */
void
th_set_mode(TAR *t, mode_t fmode)
{
	if (S_ISSOCK(fmode))
	{
		fmode &= ~S_IFSOCK;
		fmode |= S_IFIFO;
	}
	int_to_oct(fmode, (t)->th_buf.mode, 8);
}


void
th_set_from_stat(TAR *t, struct stat *s)
{
	th_set_type(t, s->st_mode);
	if (S_ISCHR(s->st_mode) || S_ISBLK(s->st_mode))
		th_set_device(t, s->st_rdev);
	th_set_user(t, s->st_uid);
	th_set_group(t, s->st_gid);
	th_set_mode(t, s->st_mode);
	th_set_mtime(t, s->st_mtime);
	if (S_ISREG(s->st_mode))
		th_set_size(t, s->st_size);
	else
		th_set_size(t, 0);
}
