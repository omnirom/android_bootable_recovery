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
	#include "digest/md5/md5.h"
}

using namespace std;

class twrpMD5: public twrpDigest {
public:
	~twrpMD5() {};
	int computeDigest(void);
	int verify_digest(void);
	int write_digest(void);
	int read_digest(void);
	int updateStream(unsigned char* stream, int len);
	void finalizeStream(void);
	void initMD5(void);
	string createDigestString(void);

private:
	struct MD5Context md5c;
	string md5string;
	unsigned char md5sum[MD5LENGTH];
};
