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
#include "twrpDU.hpp"

using namespace std;

twrpDU::twrpDU() {
		add_relative_dir(".");
		add_relative_dir("..");
		add_relative_dir("lost_found");
		add_absolute_dir("/data/data/com.google.android.music/files");
		parent = "";
}

void twrpDU::add_relative_dir(string dir) {
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

void twrpDU::add_absolute_dir(string dir) {
	absolutedir.push_back(dir);
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
		bool skip_dir = false;
		if (de->d_type == DT_DIR) {
			string dir = de->d_name;
			skip_dir = check_skip_dirs(dir);
		}
		if (de->d_type == DT_DIR && !skip_dir) {
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

bool twrpDU::check_skip_dirs(string& dir) {
	bool result = false;
	for (int i = 0; i < relativedir.size(); ++i) {
		if (dir == relativedir.at(i)) {
			result = true;
			break;
		}
	}
	for (int i = 0; i < absolutedir.size(); ++i) {
		if (dir == absolutedir.at(i)) {
			result = true;
			break;
		}
	}
	return result;
}
