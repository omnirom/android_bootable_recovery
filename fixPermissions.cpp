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
#include "gui/rapidxml.hpp"
#include "fixPermissions.hpp"
#include "twrp-functions.hpp"
#include "common.h"

using namespace std;
using namespace rapidxml;

int fixPermissions::fixPerms(bool enable_debug, bool remove_data_for_missing_apps) {
	packageFile = "/data/system/packages.xml";
	debug = enable_debug;
	remove_data = remove_data_for_missing_apps;

	if (!(TWFunc::Path_Exists(packageFile))) {
		ui_print("Can't check permissions\n");
		ui_print("after Factory Reset.\n");
		ui_print("Please boot rom and try\n");
		ui_print("again after you reboot into\n");
		ui_print("recovery.\n");
		return -1;
	}

	ui_print("Fixing permissions...\nLoading packages...\n");
	if ((getPackages()) != 0) {
		return -1;
	}
	
	ui_print("Fixing /system/app perissions...\n");
	if ((fixSystemApps()) != 0) {
		return -1;
	}

	ui_print("Fixing /data/app permisions...\n");
	if ((fixDataApps()) != 0) {
		return -1;
	}

	ui_print("Fixing /data/data permisions...\n");
	if ((fixDataData()) != 0) {
		return -1;
	}
	ui_print("Done fixing permissions.\n");
	return 0;
}

int fixPermissions::pchown(string fn, int puid, int pgid) {
	LOGI("Fixing %s, uid: %d, gid: %d\n", fn.c_str(), puid, pgid);
	if (chown(fn.c_str(), puid, pgid) != 0) {
		LOGE("Unable to chown '%s' %i %i\n", fn.c_str(), puid, pgid);
		return -1;
	}
	return 0;
}

int fixPermissions::pchmod(string fn, string mode) {
	long mask = 0;
	LOGI("Fixing %s, mode: %s\n", fn.c_str(), mode.c_str());
	for ( std::string::size_type n = 0; n < mode.length(); ++n) {
		if (n == 0) {
			if (mode[n] == '0')
				continue;
			else if (mode[n] == '1')
				mask = S_ISVTX;
			else if (mode[n] == '2')
				mask = S_ISGID;
		}
		else if (n == 1) {
			if (mode[n] == '7') {
				mask |= S_IRWXU;
			}
			if (mode[n] == '6') {
				mask |= S_IRUSR;
				mask |= S_IWUSR;
			}
			if (mode[n] == '5') {
				mask |= S_IRUSR;
				mask |= S_IXUSR;
			}
			if (mode[n] == '4')
				mask |= S_IRUSR;
			if (mode[n] == '3') {
				mask |= S_IWUSR;
				mask |= S_IRUSR;
			}
			if (mode[n] == '2')
				mask |= S_IWUSR;
			if (mode[n] == '1')
				mask |= S_IXUSR;
		}
		else if (n == 2) {
			if (mode[n] == '7') {
				mask |= S_IRWXG;
			}
			if (mode[n] == '6') {
				mask |= S_IRGRP;
				mask |= S_IWGRP;
			}
			if (mode[n] == '5') {
				mask |= S_IRGRP;
				mask |= S_IXGRP;
			}
			if (mode[n] == '4')
				mask |= S_IRGRP;
			if (mode[n] == '3') {
				mask |= S_IWGRP;
				mask |= S_IXGRP;
			}
			if (mode[n] == '2')
				mask |= S_IWGRP;
			if (mode[n] == '1')
				mask |= S_IXGRP;
		}
		else if (n == 3) {
			if (mode[n] == '7') {
				mask |= S_IRWXO;
			}
			if (mode[n] == '6') {
				mask |= S_IROTH;
				mask |= S_IWOTH;
			}
			if (mode[n] == '5') {
				mask |= S_IROTH;
				mask |= S_IXOTH;
			}
			if (mode[n] == '4')
				mask |= S_IROTH;
			if (mode[n] == '3') {
				mask |= S_IWOTH;
				mask |= S_IXOTH;
			}
			if (mode[n] == '2')
				mask |= S_IWOTH;
			if (mode[n] == '1')
				mask |= S_IXOTH;
		}
	} 

	if (chmod(fn.c_str(), mask) != 0) {
		LOGE("Unable to chmod '%s' %l\n", fn.c_str(), mask);
		return -1;
	}

	return 0;
}

int fixPermissions::removeDir(const string path) {
	DIR *d = opendir(path.c_str());
	int r = 0;
	string new_path;

	if (d == NULL) {
		LOGE("Error opening '%s'\n", path.c_str());
		return -1;
	}

	if (d) {
		struct dirent *p;
		while (!r && (p = readdir(d))) {
			if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, ".."))
				continue;

			new_path = path + "/";
			new_path.append(p->d_name);
			if (p->d_type == DT_DIR) {
				r = removeDir(new_path);
				if (!r)
					LOGI("Unable to removeDir '%s'\n", new_path.c_str());
			} else {
				r = unlink(new_path.c_str());
				if (!r)
					LOGI("Unable to unlink '%s'\n", new_path.c_str());
			}
		}
		closedir(d);
	}
	if (!r)
		r = rmdir(path.c_str());
	if (!r)
		LOGI("Unable to rmdir '%s'\n", path.c_str());

	return r;
}

int fixPermissions::fixSystemApps() {
	temp = head;
	while (temp != NULL) {
		if (TWFunc::Path_Exists(temp->codePath)) {
			if (temp->appDir.compare("/system/app") == 0) {
				if (debug)  {
					LOGI("Looking at '%s'\n", temp->codePath.c_str());
					LOGI("Fixing permissions on '%s'\n", temp->pkgName.c_str());
					LOGI("Directory: '%s'\n", temp->appDir.c_str());
					LOGI("Original package owner: %d, group: %d\n", temp->uid, temp->gid);
				}
				if (pchown(temp->codePath, 0, 0) != 0)
					return -1;
				if (pchmod(temp->codePath, "0644") != 0)
					return -1;
			}
		} else {
			//Remove data directory since app isn't installed
			if (remove_data && TWFunc::Path_Exists(temp->dDir) && temp->appDir.size() >= 9 && temp->appDir.substr(0, 9) != "/mnt/asec") {
				if (debug)
					LOGI("Looking at '%s', removing data dir: '%s', appDir: '%s'", temp->codePath.c_str(), temp->dDir.c_str(), temp->appDir.c_str());
				if (removeDir(temp->dDir) != 0) {
					LOGI("Unable to removeDir '%s'\n", temp->dDir.c_str());
					return -1;
				}
			}
		}
		temp = temp->next;
	}
	return 0;
}

int fixPermissions::fixDataApps() {
	bool fix = false;
	int new_gid = 0;
	string perms = "0000";

	temp = head;
	while (temp != NULL) {
		if (TWFunc::Path_Exists(temp->codePath)) {
			if (temp->appDir.compare("/data/app") == 0 || temp->appDir.compare("/sd-ext/app") == 0) {
				fix = true;
				new_gid = 1000;
				perms = "0644";
			} else if (temp->appDir.compare("/data/app-private") == 0 || temp->appDir.compare("/sd-ext/app-private") == 0) {
				fix = true;
				new_gid = temp->gid;
				perms = "0640";
			} else
				fix = false;
			if (fix) {
				if (debug) {
					LOGI("Looking at '%s'\n", temp->codePath.c_str());
					LOGI("Fixing permissions on '%s'\n", temp->pkgName.c_str());
					LOGI("Directory: '%s'\n", temp->appDir.c_str());
					LOGI("Original package owner: %d, group: %d\n", temp->uid, temp->gid);
				}
				if (pchown(temp->codePath, 1000, new_gid) != 0)
					return -1;
				if (pchmod(temp->codePath, perms) != 0)
					return -1;
			}
		} else {
			//Remove data directory since app isn't installed
			if (remove_data && TWFunc::Path_Exists(temp->dDir) && temp->appDir.size() >= 9 && temp->appDir.substr(0, 9) != "/mnt/asec") {
				if (debug)
					LOGI("Looking at '%s', removing data dir: '%s', appDir: '%s'", temp->codePath.c_str(), temp->dDir.c_str(), temp->appDir.c_str());
				if (removeDir(temp->dDir) != 0) {
					LOGI("Unable to removeDir '%s'\n", temp->dDir.c_str());
					return -1;
				}
			}
		}
		temp = temp->next;
	}
	return 0;
}

int fixPermissions::fixAllFiles(string directory, int gid, int uid, string file_perms) {
	vector <string> files;
	string file;

	files = listAllFiles(directory);
	for (unsigned i = 0; i < files.size(); ++i) {
		file = directory + "/";
		file.append(files.at(i));
		if (debug)
			LOGI("Looking at file '%s'\n", file.c_str());
		if (pchmod(file, file_perms) != 0)
			return -1;
		if (pchown(file, uid, gid) != 0)
			return -1;
	}
	return 0;
}

int fixPermissions::fixDataData() {
	string directory;

	temp = head;
	while (temp != NULL) {
		if (TWFunc::Path_Exists(temp->dDir)) {
			vector <string> dataDataDirs = listAllDirectories(temp->dDir);
			for (unsigned n = 0; n < dataDataDirs.size(); ++n) {
				directory = temp->dDir + "/";
				directory.append(dataDataDirs.at(n));
				if (debug)
					LOGI("Looking at data directory: '%s'\n", directory.c_str());
				if (dataDataDirs.at(n) == ".") {
					if (pchmod(temp->dDir, "0755") != 0)
						return -1;
					if (pchown(directory.c_str(), temp->uid, temp->gid) != 0)
						return -1;
					if (fixAllFiles(directory, temp->uid, temp->gid, "0755") != 0)
						return -1;
				}
				else if (dataDataDirs.at(n) == "..") {
					if (debug)
						LOGI("Skipping ..\n");
					continue;
				}
				else if (dataDataDirs.at(n) == "lib") {
					if (pchmod(directory.c_str(), "0755") != 0)
						return -1;
					if (pchown(directory.c_str(), 1000, 1000) != 0)
						return -1;
					if (fixAllFiles(directory, temp->uid, temp->gid, "0755") != 0)
						return -1;
				}
				else if (dataDataDirs.at(n) == "shared_prefs") {
					if (pchmod(directory.c_str(), "0771") != 0)
						return -1;
					if (pchown(directory.c_str(), temp->uid, temp->gid) != 0)
						return -1;
					if (fixAllFiles(directory, temp->uid, temp->gid, "0660") != 0)
						return -1;
				}
				else if (dataDataDirs.at(n) == "databases") {
					if (pchmod(directory.c_str(), "0771") != 0)
						return -1;
					if (pchown(directory.c_str(), temp->uid, temp->gid) != 0)
						return -1;
					if (fixAllFiles(directory, temp->uid, temp->gid, "0660") != 0)
						return -1;
				}
				else if (dataDataDirs.at(n) == "cache") {
					if (pchmod(directory.c_str(), "0771") != 0)
						return -1;
					if (pchown(directory.c_str(), temp->uid, temp->gid) != 0)
						return -1;
					if (fixAllFiles(directory, temp->uid, temp->gid, "0600") != 0)
						return -1;
				}
				else {
					if (pchmod(directory.c_str(), "0771") != 0)
						return -1;
					if (pchown(directory.c_str(), temp->uid, temp->gid) != 0)
						return -1;
					if (fixAllFiles(directory, temp->uid, temp->gid, "0755") != 0)
						return -1;
				}
			}
		}
		temp = temp->next;
	}
	return 0;
}

vector <string> fixPermissions::listAllDirectories(string path) {
	DIR *dir = opendir(path.c_str());
	struct dirent *entry = readdir(dir);
	vector <string> dirs;

	if (dir == NULL) {
		LOGE("Error opening '%s'\n", path.c_str());
		return dirs;
	}

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
	struct dirent *entry = readdir(dir);
	vector <string> files;

	if (dir == NULL) {
		LOGE("Error opening '%s'\n", path.c_str());
		return files;
	}

	while (entry != NULL) {
		if (entry->d_type == DT_REG)
			files.push_back(entry->d_name);
		entry = readdir(dir);
	}
	closedir(dir);
	return files;
}

int fixPermissions::getPackages() {
	int len = 0;
	bool skiploop = false;
	vector <string> skip;
	string dataDir;
	string name;
	head = NULL;

	skip.push_back("/system/framework/framework-res.apk");
	skip.push_back("/system/framework/com.htc.resources.apk");

	ifstream xmlFile(packageFile.c_str());
	xmlFile.seekg(0, ios::end);
	len = (int) xmlFile.tellg();
	xmlFile.seekg(0, ios::beg);
	char xmlBuf[len + 1];
	xmlFile.read(&xmlBuf[0], len);
	xmlBuf[len] = '\0';
	xml_document<> pkgDoc;
	pkgDoc.parse<parse_full>(&xmlBuf[0]);

	xml_node<> * pkgNode = pkgDoc.first_node("packages");
	xml_node <> * next = pkgNode->first_node("package");

	if (next == NULL) {
		LOGE("No packages found to fix.\n");
		return -1;
	}

	//Get packages
	while (next->first_attribute("name") != NULL) {
		package* temp = new package;
		for (unsigned n = 0; n < skip.size(); ++n) {
			if (skip.at(n).compare(next->first_attribute("codePath")->value()) == 0) {
				skiploop = true;
				break;
			}
		}

		if (skiploop == true) {
			if (debug)
				LOGI("Skipping package %s\n", next->first_attribute("codePath")->value());
			free(temp);
			next = next->next_sibling();
			skiploop = false;
			continue;
		}
		name.append((next->first_attribute("name")->value()));
		temp->pkgName = next->first_attribute("name")->value();
		if (debug)
			LOGI("Loading pkg: %s\n", next->first_attribute("name")->value());
		temp->codePath = next->first_attribute("codePath")->value();
		temp->app = basename(next->first_attribute("codePath")->value());
		temp->appDir = dirname(next->first_attribute("codePath")->value());
		dataDir.append("/data/data/");
		dataDir.append(name);
		temp->dDir = dataDir;
		if ( next->first_attribute("sharedUserId") != NULL) {
			temp->uid = atoi(next->first_attribute("sharedUserId")->value());
			temp->gid = atoi(next->first_attribute("sharedUserId")->value());
		}
		else {
			temp->uid = atoi(next->first_attribute("userId")->value());
			temp->gid = atoi(next->first_attribute("userId")->value());
		}
		next = next->next_sibling();
		name.clear();
		dataDir.clear();
		temp->next = head;
		head = temp;
	}
	//Get updated packages	
	next = pkgNode->first_node("updated-package");
	if (next != NULL) {
		while (next->first_attribute("name") != NULL) {
			package* temp = new package;
			for (unsigned n = 0; n < skip.size(); ++n) {
				if (skip.at(n).compare(next->first_attribute("codePath")->value()) == 0) {
					skiploop = true;
					break;
				}
			}

			if (skiploop == true) {
				if (debug)
					LOGI("Skipping package %s\n", next->first_attribute("codePath")->value());
				free(temp);
				next = next->next_sibling();
				skiploop = false;
				continue;
			}
			name.append((next->first_attribute("name")->value()));
			temp->pkgName = next->first_attribute("name")->value();
			if (debug)
				LOGI("Loading pkg: %s\n", next->first_attribute("name")->value());
			temp->codePath = next->first_attribute("codePath")->value();
			temp->app = basename(next->first_attribute("codePath")->value());
			temp->appDir = dirname(next->first_attribute("codePath")->value());

			dataDir.append("/data/data/");
			dataDir.append(name);
			temp->dDir = dataDir;
			if ( next->first_attribute("sharedUserId") != NULL) {
				temp->uid = atoi(next->first_attribute("sharedUserId")->value());
				temp->gid = atoi(next->first_attribute("sharedUserId")->value());
			}
			else {
				temp->uid = atoi(next->first_attribute("userId")->value());
				temp->gid = atoi(next->first_attribute("userId")->value());
			}
			next = next->next_sibling();
			name.clear();
			dataDir.clear();
			temp->next = head;
			head = temp;
		}
	}
	return 0;
}
