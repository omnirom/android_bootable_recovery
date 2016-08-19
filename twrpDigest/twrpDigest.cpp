/*
	Copyright 2012 to 2016 bigbiff/Dees_Troy TeamWin
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

extern "C"
{
	#include "digest/md5/md5.h"
	#include "../libcrecovery/common.h"
}

#include <vector>
#include <string>
#include <sstream>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <string.h>
#include <libgen.h>
#include <errno.h>
#include <fcntl.h>
#include <fstream>
#include "twrpDigest.hpp"

using namespace std;

void twrpDigest::setfn(const string& fn) {
	digestfn = fn;
}

int twrpDigest::write_file(string fn, string& line) {
        FILE *file;
        file = fopen(fn.c_str(), "w");
        if (file != NULL) {
                fwrite(line.c_str(), line.size(), 1, file);
                fclose(file);
                return 0;
        }
        printf("Cannot find file %s\n", fn.c_str());
        return -1;
}

bool twrpDigest::Path_Exists(string Path) {
        struct stat st;
        if (stat(Path.c_str(), &st) != 0)
                return false;
        else
                return true;
}

int twrpDigest::read_file(string fn, string& results) {
        ifstream file;
        file.open(fn.c_str(), ios::in);

        if (file.is_open()) {
                file >> results;
                file.close();
                return 0;
        }

        printf("Cannot find file %s\n", fn.c_str());
        return -1;
}
