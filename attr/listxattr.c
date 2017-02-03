/****************************************************************************
 |  (C) Copyright 2008 Novell, Inc. All Rights Reserved.
 |
 |  GPLv2: This program is free software; you can redistribute it
 |  and/or modify it under the terms of version 2 of the GNU General
 |  Public License as published by the Free Software Foundation.
 |
 |  This program is distributed in the hope that it will be useful,
 |  but WITHOUT ANY WARRANTY; without even the implied warranty of
 |  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 |  GNU General Public License for more details.
 +-------------------------------------------------------------------------*/
/*
 * NOTE from Dees_Troy: modified source to display values along with xattr names
 * Below code comments about usage are no longer accurate but came from the
 * original source code from the chromium project and combine features of
 * listxattr and getfattr:
 * https://chromium.googlesource.com/chromiumos/platform/punybench/+/factory-1235.B/file.m/listxattr.c
 * https://chromium.googlesource.com/chromiumos/platform/punybench/+/factory-1235.B/file.m/getxattr.c
 */
/*
 * LISTXATTR(2)             Linux Programmer's Manual            LISTXATTR(2)
 *
 *
 *
 * NAME
 *        listxattr, llistxattr, flistxattr - list extended attribute names
 *
 * SYNOPSIS
 *        #include <sys/types.h>
 *        #include <attr/xattr.h>
 *
 *        ssize_t listxattr (const char *path,
 *                             char *list, size_t size);
 *        ssize_t llistxattr (const char *path,
 *                             char *list, size_t size);
 *        ssize_t flistxattr (int filedes,
 *                             char *list, size_t size);
 *
 * DESCRIPTION
 *        Extended  attributes  are  name:value  pairs associated with inodes
 *        (files, directories, symlinks, etc).  They are  extensions  to  the
 *        normal  attributes which are associated with all inodes in the sys-
 *        tem (i.e. the stat(2)  data).   A  complete  overview  of  extended
 *        attributes concepts can be found in attr(5).
 *
 *        listxattr retrieves the list of extended attribute names associated
 *        with the given path in the filesystem.  The  list  is  the  set  of
 *        (NULL-terminated)  names,  one  after the other.  Names of extended
 *        attributes to which the calling process does not have access may be
 *        omitted  from  the  list.  The length of the attribute name list is
 *        returned.
 *
 *        llistxattr is identical to listxattr, except in the case of a  sym-
 *        bolic  link, where the list of names of extended attributes associ-
 *        ated with the link itself is retrieved, not the file that it refers
 *        to.
 *
 *        flistxattr is identical to listxattr, only the open file pointed to
 *        by filedes (as returned by open(2)) is  interrogated  in  place  of
 *        path.
 *
 *        A  single  extended  attribute  name  is  a  simple NULL-terminated
 *        string.  The name includes a namespace prefix; there  may  be  sev-
 *        eral, disjoint namespaces associated with an individual inode.
 *
 *        An  empty  buffer  of  size  zero can be passed into these calls to
 *        return the current size of the list of  extended  attribute  names,
 *        which  can be used to estimate the size of a buffer which is suffi-
 *        ciently large to hold the list of names.
 *
 * EXAMPLES
 *        The list of names is returned as an unordered array of  NULL-termi-
 *        nated  character  strings  (attribute  names  are separated by NULL
 *        characters), like this:
 *               user.name1\0system.name1\0user.name2\0
 *
 *        Filesystems like ext2, ext3 and  XFS  which  implement  POSIX  ACLs
 *        using extended attributes, might return a list like this:
 *               system.posix_acl_access\0system.posix_acl_default\0
 *
 * RETURN VALUE
 *        On  success,  a  positive number is returned indicating the size of
 *        the extended attribute name list.  On failure, -1 is  returned  and
 *        errno is set appropriately.
 *
 *        If  the  size  of  the list buffer is too small to hold the result,
 *        errno is set to ERANGE.
 *
 *        If extended attributes are not supported by the filesystem, or  are
 *        disabled, errno is set to ENOTSUP.
 *
 *        The errors documented for the stat(2) system call are also applica-
 *        ble here.
 *
 * AUTHORS
 *        Andreas Gruenbacher, <a.gruenbacher@computer.org> and the  SGI  XFS
 *        development  team,  <linux-xfs@oss.sgi.com>.   Please  send any bug
 *        reports or comments to these addresses.
 *
 * SEE ALSO
 *        getfattr(1),  setfattr(1),  getxattr(2),  open(2),  removexattr(2),
 *        setxattr(2), stat(2), attr(5)
 *
 *
 *
 * Dec 2001                    Extended Attributes               LISTXATTR(2)
 */
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/xattr.h>
//#include <eprintf.h>
//#include <puny.h>

/* dumpmem: dumps an n byte area of memory to screen */
void dumpmem (const void *mem, unsigned int n)
{
	const char *c = mem;
	unsigned i;
	int all_text = 1;
	if (n == 0) {
		printf("<empty>");
		return;
	}
	for (i = 0; i < n - 1; i++, c++) {
		if (!isprint(*c)) {
			all_text = 0;
			break;
		}
	}
	c = mem;
	if (all_text) {
		for (i = 0; i < n - 1; i++, c++) {
			putchar(*c);
		}
		return;
	} else {
		char hex[(n * 2) + 1];
		for(i = 0; i < n; i++, c++)
			sprintf(hex + (i * 2), "%02X", *c);
		hex[n] = 0;
		printf("0x%s", hex);
		return;
	}
}

void dump_list (char *file, char *list, ssize_t size)
{
	int	c;
	int	i;
	int	first = 1;
	int j = 0;
	char xattr[1024];
	char value[1024];
	ssize_t	size2;
	for (i = 0; i < size; i++) {
		c = list[i];
		if (c) {
			if (first) {
				putchar('\t');
				first = 0;
				j = 0;
			}
			putchar(c);
			xattr[j++] = list[i];
		} else {
			xattr[j] = '\0';
			size2 = getxattr(file, xattr, value, sizeof(value));
			if (size2 < 0) {
				printf("file=%s xattr=%s returned:", file, xattr);
			} else {
				putchar('=');
				dumpmem(value, size2);
			}
			putchar('\n');
			first = 1;
		}
	}
}
void usage (void)
{
	printf("listxattr <file>");
}
char	List[1<<17];
int main (int argc, char *argv[])
{
	ssize_t	size;
	if (argc < 2) {
		usage();
		exit(2);
	}
	size = listxattr(argv[1], List, sizeof(List));
	if (size == -1) {
		perror(argv[1]);
		exit(2);
	}
	printf("xattrs for %s:\n", argv[1]);
	dump_list(argv[1], List, size);
	return 0;
}
