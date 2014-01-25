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

#ifndef TWRPDU_HPP
#define TWRPDU_HPP

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <fstream>
#include <string>
#include <vector>
#include "twcommon.h"

using namespace std;

class twrpDU {

public:
	twrpDU();
	uint64_t Get_Folder_Size(const string& Path); // Gets the folder's size using stat
	void add_absolute_dir(string Path);
	void add_relative_dir(string Path);
	bool check_skip_dirs(string& dir);
	vector<string> get_absolute_dirs(void);
	void clear_relative_dir(string dir);
private:
	vector<string> absolutedir;
	vector<string> relativedir;
	string parent;
};

extern twrpDU du;
#endif
