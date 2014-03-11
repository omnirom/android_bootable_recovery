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

using namespace std;

twrpDU::twrpDU() {
		add_relative_dir(".");
		add_relative_dir("..");
		add_relative_dir("lost+found");
		add_absolute_dir("/data/data/com.google.android.music/files");
		parent = "";
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
	unsigned long long dusize = 0;
	unsigned long long dutemp = 0;

	parent = Path.substr(0, Path.find_last_of('/'));

	d = opendir(Path.c_str());
	if (d == NULL) {
		LOGERR("error opening '%s'\n", Path.c_str());
		LOGERR("error: %s\n", strerror(errno));
		return 0;
	}

	while ((de = readdir(d)) != NULL)
	{
		if (de->d_type == DT_DIR && !check_skip_dirs(Path, de->d_name)) {
			dutemp = Get_Folder_Size((Path + "/" + de->d_name));
			dusize += dutemp;
			dutemp = 0;
		}
		else if (de->d_type == DT_REG) {
			stat((Path + "/" + de->d_name).c_str(), &st);
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
	string normalized = TWFunc::Remove_Trailing_Slashes(path);
	return std::find(absolutedir.begin(), absolutedir.end(), normalized) != absolutedir.end();
}

bool twrpDU::check_skip_dirs(const string& parent, const string& dir) {
	return check_relative_skip_dirs(dir) || check_absolute_skip_dirs(parent + "/" + dir);
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
