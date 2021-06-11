/*
        Copyright 2013 to 2017 TeamWin
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

#ifndef __TWRP_DIGEST_DRIVER
#define __TWRP_DIGEST_DRIVER
#include <string>
#include "twrpDigest/twrpDigest.hpp"

class twrpDigestDriver {
public:

	static bool Check_File_Digest(const string& Filename);		//Check the digest of a TWRP partition backup
	static bool Check_Digest(string Full_Filename);				//Check to make sure the digest is correct
	static bool Write_Digest(string Full_Filename);				//Write the digest to a file
	static bool Make_Digest(string Full_Filename);				//Create the digest for a partition backup
	static bool stream_file_to_digest(string filename, twrpDigest* digest); //Stream the file to twrpDigest
};
#endif //__TWRP_DIGEST_DRIVER
