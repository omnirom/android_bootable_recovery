/*
** Copyright 2008, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <string.h>
#include <linux/limits.h>
#include <errno.h>

#include "libtar.h"
#include "android_utils.h"

/* This code may come in handy later if we ever need to extend to storing more user.inode_* xattrs
#define USER_INODE_SEPARATOR "\0"
#define ANDROID_USER_INODE_XATTR_PREFIX "user.inode_"
#define ANDROID_USER_INODE_XATTR_PREFIX_LEN strlen(ANDROID_USER_INODE_XATTR_PREFIX)

char* scan_xattrs_for_user_inode (const char *realname, size_t *return_size)
{
	ssize_t size;
	char xattr_list[PATH_MAX];
	size = listxattr(realname, xattr_list, sizeof(xattr_list));
	if (size < 0) {
		return NULL;
	}
	char xattr[T_BLOCKSIZE];
	char *xattr_ptr;
	int first = 1;
	*return_size = 0;
	for (int i = 0; i < size; i++) {
		if (xattr_list[i]) {
			xattr_ptr = xattr_list + i;
			if (strncmp(xattr_ptr, ANDROID_USER_INODE_XATTR_PREFIX, ANDROID_USER_INODE_XATTR_PREFIX_LEN) == 0) {
				// found a user.inode xattr
				if (first) {
					first = 0;
					strcpy(xattr, xattr_ptr);
					*return_size = strlen(xattr_ptr);
				} else {
					char *ptr = xattr + *return_size;
					snprintf(ptr, T_BLOCKSIZE - *return_size, "%s", xattr_ptr);
					*return_size += strlen(xattr_ptr) + 1; // + 1 for null separator
					if (*return_size >= T_BLOCKSIZE) {
						*return_size = 0;
						return NULL;
					}
				}
			}
			i += strlen(xattr_ptr);
		}
	}
	if (first)
		return NULL;
	return strdup(xattr);
}*/

/*
 * get_path_inode and write_path_inode were taken from frameworks/native/cmds/installd/utils.cpp
 */

static int get_path_inode(const char* path, ino_t *inode) {
	struct stat buf;
	memset(&buf, 0, sizeof(buf));
	if (stat(path, &buf) != 0) {
		printf("failed to stat %s\n", path);
		return -1;
	}
	*inode = buf.st_ino;
	return 0;
}

/**
 * Write the inode of a specific child file into the given xattr on the
 * parent directory. This allows you to find the child later, even if its
 * name is encrypted.
 */
int write_path_inode(const char* parent, const char* name, const char* inode_xattr) {
	ino_t inode = 0;
	uint64_t inode_raw = 0;
	char path[PATH_MAX];
	snprintf(path, PATH_MAX, "%s/%s", parent, name);

	if (mkdirhier(path) == -1) {
		printf("failed to mkdirhier for %s\n", path);
		return -1;
	}

	if (get_path_inode(path, &inode) != 0) {
		return -1;
	}

	// Check to see if already set correctly
	if (getxattr(parent, inode_xattr, &inode_raw, sizeof(inode_raw)) == sizeof(inode_raw)) {
		if (inode_raw == inode) {
			// Already set correctly; skip writing
			return 0;
		}
	}

	inode_raw = inode;
	printf("setting %s on %s pointing to %s\n", inode_xattr, parent, path);
	if (setxattr(parent, inode_xattr, &inode_raw, sizeof(inode_raw), 0) != 0 && errno != EOPNOTSUPP) {
		printf("Failed to write xattr %s at %s (%s)\n", inode_xattr, parent, strerror(errno));
		return -1;
	}
	return 0;
}
