/*
		Copyright 2013 TeamWin
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

extern "C" {
	#include "libtar/libtar.h"
}
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#include <fnmatch.h>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include "twrpDU.hpp"
#include "twrp-functions.hpp"
#include "gui/gui.hpp"

using namespace std;

extern bool datamedia;

twrpDU::twrpDU() {
	add_relative_dir(".");
	add_relative_dir("..");
	add_relative_dir("lost+found");
	add_absolute_dir("/data/data/com.google.android.music/files");

	backup_excludes_loaded = false;
}

void twrpDU::add_relative_dir(const string& dir) {
	relativedir.push_back(dir);
}

void twrpDU::clear_relative_dir(string dir) {
	vector<string>::iterator iter = relativedir.begin();
	while (iter != relativedir.end()) {
		if (*iter == dir)
			iter = relativedir.erase(iter);
		else
			iter++;
	}
}

void twrpDU::add_absolute_dir(const string& dir) {
	absolutedir.push_back(TWFunc::Remove_Trailing_Slashes(dir));
}

vector<string> twrpDU::get_absolute_dirs(void) {
	return absolutedir;
}

uint64_t twrpDU::Get_Folder_Size(const string& Path) {
	DIR* d;
	struct dirent* de;
	struct stat st;
	uint64_t dusize = 0;
	string FullPath;

	d = opendir(Path.c_str());
	if (d == NULL) {
		gui_msg(Msg(msg::kError, "error_opening_strerr=Error opening: '{1}' ({2})")(Path)(strerror(errno)));
		return 0;
	}

	while ((de = readdir(d)) != NULL) {
		FullPath = Path + "/";
		FullPath += de->d_name;
		if (lstat(FullPath.c_str(), &st)) {
			gui_msg(Msg(msg::kError, "error_opening_strerr=Error opening: '{1}' ({2})")(FullPath)(strerror(errno)));
			LOGINFO("Real error: Unable to stat '%s'\n", FullPath.c_str());
			continue;
		}
		if ((st.st_mode & S_IFDIR) && !check_skip_dirs(FullPath) && de->d_type != DT_SOCK) {
			dusize += Get_Folder_Size(FullPath);
		} else if (st.st_mode & S_IFREG) {
			dusize += (uint64_t)(st.st_size);
		}
	}
	closedir(d);
	return dusize;
}

uint64_t twrpDU::Get_Folder_Backup_Size(const string& Path) {
	DIR* d;
	struct dirent* de;
	struct stat st;
	uint64_t dusize = 0;
	string FullPath;

	d = opendir(Path.c_str());
	if (d == NULL) {
		gui_msg(Msg(msg::kError, "error_opening_strerr=Error opening: '{1}' ({2})")(Path)(strerror(errno)));
		return 0;
	}

	while ((de = readdir(d)) != NULL) {
		FullPath = Path + "/";
		FullPath += de->d_name;
		if (lstat(FullPath.c_str(), &st)) {
			gui_msg(Msg(msg::kError, "error_opening_strerr=Error opening: '{1}' ({2})")(FullPath)(strerror(errno)));
			LOGINFO("Real error: Unable to stat '%s'\n", FullPath.c_str());
			continue;
		}
		if ((st.st_mode & S_IFDIR) && !check_skip_dirs(FullPath) && !check_skip_backup_dirs(FullPath + "/") && de->d_type != DT_SOCK) {
			dusize += Get_Folder_Backup_Size(FullPath);
		} else if (st.st_mode & S_IFREG && !check_skip_backup_files(FullPath)) {
			dusize += (uint64_t)(st.st_size);
		}
	}
	closedir(d);
	return dusize;
}

bool twrpDU::check_relative_skip_dirs(const string& dir) {
	return std::find(relativedir.begin(), relativedir.end(), dir) != relativedir.end();
}

bool twrpDU::check_absolute_skip_dirs(const string& path) {
	return std::find(absolutedir.begin(), absolutedir.end(), path) != absolutedir.end();
}

bool twrpDU::check_skip_dirs(const string& path) {
	string normalized = TWFunc::Remove_Trailing_Slashes(path);
	size_t slashIdx = normalized.find_last_of('/');
	if(slashIdx != std::string::npos && slashIdx+1 < normalized.size()) {
		if(check_relative_skip_dirs(normalized.substr(slashIdx+1)))
			return true;
	}
	return check_absolute_skip_dirs(normalized);
}


// text file to be placed in device files, under recovery/root
// eg 'device/htc/pme/recovery/root/backup_excludes'

#define BACKUP_EXCLUDE_PATTERN_FILE "backup_excludes"


// file format, in ascending order:
// 1) begins with '/' -> absolute dir(s) or file(s)
// 2) ends with '/' -> dir otherwise file(s)
// 3) wildcard pattern matching as per fnmatch rules
//    note i'm not using FNM_PATHNAME flag so wildcards will be greedy and match /
// comments can be included in the file and should be preceded by #


//  Examples: for Files
//  -------------------
//  dummyfile            relative: any file named 'dummyfile'
//  *dummy*file*         relative: any file containing 'dummy' and 'file'
//  .*                   relative: any "hidden" files, ie files starting with .
//  /data/hrdump*        absolute: file beginning with 'hrdump' in /data/ (no subdirs)
//  /data/*/*hrdump*     absolute: file containing 'hrdump' in /data/subs/ and any subdirs thereof

//  Examples: for Dirs
//  -------------------
//  dummydir/            relative: any dir named 'dummydir'
//  cache/               relative: any dir named 'cache' (NOTE: this will also match the entire cache partition)
//  /data/*/cache/       absolute: any dir named 'cache' in /data/subs/ and any subdirs thereof


//  HTC 10 example file:
/*
# HTC 10 TWRP Backup Exclusion File

# 4GB ramdump created by s-off devices
/data/hrdmp.rdmp

# restoring locksettings causes PIN/pattern problems
/data/system/locksettings.*
*/

int twrpDU::LoadBackupExcludes(void) {
	if (backup_excludes_loaded)
		return 0;

	backup_excludes_loaded = true;

	FILE* in = fopen(BACKUP_EXCLUDE_PATTERN_FILE, "r");
	if (!in) {
		LOGINFO("twrpDU: file '%s' not found.\n", BACKUP_EXCLUDE_PATTERN_FILE);
		return -1;
	} else {
		LOGINFO("twrpDU: loading backup exclusions from '%s'.\n", BACKUP_EXCLUDE_PATTERN_FILE);
	}

	char strLine[255];
	while (!feof(in)) {
		if (fgets(strLine, sizeof(strLine), in) != NULL) {
			char *beg = strLine;
			char *end;

			while(isspace(*beg)) beg++;

			if(*beg == '\x00' || *beg == '#')  //empty or comment line, so skip
				continue;

			end = beg + strlen(beg) - 1;
			while(end > beg && isspace(*end)) end--;

			*(end+1) = '\x00';

			LOGINFO("twrpDU: adding exclusion '%s'\n", beg);
			backup_exclude.push_back(beg);
		}
	}
	fclose(in);
	return 0;
}

bool twrpDU::check_skip_backup_dirs(const string& path) {
	if (!backup_excludes_loaded)
		LoadBackupExcludes();

	if (path.empty() || path[path.length()-1] != '/')
		return false; // path is a file

	std::vector<std::string>::iterator iter;
	int res;
	for (iter = backup_exclude.begin(); iter != backup_exclude.end(); iter++) {
		if ((*iter)[(*iter).length()-1] == '/') {
			// exclude is a dir, compare it
			if ((*iter)[0] == '/') {
				// absolute dir
				res = fnmatch((*iter).c_str(), path.c_str(), FNM_PERIOD);
				if (res == 0)
					return true;
			} else {
				// relative dir, strip pre '/'
				string dir_name;
				size_t pos = path.find_last_of('/');
				if (pos != string::npos)
					dir_name = path.substr(pos+1);
				else
					dir_name = path;

				res = fnmatch((*iter).c_str(), dir_name.c_str(), FNM_PERIOD);
				if (res == 0)
					return true;
			}
		}
	}
	return false;
}

bool twrpDU::check_skip_backup_files(const string& path) {
	if (!backup_excludes_loaded)
		LoadBackupExcludes();

	if (path.empty() || path[path.length()-1] == '/')
		return false; // path is a dir

	vector<std::string>::iterator iter;
	for (iter = backup_exclude.begin(); iter != backup_exclude.end(); iter++) {
		if ((*iter)[(*iter).length()-1] != '/') {
			// exclude pattern is a file, compare it
			int res;
			if ((*iter)[0] == '/') {
				// absolute file
				res = fnmatch((*iter).c_str(), path.c_str(), FNM_PERIOD);
				if (res == 0)
					return true;
			} else {
				// relative file, strip pre '/'
				string file_name;
				size_t pos = path.find_last_of('/');
				if (pos != string::npos)
					file_name = path.substr(pos+1);
				else
					file_name = path;

				res = fnmatch((*iter).c_str(), file_name.c_str(), FNM_PERIOD);
				if (res == 0)
					return true;
			}
		}
	}
	return false;
}

// in order for a path to be considered a dir, please add a trailing '/', otherwise it will be considered a file
bool twrpDU::check_skip_backup(const string& path) {
	return check_skip_backup_dirs(path) || check_skip_backup_files(path);
}
