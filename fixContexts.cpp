/*
	Copyright 2012-2016 bigbiff/Dees_Troy TeamWin
	This file is part of TWRP/TeamWin Recovery Project.

	TWRP is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	TWRP is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with TWRP.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <string>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <cctype>
#include "fixContexts.hpp"
#include "twrp-functions.hpp"
#include "twcommon.h"
#ifdef HAVE_SELINUX
#include "selinux/selinux.h"
#include "selinux/label.h"
#include "selinux/android.h"
#include "selinux/label.h"
#endif

using namespace std;

#ifdef HAVE_SELINUX
struct selabel_handle *sehandle;
struct selinux_opt selinux_options[] = {
	{ SELABEL_OPT_PATH, "/file_contexts" }
};

int fixContexts::restorecon(string entry, struct stat *sb) {
	char *oldcontext, *newcontext;

	if (lgetfilecon(entry.c_str(), &oldcontext) < 0) {
		LOGINFO("Couldn't get selinux context for %s\n", entry.c_str());
		return -1;
	}
	if (selabel_lookup(sehandle, &newcontext, entry.c_str(), sb->st_mode) < 0) {
		LOGINFO("Couldn't lookup selinux context for %s\n", entry.c_str());
		return -1;
	}
	if (strcmp(oldcontext, newcontext) != 0) {
		LOGINFO("Relabeling %s from %s to %s\n", entry.c_str(), oldcontext, newcontext);
		if (lsetfilecon(entry.c_str(), newcontext) < 0) {
			LOGINFO("Couldn't label %s with %s: %s\n", entry.c_str(), newcontext, strerror(errno));
		}
	}
	freecon(oldcontext);
	freecon(newcontext);
	return 0;
}

int fixContexts::fixContextsRecursively(string name, int level) {
	DIR *d;
	struct dirent *de;
	struct stat sb;
	string path;

	if (!(d = opendir(name.c_str())))
		return -1;
	if (!(de = readdir(d)))
		return -1;

	do {
		if (de->d_type ==  DT_DIR) {
			if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
				continue;
			path = name + "/" + de->d_name;
			restorecon(path, &sb);
			fixContextsRecursively(path, level + 1);
		}
		else {
			path = name + "/" + de->d_name;
			restorecon(path, &sb);
		}
	} while ((de = readdir(d)));
	closedir(d);
	return 0;
}

int fixContexts::fixDataMediaContexts(string Mount_Point) {
	DIR *d;
	struct dirent *de;
	struct stat sb;

	LOGINFO("Fixing media contexts on '%s'\n", Mount_Point.c_str());

	sehandle = selabel_open(SELABEL_CTX_FILE, selinux_options, 1);
	if (!sehandle) {
		LOGINFO("Unable to open /file_contexts\n");
		return 0;
	}

	if (TWFunc::Path_Exists(Mount_Point + "/media/0")) {
		string dir = Mount_Point + "/media";
		if (!(d = opendir(dir.c_str()))) {
			LOGINFO("opendir failed (%s)\n", strerror(errno));
			return -1;
		}
		if (!(de = readdir(d))) {
			LOGINFO("readdir failed (%s)\n", strerror(errno));
			closedir(d);
			return -1;
		}

		do {
			if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0 || de->d_type != DT_DIR)
				continue;
			size_t len = strlen(de->d_name);
			bool is_numeric = true;
			char* folder_name = de->d_name;
			for (size_t i = 0; i < len; i++) {
				if (!isdigit(*folder_name)) {
					is_numeric = false;
					break;
				}
				folder_name++;
			}
			if (is_numeric) {
				dir = Mount_Point + "/media/";
				dir += de->d_name;
				restorecon(dir, &sb);
				fixContextsRecursively(dir, 0);
			}
		} while ((de = readdir(d)));
		closedir(d);
	} else if (TWFunc::Path_Exists(Mount_Point + "/media")) {
		restorecon(Mount_Point + "/media", &sb);
		fixContextsRecursively(Mount_Point + "/media", 0);
	} else {
		LOGINFO("fixDataMediaContexts: %s/media does not exist!\n", Mount_Point.c_str());
		return 0;
	}
	selabel_close(sehandle);
	return 0;
}

#else

int fixContexts::restorecon(string entry __unused, struct stat *sb __unused) {
	return -1;
}

int fixContexts::fixContextsRecursively(string name __unused, int level __unused) {
	return -1;
}

int fixContexts::fixDataMediaContexts(string Mount_Point __unused) {
	return -1;
}
#endif
