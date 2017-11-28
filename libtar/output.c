/*
**  Copyright 1998-2003 University of Illinois Board of Trustees
**  Copyright 1998-2003 Mark D. Roth
**  All rights reserved.
**
**  output.c - libtar code to print out tar header blocks
**
**  Mark D. Roth <roth@uiuc.edu>
**  Campus Information Technologies and Educational Services
**  University of Illinois at Urbana-Champaign
*/

#include <internal.h>

#include <stdio.h>
#include <stdlib.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <limits.h>
#include <sys/param.h>

#ifdef STDC_HEADERS
# include <string.h>
#endif

#ifdef HAVE_EXT4_CRYPT
# include "ext4crypt_tar.h"
#endif


#ifndef _POSIX_LOGIN_NAME_MAX
# define _POSIX_LOGIN_NAME_MAX	9
#endif


void
th_print(TAR *t)
{
	puts("\nPrinting tar header:");
	printf("  name     = \"%.100s\"\n", t->th_buf.name);
	printf("  mode     = \"%.8s\"\n", t->th_buf.mode);
	printf("  uid      = \"%.8s\"\n", t->th_buf.uid);
	printf("  gid      = \"%.8s\"\n", t->th_buf.gid);
	printf("  size     = \"%.12s\"\n", t->th_buf.size);
	printf("  mtime    = \"%.12s\"\n", t->th_buf.mtime);
	printf("  chksum   = \"%.8s\"\n", t->th_buf.chksum);
	printf("  typeflag = \'%c\'\n", t->th_buf.typeflag);
	printf("  linkname = \"%.100s\"\n", t->th_buf.linkname);
	printf("  magic    = \"%.6s\"\n", t->th_buf.magic);
	/*printf("  version  = \"%.2s\"\n", t->th_buf.version); */
	/*printf("  version[0] = \'%c\',version[1] = \'%c\'\n",
	       t->th_buf.version[0], t->th_buf.version[1]);*/
	printf("  uname    = \"%.32s\"\n", t->th_buf.uname);
	printf("  gname    = \"%.32s\"\n", t->th_buf.gname);
	printf("  devmajor = \"%.8s\"\n", t->th_buf.devmajor);
	printf("  devminor = \"%.8s\"\n", t->th_buf.devminor);
	printf("  prefix   = \"%.155s\"\n", t->th_buf.prefix);
	printf("  padding  = \"%.12s\"\n", t->th_buf.padding);
	printf("  gnu_longname = \"%s\"\n",
	       (t->th_buf.gnu_longname ? t->th_buf.gnu_longname : "[NULL]"));
	printf("  gnu_longlink = \"%s\"\n",
	       (t->th_buf.gnu_longlink ? t->th_buf.gnu_longlink : "[NULL]"));
#ifdef HAVE_EXT4_CRYPT
	printf("  eep = \"%s\"\n",
	       (t->th_buf.eep ? t->th_buf.eep->master_key_descriptor : "[NULL]"));
#endif
}


void
th_print_long_ls(TAR *t)
{
	char modestring[12];
	struct passwd *pw;
	struct group *gr;
	uid_t uid;
	gid_t gid;
	char username[_POSIX_LOGIN_NAME_MAX];
	char groupname[_POSIX_LOGIN_NAME_MAX];
	time_t mtime;
	struct tm *mtm;

#ifdef HAVE_STRFTIME
	char timebuf[18];
#else
	const char *months[] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
	};
#endif

	uid = th_get_uid(t);
	pw = getpwuid(uid);
	if ((t->options & TAR_USE_NUMERIC_ID) || pw == NULL)
		snprintf(username, sizeof(username), "%d", uid);
	else
		strlcpy(username, pw->pw_name, sizeof(username));

	gid = th_get_gid(t);
	gr = getgrgid(gid);
	if ((t->options & TAR_USE_NUMERIC_ID) || gr == NULL)
		snprintf(groupname, sizeof(groupname), "%d", gid);
	else
		strlcpy(groupname, gr->gr_name, sizeof(groupname));

	strmode(th_get_mode(t), modestring);
	printf("%.10s %-8.8s %-8.8s ", modestring, username, groupname);

	if (TH_ISCHR(t) || TH_ISBLK(t))
		printf(" %3d, %3d ", (int)th_get_devmajor(t), (int)th_get_devminor(t));
	else
		printf("%9ld ", (long)th_get_size(t));

	mtime = th_get_mtime(t);
	mtm = localtime(&mtime);
#ifdef HAVE_STRFTIME
	strftime(timebuf, sizeof(timebuf), "%h %e %H:%M %Y", mtm);
	printf("%s", timebuf);
#else
	printf("%.3s %2d %2d:%02d %4d",
	       months[mtm->tm_mon],
	       mtm->tm_mday, mtm->tm_hour, mtm->tm_min, mtm->tm_year + 1900);
#endif

	printf(" %s", th_get_pathname(t));

	if (TH_ISSYM(t) || TH_ISLNK(t))
	{
		if (TH_ISSYM(t))
			printf(" -> ");
		else
			printf(" link to ");
		if ((t->options & TAR_GNU) && t->th_buf.gnu_longlink != NULL)
			printf("%s", t->th_buf.gnu_longlink);
		else
			printf("%.100s", t->th_buf.linkname);
	}

	putchar('\n');
}


