/*
		Copyright 2014 TeamWin
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
#include <vector>
#include <dirent.h>
#include <stdlib.h>
#include "find_file.hpp"
#include "twrp-functions.hpp"
#include "twcommon.h"

using namespace std;

string Find_File::Find(const string& file_name, const string& start_path) {
	return Find_File().Find_Internal(file_name, start_path);
}

Find_File::Find_File() {
}

string Find_File::Find_Internal(const string& filename, const string& starting_path) {
	DIR *d;
	string new_path, return_path;
	vector<string> dirs;
	vector<string> symlinks;
	unsigned index;

	// Check to see if we have already searched this directory to prevent infinite loops
	if (std::find(searched_dirs.begin(), searched_dirs.end(), starting_path) != searched_dirs.end()) {
		return "";
	}
	searched_dirs.push_back(starting_path);

	d = opendir(starting_path.c_str());
	if (d == NULL) {
		LOGINFO("Find_File: Error opening '%s'\n", starting_path.c_str());
		return "";
	}

	struct dirent *p;
	while ((p = readdir(d))) {
		if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, ".."))
			continue;
		new_path = starting_path + "/";
		new_path.append(p->d_name);
		if (p->d_type == DT_DIR) {
			// Add dir to search list for later
			dirs.push_back(new_path);
		} else if (p->d_type == DT_LNK) {
			// Add symlink to search list for later
			symlinks.push_back(new_path);
		} else if (p->d_type == DT_REG && filename == p->d_name) {
			// We found a match!
			closedir(d);
			return new_path;
		}
	}
	closedir(d);

	// Scan real directories first if no match found in this path
	for (index = 0; index < dirs.size(); index++) {
		return_path = Find_Internal(filename, dirs.at(index));
		if (!return_path.empty()) return return_path;
	}
	// Scan symlinks after scanning real directories
	for (index = 0; index < symlinks.size(); index++) {
		char buf[PATH_MAX];
		// Resolve symlink to a real path
		char* ret = realpath(symlinks.at(index).c_str(), buf);
		if (ret) {
			return_path = Find_Internal(filename, buf);
			if (!return_path.empty()) return return_path;
		}
	}
	return "";
}
