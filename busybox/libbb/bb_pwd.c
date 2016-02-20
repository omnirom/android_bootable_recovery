/* vi: set sw=4 ts=4: */
/*
 * password utility routines.
 *
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 * Copyright (C) 2008 by Tito Ragusa <farmatito@tiscali.it>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */

#include "libbb.h"

/* TODO: maybe change API to return malloced data?
 * This will allow to stop using libc functions returning
 * pointers to static data (getpwuid)
 */

struct passwd* FAST_FUNC safegetpwnam(const char *name)
{
	struct passwd *pw = getpwnam(name);
#ifdef __BIONIC__
	if (pw && !pw->pw_passwd) {
		pw->pw_passwd = "";
	}
#endif
	return pw;
}

struct passwd* FAST_FUNC safegetpwuid(uid_t uid)
{
	struct passwd *pw = getpwuid(uid);
#ifdef __BIONIC__
	if (pw && !pw->pw_passwd) {
		pw->pw_passwd = "";
	}
#endif
	return pw;
}

struct passwd* FAST_FUNC xgetpwnam(const char *name)
{
	struct passwd *pw = safegetpwnam(name);
	if (!pw)
		bb_error_msg_and_die("unknown user %s", name);
	return pw;
}

struct group* FAST_FUNC xgetgrnam(const char *name)
{
	struct group *gr = getgrnam(name);
	if (!gr)
		bb_error_msg_and_die("unknown group %s", name);
	return gr;
}

struct passwd* FAST_FUNC xgetpwuid(uid_t uid)
{
	struct passwd *pw = safegetpwuid(uid);
	if (!pw)
		bb_error_msg_and_die("unknown uid %u", (unsigned)uid);
	return pw;
}

struct group* FAST_FUNC xgetgrgid(gid_t gid)
{
	struct group *gr = getgrgid(gid);
	if (!gr)
		bb_error_msg_and_die("unknown gid %u", (unsigned)gid);
	return gr;
}

char* FAST_FUNC xuid2uname(uid_t uid)
{
	struct passwd *pw = xgetpwuid(uid);
	return pw->pw_name;
}

char* FAST_FUNC xgid2group(gid_t gid)
{
	struct group *gr = xgetgrgid(gid);
	return gr->gr_name;
}

char* FAST_FUNC uid2uname(uid_t uid)
{
	struct passwd *pw = getpwuid(uid);
	return (pw) ? pw->pw_name : NULL;
}

char* FAST_FUNC gid2group(gid_t gid)
{
	struct group *gr = getgrgid(gid);
	return (gr) ? gr->gr_name : NULL;
}

char* FAST_FUNC uid2uname_utoa(uid_t uid)
{
	char *name = uid2uname(uid);
	return (name) ? name : utoa(uid);
}

char* FAST_FUNC gid2group_utoa(gid_t gid)
{
	char *name = gid2group(gid);
	return (name) ? name : utoa(gid);
}

long FAST_FUNC xuname2uid(const char *name)
{
	struct passwd *myuser;

	myuser = xgetpwnam(name);
	return myuser->pw_uid;
}

long FAST_FUNC xgroup2gid(const char *name)
{
	struct group *mygroup;

	mygroup = xgetgrnam(name);
	return mygroup->gr_gid;
}

unsigned long FAST_FUNC get_ug_id(const char *s,
		long FAST_FUNC (*xname2id)(const char *))
{
	unsigned long r;

	r = bb_strtoul(s, NULL, 10);
	if (errno)
		return xname2id(s);
	return r;
}

/* Experimental "mallocing" API.
 * The goal is nice: "we want to support a case when "guests" group is very large"
 * but the code is butt-ugly.
 */
#if 0
static char *find_latest(char last, char *cp)
{
	if (!cp)
		return last;
	cp += strlen(cp) + 1;
	if (last < cp)
		last = cp;
	return last;
}

struct group* FAST_FUNC xmalloc_getgrnam(const char *name)
{
	struct {
		struct group gr;
		// May still be not enough!
		char buf[64*1024 - sizeof(struct group) - 16];
	} *s;
	struct group *grp;
	int r;
	char *last;
	char **gr_mem;

	s = xmalloc(sizeof(*s));
	r = getgrnam_r(name, &s->gr, s->buf, sizeof(s->buf), &grp);
	if (!grp) {
		free(s);
		return grp;
	}
	last = find_latest(s->buf, grp->gr_name);
	last = find_latest(last, grp->gr_passwd);
	gr_mem = grp->gr_mem;
	while (*gr_mem)
		last = find_latest(last, *gr_mem++);
	gr_mem++; /* points past NULL */
	if (last < (char*)gr_mem)
		last = (char*)gr_mem;
//FIXME: what if we get not only truncated, but also moved here?
// grp->gr_name pointer and friends are invalid now!!!
	s = xrealloc(s, last - (char*)s);
	return grp;
}
#endif
