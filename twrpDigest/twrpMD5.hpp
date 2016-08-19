/*
        Copyright 2012 to 2016 TeamWin
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

#ifndef __TWRPMD5_H
#define __TWRPMD5_H

extern "C" {
	#include "digest/md5/md5.h"
}

class twrpMD5: virtual public twrpDigest {
public:
	~twrpMD5() {};
	void init_digest(void);
	bool update_stream(unsigned char* stream, int len);
	void finalize_stream(void);
	std::string create_digest_string(void);
	std::string return_digest_string(void);
	bool verify_digest(std::string digest);

private:
	struct MD5Context md5c;
	std::string md5string;
	std::string md5verify;
	unsigned char md5sum[MD5LENGTH];
};

#endif //__TWRPMD5_H
