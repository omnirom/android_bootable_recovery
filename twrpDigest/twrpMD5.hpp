/*
        Copyright 2012 to 2017 TeamWin
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

#include <string>
#include "twrpDigest.hpp"

extern "C" {
	#include "digest/md5/md5.h"
}

class twrpMD5: public twrpDigest {
public:
	twrpMD5();                                                            // Stream initializer
	void init();                                                          // Initialize MD5 structures
	void update(const unsigned char* stream, size_t len);                 // Update MD5 stream with data. Return false if failure to update MD5.
	std::string return_digest_string();                                   // Return MD5 digest as string to callee
	void finalize();                                                      // Finalize and compute MD5

private:
	struct MD5Context md5c;                                               // MD5 control structure
	unsigned char md5sum[MD5LENGTH];                                      // Stores the md5sum computation
};

#endif //__TWRPMD5_H
