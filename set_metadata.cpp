/*
 * Copyright (C) 2014 The Team Win Recovery Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * The purpose of these functions is to try to get and set the proper
 * file permissions, SELinux contexts, owner, and group so that these
 * files are accessible when we boot up to normal Android via MTP and to
 * file manager apps. During early boot we try to read the contexts and
 * owner / group info from /data/media or from /data/media/0 and store
 * them in static variables. From there, we'll try to set the same
 * contexts, owner, and group information on most files we create during
 * operations like backups, copying the log, and MTP operations.
 */

#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>
#include "selinux/selinux.h"

static security_context_t selinux_context;
struct stat s;
static int has_stat = 0;

int tw_get_context(const char* filename) {
	if (lgetfilecon(filename, &selinux_context) >= 0) {
		printf("tw_get_context got selinux context: %s\n", selinux_context);
		return 0;
	} else {
		printf("tw_get_context failed to get selinux context\n");
		selinux_context = NULL;
	}
	return -1;
}

int tw_get_stat(const char* filename) {
	if (lstat(filename, &s) == 0) {
		has_stat = 1;
		return 0;
	}
	printf("tw_get_stat failed to lstat '%s'\n", filename);
	return -1;
}

int tw_get_default_metadata(const char* filename) {
	if (tw_get_context(filename) == 0 && tw_get_stat(filename) == 0)
		return 0;
	return -1;
}

// Most of this logging is disabled to prevent log spam if we are trying
// to set contexts and permissions on file systems that do not support
// these types of things (e.g. vfat / FAT / FAT32).
int tw_set_default_metadata(const char* filename) {
	int ret = 0;
	struct stat st;

	if (selinux_context == NULL) {
		//printf("selinux_context was null, '%s'\n", filename);
		ret = -1;
	} else if (lsetfilecon(filename, selinux_context) < 0) {
		//printf("Failed to set default contexts on '%s'.\n", filename);
		ret = -1;
	}

	if (lstat(filename, &st) == 0 && st.st_mode & S_IFREG && chmod(filename, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH) < 0) {
		//printf("Failed to chmod '%s'\n", filename);
		ret = -1;
	}

	if (has_stat && chown(filename, s.st_uid, s.st_gid) < 0) {
		//printf("Failed to lchown '%s'.\n", filename);
		ret = -1;
	}
	//printf("Done trying to set defaults on '%s'\n");
	return ret;
}
