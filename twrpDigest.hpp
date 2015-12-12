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

extern "C" {
	#include "digest/md5.h"
}

using namespace std;

class twrpDigest
{
public:
	void setfn(const string& fn);
	int computeMD5(void);
	int verify_md5digest(void);
	int write_md5digest(void);
	int updateMD5stream(unsigned char* stream, int len);
	void finalizeMD5stream(void);
	string createMD5string(void);
	void initMD5(void);

private:
	int read_md5digest(void);
	struct MD5Context md5c;
	string md5fn;
	string line;
	unsigned char md5sum[MD5LENGTH];
	string md5string;
};
