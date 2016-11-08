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
}

void twrpDU::add_relative_dir(const string& dir, const bool Skip_Recursive) {
	skipitem_struct item;
	item.Path = dir;
	item.Skip_Recursive = Skip_Recursive;
	relativedir.push_back(item);
}

void twrpDU::clear_relative_dir(string dir) {
	vector<skipitem_struct>::iterator iter = relativedir.begin();
	while (iter != relativedir.end()) {
		if ((*iter).Path == dir)
			iter = relativedir.erase(iter);
		else
			iter++;
	}
}

void twrpDU::add_absolute_dir(const string& dir, const bool Skip_Recursive) {
	skipitem_struct item;
	item.Path = TWFunc::Remove_Trailing_Slashes(dir);
	item.Skip_Recursive = Skip_Recursive;
	absolutedir.push_back(item);
}

/*vector<skipitem_struct> twrpDU::get_absolute_dirs(void) {
	return absolutedir;
}*/

uint64_t twrpDU::Get_Folder_Size(const string& Path) {
	DIR* d;
	struct dirent* de;
	struct stat st;
	uint64_t dusize = 0;
	string FullPath;
	bool Skip, Skip_Recursive;

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
			LOGINFO("Real error: Unable to stat '%s': %s\n", FullPath.c_str(), strerror(errno));
			continue;
		}
		Skip = check_skip_dirs(FullPath, Skip_Recursive);
		if ((st.st_mode & S_IFDIR) && !Skip_Recursive && de->d_type != DT_SOCK) {
			dusize += Get_Folder_Size(FullPath);
		} else if (!Skip && st.st_mode & S_IFREG) {
			dusize += (uint64_t)(st.st_size);
		}
	}
	closedir(d);
	return dusize;
}

bool twrpDU::check_relative_skip_dirs(const string& dir, bool& Skip_Recursive) {
	vector<skipitem_struct>::iterator iter = relativedir.begin();
	while (iter != relativedir.end()) {
		if ((*iter).Path == dir) {
			Skip_Recursive = (*iter).Skip_Recursive;
			return true;
		}
		iter++;
	}
	return false; //std::find(relativedir.begin(), relativedir.end(), dir) != relativedir.end();
}

bool twrpDU::check_absolute_skip_dirs(const string& dir, bool& Skip_Recursive) {
	vector<skipitem_struct>::iterator iter = absolutedir.begin();
	while (iter != absolutedir.end()) {
		if ((*iter).Path == dir) {
			Skip_Recursive = (*iter).Skip_Recursive;
			return true;
		}
		iter++;
	}
	return false; //return std::find(absolutedir.begin(), absolutedir.end(), path) != absolutedir.end();
}

bool twrpDU::check_skip_dirs(const string& path, bool& Skip_Recursive) {
	Skip_Recursive = false;
	string normalized = TWFunc::Remove_Trailing_Slashes(path);
	size_t slashIdx = normalized.find_last_of('/');
	if(slashIdx != std::string::npos && slashIdx+1 < normalized.size()) {
		if(check_relative_skip_dirs(normalized.substr(slashIdx+1), Skip_Recursive))
			return true;
	}
	return check_absolute_skip_dirs(normalized, Skip_Recursive);
}
