/*
		Copyright 2013 to 2016 TeamWin
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
#include <dirent.h>
#include <errno.h>
#include <string>
#include <vector>
#include "exclude.hpp"
#include "twrp-functions.hpp"
#include "gui/gui.hpp"
#include "twcommon.h"

using namespace std;

extern bool datamedia;

TWExclude::TWExclude() {
	add_relative_dir(".");
	add_relative_dir("..");
	add_relative_dir("lost+found");
}

void TWExclude::add_relative_dir(const string& dir) {
	relativedir.push_back(dir);
}

void TWExclude::clear_relative_dir(string dir) {
	vector<string>::iterator iter = relativedir.begin();
	while (iter != relativedir.end()) {
		if (*iter == dir)
			iter = relativedir.erase(iter);
		else
			iter++;
	}
}

void TWExclude::add_absolute_dir(const string& dir) {
	absolutedir.push_back(TWFunc::Remove_Trailing_Slashes(dir));
}

uint64_t TWExclude::Get_Folder_Size(const string& Path) {
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
		} else if (st.st_mode & S_IFREG || st.st_mode & S_IFLNK) {
			dusize += (uint64_t)(st.st_size);
		}
	}
	closedir(d);
	return dusize;
}

bool TWExclude::check_relative_skip_dirs(const string& dir) {
	return std::find(relativedir.begin(), relativedir.end(), dir) != relativedir.end();
}

bool TWExclude::check_absolute_skip_dirs(const string& path) {
	return std::find(absolutedir.begin(), absolutedir.end(), path) != absolutedir.end();
}

bool TWExclude::check_skip_dirs(const string& path) {
	string normalized = TWFunc::Remove_Trailing_Slashes(path);
	size_t slashIdx = normalized.find_last_of('/');
	if (slashIdx != std::string::npos && slashIdx+1 < normalized.size()) {
		if (check_relative_skip_dirs(normalized.substr(slashIdx+1)))
			return true;
	}
	return check_absolute_skip_dirs(normalized);
}
