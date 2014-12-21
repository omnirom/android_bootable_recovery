/*
	Copyright 2012 bigbiff/Dees_Troy TeamWin
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

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <string.h>
#include <libgen.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include "gui/rapidxml.hpp"
#include "fixPermissions.hpp"
#include "twrp-functions.hpp"
#include "twcommon.h"
#ifdef HAVE_SELINUX
#include "selinux/selinux.h"
#include "selinux/label.h"
#include "selinux/android.h"
#include "selinux/label.h"
#endif

using namespace std;
using namespace rapidxml;

static const mode_t kMode_0600 = 0600; // S_IRUSR | S_IWUSR
static const mode_t kMode_0640 = 0640; // S_IRUSR | S_IWUSR | S_IRGRP
static const mode_t kMode_0644 = 0644; // S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH
static const mode_t kMode_0660 = 0660; // S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP
static const mode_t kMode_0755 = 0755; // S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH
static const mode_t kMode_0771 = 0771; // S_IRWXU | S_IRWXG | S_IXOTH

fixPermissions::fixPermissions() : head(NULL) {
}

fixPermissions::~fixPermissions() {
	deletePackages();
}

#ifdef HAVE_SELINUX
struct selabel_handle *sehandle;
struct selinux_opt selinux_options[] = {
	{ SELABEL_OPT_PATH, "/file_contexts" }
};

int fixPermissions::restorecon(string entry, struct stat *sb) {
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

int fixPermissions::fixDataDataContexts(void) {
	string dir = "/data/data/";
	sehandle = selabel_open(SELABEL_CTX_FILE, selinux_options, 1);
	if (!sehandle) {
		LOGINFO("Unable to open /file_contexts\n");
		return 0;
	}
	if (TWFunc::Path_Exists(dir)) {
		fixContextsRecursively(dir, 0);
	}
	selabel_close(sehandle);
	return 0;
}

int fixPermissions::fixContextsRecursively(string name, int level) {
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

int fixPermissions::fixDataInternalContexts(void) {
	DIR *d;
	struct dirent *de;
	struct stat sb;
	string dir, androiddir;
	sehandle = selabel_open(SELABEL_CTX_FILE, selinux_options, 1);
	if (!sehandle) {
		LOGINFO("Unable to open /file_contexts\n");
		return 0;
	}
	// TODO: what about /data/media/1 etc.?
	if (TWFunc::Path_Exists("/data/media/0"))
		dir = "/data/media/0";
	else
		dir = "/data/media";
	if (!TWFunc::Path_Exists(dir)) {
		LOGINFO("fixDataInternalContexts: '%s' does not exist!\n", dir.c_str());
		return 0;
	}
	LOGINFO("Fixing %s contexts\n", dir.c_str());
	restorecon(dir, &sb);
	d = opendir(dir.c_str());

	while (( de = readdir(d)) != NULL) {
		stat(de->d_name, &sb);
		string f;
		f = dir + "/" + de->d_name;
		restorecon(f, &sb);
	}
	closedir(d);

	androiddir = dir + "/Android/";
	if (TWFunc::Path_Exists(androiddir)) {
		fixContextsRecursively(androiddir, 0);
	}
	selabel_close(sehandle);
	return 0;
}
#endif

int fixPermissions::fixPerms(bool enable_debug, bool remove_data_for_missing_apps) {
	string packageFile = "/data/system/packages.xml";
	debug = enable_debug;
	remove_data = remove_data_for_missing_apps;
	bool multi_user = TWFunc::Path_Exists("/data/user");

	if (!(TWFunc::Path_Exists(packageFile))) {
		gui_print("Can't check permissions\n");
		gui_print("after Factory Reset.\n");
		gui_print("Please boot rom and try\n");
		gui_print("again after you reboot into\n");
		gui_print("recovery.\n");
		return -1;
	}

	gui_print("Fixing permissions...\nLoading packages...\n");
	if ((getPackages(packageFile)) != 0) {
		return -1;
	}

	gui_print("Fixing app permissions...\n");
	if (fixApps() != 0) {
		return -1;
	}

	if (multi_user) {
		DIR *d = opendir("/data/user");
		string new_path, user_id;

		if (d == NULL) {
			LOGERR("Error opening '/data/user'\n");
			return -1;
		}

		if (d) {
			struct dirent *p;
			while ((p = readdir(d))) {
				if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, ".."))
					continue;

				new_path = "/data/user/";
				new_path.append(p->d_name);
				user_id = "u";
				user_id += p->d_name;
				user_id += "_";
				if (p->d_type == DT_LNK) {
					char link[512], realPath[512];
					strcpy(link, new_path.c_str());
					memset(realPath, 0, sizeof(realPath));
					while (readlink(link, realPath, sizeof(realPath)) > 0) {
						strcpy(link, realPath);
						memset(realPath, 0, sizeof(realPath));
					}
					new_path = link;
				} else if (p->d_type != DT_DIR) {
					continue;
				} else {
					new_path.append("/");
					// We're probably going to need to fix permissions on multi user but
					// it will have to wait for another time. Need to figure out where
					// the uid and gid is stored for other users.
					continue;
				}
				gui_print("Fixing %s permissions...\n", new_path.c_str());
				if ((fixDataData(new_path)) != 0) {
					closedir(d);
					return -1;
				}
			}
			closedir(d);
		}
	} else {
		gui_print("Fixing /data/data permissions...\n");
		if ((fixDataData("/data/data/")) != 0) {
			return -1;
		}
	}
	gui_print("Done fixing permissions.\n");
	return 0;
}

int fixPermissions::fixContexts()
{
#ifdef HAVE_SELINUX
	gui_print("Fixing /data/data/ contexts.\n");
	fixDataDataContexts();
	fixDataInternalContexts();
	gui_print("Done fixing contexts.\n");
	return 0;
#endif
	gui_print("Not fixing SELinux contexts; support not compiled in.\n");
	return -1;
}

int fixPermissions::pchown(string fn, int puid, int pgid) {
	LOGINFO("Fixing %s, uid: %d, gid: %d\n", fn.c_str(), puid, pgid);
	if (chown(fn.c_str(), puid, pgid) != 0) {
		LOGERR("Unable to chown '%s' %i %i\n", fn.c_str(), puid, pgid);
		return -1;
	}
	return 0;
}

int fixPermissions::pchmod(string fn, mode_t mode) {
	LOGINFO("Fixing %s, mode: %o\n", fn.c_str(), mode);

	if (chmod(fn.c_str(), mode) != 0) {
		LOGERR("Unable to chmod '%s' %o\n", fn.c_str(), mode);
		return -1;
	}

	return 0;
}

int fixPermissions::fixApps() {
	package* temp = head;
	while (temp != NULL) {
		struct stat st;
		if (stat(temp->codePath.c_str(), &st) == 0) {
			int new_uid = 0;
			int new_gid = 0;
			mode_t perms = 0;
			bool fix = false;
			if (temp->appDir.compare("/system/app") == 0 || temp->appDir.compare("/system/priv-app") == 0) {
				fix = true;
				new_uid = 0;
				new_gid = 0;
				perms = kMode_0644;
			} else if (temp->appDir.compare("/data/app") == 0 || temp->appDir.compare("/sd-ext/app") == 0) {
				fix = true;
				new_uid = 1000;
				new_gid = 1000;
				perms = kMode_0644;
			} else if (temp->appDir.compare("/data/app-private") == 0 || temp->appDir.compare("/sd-ext/app-private") == 0) {
				fix = true;
				new_uid = 1000;
				new_gid = temp->gid;
				perms = kMode_0640;
			} else
				fix = false;
			if (fix) {
				if (debug) {
					LOGINFO("Looking at '%s'\n", temp->codePath.c_str());
					LOGINFO("Fixing permissions on '%s'\n", temp->pkgName.c_str());
					LOGINFO("Directory: '%s'\n", temp->appDir.c_str());
					LOGINFO("Original package owner: %d, group: %d\n", temp->uid, temp->gid);
				}
				if (S_ISDIR(st.st_mode)) {
					// Android 5.0 introduced codePath pointing to a directory instead of the apk itself
					// TODO: check what this should do
					if (fixDir(temp->codePath, new_uid, new_gid, kMode_0755, new_uid, new_gid, perms) != 0)
						return -1;
				} else {
					if (pchown(temp->codePath, new_uid, new_gid) != 0)
						return -1;
					if (pchmod(temp->codePath, perms) != 0)
						return -1;
				}
			}
		} else if (remove_data) {
			//Remove data directory since app isn't installed
			string datapath = "/data/data/" + temp->dDir;
			if (TWFunc::Path_Exists(datapath) && temp->appDir.size() >= 9 && temp->appDir.substr(0, 9) != "/mnt/asec") {
				if (debug)
					LOGINFO("Looking at '%s', removing data dir: '%s', appDir: '%s'", temp->codePath.c_str(), datapath.c_str(), temp->appDir.c_str());
				if (TWFunc::removeDir(datapath, false) != 0) {
					LOGINFO("Unable to removeDir '%s'\n", datapath.c_str());
					return -1;
				}
			}
		}
		temp = temp->next;
	}
	return 0;
}

int fixPermissions::fixAllFiles(string directory, int uid, int gid, mode_t file_perms) {
	vector <string> files;
	string file;

	files = listAllFiles(directory);
	for (unsigned i = 0; i < files.size(); ++i) {
		file = directory + "/";
		file.append(files.at(i));
		if (debug)
			LOGINFO("Looking at file '%s'\n", file.c_str());
		if (pchmod(file, file_perms) != 0)
			return -1;
		if (pchown(file, uid, gid) != 0)
			return -1;
	}
	return 0;
}

int fixPermissions::fixDir(const string& dir, int diruid, int dirgid, mode_t dirmode, int fileuid, int filegid, mode_t filemode)
{
	if (pchmod(dir.c_str(), dirmode) != 0)
		return -1;
	if (pchown(dir.c_str(), diruid, dirgid) != 0)
		return -1;
	if (fixAllFiles(dir, fileuid, filegid, filemode) != 0)
		return -1;
	return 0;
}

int fixPermissions::fixDataData(string dataDir) {
	package* temp = head;
	while (temp != NULL) {
		string dir = dataDir + temp->dDir;
		if (TWFunc::Path_Exists(dir)) {
			vector <string> dataDataDirs = listAllDirectories(dir);
			for (unsigned n = 0; n < dataDataDirs.size(); ++n) {
				string directory = dir + "/";
				directory.append(dataDataDirs.at(n));
				if (debug)
					LOGINFO("Looking at data directory: '%s'\n", directory.c_str());
				if (dataDataDirs.at(n) == ".") {
					if (fixDir(directory, temp->uid, temp->gid, kMode_0755, temp->uid, temp->gid, kMode_0755) != 0)
						return -1;
				}
				else if (dataDataDirs.at(n) == "..") {
					if (debug)
						LOGINFO("Skipping ..\n");
					continue;
				}
				// TODO: when any of these fails, do we really want to stop everything? 
				else if (dataDataDirs.at(n) == "lib") {
					if (fixDir(directory, 1000, 1000, kMode_0755, 1000, 1000, kMode_0755) != 0)
						return -1;
				}
				else if (dataDataDirs.at(n) == "shared_prefs") {
					if (fixDir(directory, temp->uid, temp->gid,kMode_0771, temp->uid, temp->gid, kMode_0660) != 0)
						return -1;
				}
				else if (dataDataDirs.at(n) == "databases") {
					if (fixDir(directory, temp->uid, temp->gid,kMode_0771, temp->uid, temp->gid, kMode_0660) != 0)
						return -1;
				}
				else if (dataDataDirs.at(n) == "cache") {
					if (fixDir(directory, temp->uid, temp->gid,kMode_0771, temp->uid, temp->gid, kMode_0600) != 0)
						return -1;
				}
				else {
					if (fixDir(directory, temp->uid, temp->gid,kMode_0771, temp->uid, temp->gid, kMode_0755) != 0)
						return -1;
				}
			}
		}
		temp = temp->next;
	}
	return 0;
}

// TODO: merge to listAllDirEntries(path, type)
vector <string> fixPermissions::listAllDirectories(string path) {
	DIR *dir = opendir(path.c_str());
	vector <string> dirs;

	if (dir == NULL) {
		LOGERR("Error opening '%s'\n", path.c_str());
		return dirs;
	}
	struct dirent *entry = readdir(dir);
	while (entry != NULL) {
		if (entry->d_type == DT_DIR)
			dirs.push_back(entry->d_name);
		entry = readdir(dir);
	}
	closedir(dir);
	return dirs;
}

vector <string> fixPermissions::listAllFiles(string path) {
	DIR *dir = opendir(path.c_str());
	vector <string> files;

	if (dir == NULL) {
		LOGERR("Error opening '%s'\n", path.c_str());
		return files;
	}
	struct dirent *entry = readdir(dir);
	while (entry != NULL) {
		if (entry->d_type == DT_REG)
			files.push_back(entry->d_name);
		entry = readdir(dir);
	}
	closedir(dir);
	return files;
}

void fixPermissions::deletePackages() {
	while (head) {
		package* temp = head;
		head = temp->next;
		delete temp;
	}
}

int fixPermissions::getPackages(const string& packageFile) {
	deletePackages();
	head = NULL;

	// TODO: simply skip all packages in /system/framework? or why are these excluded?
	vector <string> skip;
	skip.push_back("/system/framework/framework-res.apk");
	skip.push_back("/system/framework/com.htc.resources.apk");

	ifstream xmlFile(packageFile.c_str());
	xmlFile.seekg(0, ios::end);
	int len = (int) xmlFile.tellg();
	xmlFile.seekg(0, ios::beg);
	vector<char> xmlBuf(len + 1);
	xmlFile.read(&xmlBuf[0], len);
	xmlBuf[len] = '\0';
	xml_document<> pkgDoc;
	LOGINFO("Parsing packages.xml, size=%i...\n", len);
	pkgDoc.parse<parse_full>(&xmlBuf[0]);

	xml_node<> * pkgNode = pkgDoc.first_node("packages");
	if (pkgNode == NULL) {
		LOGERR("No packages found to fix.\n");
		return -1;
	}

	// Get packages
	for (xml_node<>* node = pkgNode->first_node(); node; node = node->next_sibling()) {
		if (node->type() != node_element)
			continue;
		string elementName = node->name();
		// we want <package> and <updated-package>
		if (!(elementName == "package" || elementName == "updated-package"))
			continue;

		xml_attribute<>* attName = node->first_attribute("name");
		if (!attName)
			continue;
		string name = attName->value();

		xml_attribute<>* attCodePath = node->first_attribute("codePath");
		if (!attCodePath)
		{
			LOGINFO("No codePath on %s, skipping.\n", name.c_str());
			continue;
		}
		string codePath = attCodePath->value();

		bool doskip = std::find(skip.begin(), skip.end(), codePath) != skip.end();
		if (doskip) {
			if (debug)
				LOGINFO("Skipping package %s\n", codePath.c_str());
			continue;
		}

		if (debug)
			LOGINFO("Loading pkg: %s\n", name.c_str());

		package* temp = new package;
		temp->pkgName = name;
		temp->codePath = codePath;
		temp->appDir = codePath;
		temp->dDir = name;
		xml_attribute<>* attUserId = node->first_attribute("userId");
		if (!attUserId)
			attUserId = node->first_attribute("sharedUserId");
		if (!attUserId) {
			LOGINFO("Problem with userID on %s\n", name.c_str());
		} else {
			temp->uid = atoi(attUserId->value());
			temp->gid = atoi(attUserId->value());
		}
		temp->next = head;
		head = temp;
	}

	if (head == NULL) {
		LOGERR("No package found to fix.\n");
		return -1;
	}

	return 0;
}
