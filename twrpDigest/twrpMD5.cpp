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

#include <vector>
#include <string>
//#include <sstream>
#include "twrpDigest.hpp"
#include "twrpMD5.hpp"

twrpMD5::twrpMD5() {
	init();
}

void twrpMD5::init() {
	MD5Init(&md5c);
}

void twrpMD5::update(const unsigned char* stream, size_t len) {
	MD5Update(&md5c, stream, len);
}

void twrpMD5::finalize() {
	MD5Final(md5sum, &md5c);
}

std::string twrpMD5::return_digest_string() {
	std::string md5string;
	finalize();

	md5string = hexify(md5sum, sizeof(md5sum));
	return md5string;
}
