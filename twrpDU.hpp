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
	void add_absolute_dir(const string& Path, const bool Skip_Recursive = true);
	void add_relative_dir(const string& Path, const bool Skip_Recursive = true);
	bool check_relative_skip_dirs(const string& dir, bool& Skip_Recursive);
	bool check_absolute_skip_dirs(const string& dir, bool& Skip_Recursive);
	bool check_skip_dirs(const string& path, bool& Skip_Recursive);
	//vector<skipitem_struct> get_absolute_dirs(void);
	void clear_relative_dir(string dir);
private:
	struct skipitem_struct {
		string Path;
		bool Skip_Recursive;
	};
	vector<skipitem_struct> absolutedir;
	vector<skipitem_struct> relativedir;
};

extern twrpDU du;
#endif
