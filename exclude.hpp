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

#ifndef TWEXCLUDE_HPP
#define TWEXCLUDE_HPP

#include <string>
#include <vector>

using namespace std;

class TWExclude {

public:
	TWExclude();
	uint64_t Get_Folder_Size(const string& Path); // Gets the folder's size using stat
	void add_absolute_dir(const string& Path);
	void add_relative_dir(const string& Path);
	bool check_relative_skip_dirs(const string& dir);
	bool check_absolute_skip_dirs(const string& path);
	bool check_skip_dirs(const string& path);
	void clear_relative_dir(string dir);
private:
	vector<string> absolutedir;
	vector<string> relativedir;
};

#endif
