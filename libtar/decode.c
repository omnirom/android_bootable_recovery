/*
**  Copyright 1998-2003 University of Illinois Board of Trustees
**  Copyright 1998-2003 Mark D. Roth
**  All rights reserved.
**
**  decode.c - libtar code to decode tar header blocks
**
**  Mark D. Roth <roth@uiuc.edu>
**  Campus Information Technologies and Educational Services
**  University of Illinois at Urbana-Champaign
*/

#include <internal.h>

#include <stdio.h>
#include <sys/param.h>
#include <pwd.h>
#include <grp.h>

#ifdef STDC_HEADERS
# include <string.h>
#endif


/* determine full path name */
char *
th_get_pathname(TAR *t)
{
	char filename[MAXPATHLEN];

	if (t->th_buf.gnu_longname)
		return strdup(t->th_buf.gnu_longname);

	if (t->th_buf.prefix[0] != '\0')
	{
		snprintf(filename, sizeof(filename), "%.155s/%.100s",
			 t->th_buf.prefix, t->th_buf.name);
		return strdup(filename);
	}

	snprintf(filename, sizeof(filename), "%.100s", t->th_buf.name);
	return strdup(filename);
}


uid_t
th_get_uid(TAR *t)
{
	int uid;
	struct passwd *pw;

	if (!(t->options & TAR_USE_NUMERIC_ID)) {
		pw = getpwnam(t->th_buf.uname);
		if (pw != NULL)
			return pw->pw_uid;
	}

	/* if the password entry doesn't exist */
	sscanf(t->th_buf.uid, "%o", &uid);
	return uid;
}


gid_t
th_get_gid(TAR *t)
{
	int gid;
	struct group *gr;

	if (!(t->options & TAR_USE_NUMERIC_ID)) {
		gr = getgrnam(t->th_buf.gname);
		if (gr != NULL)
			return gr->gr_gid;
	}

	/* if the group entry doesn't exist */
	sscanf(t->th_buf.gid, "%o", &gid);
	return gid;
}


unsigned int
th_get_mode(TAR *t)
{
	unsigned int mode;

	mode = (unsigned int)oct_to_int(t->th_buf.mode, sizeof(t->th_buf.mode));
	if (! (mode & S_IFMT))
	{
		switch (t->th_buf.typeflag)
		{
		case SYMTYPE:
			mode |= S_IFLNK;
			break;
		case CHRTYPE:
			mode |= S_IFCHR;
			break;
		case BLKTYPE:
			mode |= S_IFBLK;
			break;
		case DIRTYPE:
			mode |= S_IFDIR;
			break;
		case FIFOTYPE:
			mode |= S_IFIFO;
			break;
		case AREGTYPE:
			if (t->th_buf.name[strnlen(t->th_buf.name, T_NAMELEN) - 1] == '/')
			{
				mode |= S_IFDIR;
				break;
			}
			/* FALLTHROUGH */
		case LNKTYPE:
		case REGTYPE:
		default:
			mode |= S_IFREG;
		}
	}

	return mode;
}


