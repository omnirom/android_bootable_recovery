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
#include <fcntl.h>
#include "twrpDigest.hpp"

std::string twrpDigest::hexify(uint8_t* hash, size_t len) {
	char hex[3];
	std::string digest_string;

	for (size_t i = 0; i < len; ++i) {
		snprintf(hex, 3, "%02x", hash[i]);
		digest_string += hex;
	}
	return digest_string;
}
