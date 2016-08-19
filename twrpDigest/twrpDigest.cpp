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

std::string twrpDigest::hexify(uint8_t* hash, size_t len) {
	char hex[3];
	std::string digest_string;

	for (size_t i = 0; i < len; ++i) {
		snprintf(hex, 3, "%02x", hash[i]);
		digest_string += hex;
	}
	return digest_string;
}

bool twrpDigest::read_file(std::string digest_filename) {
        char buf[1024];
        int bytes;
	int fd;

	fd = open(digest_filename.c_str(), O_RDONLY);
        if (fd < 0) {
                return false;
        }
        while ((bytes = read(fd, &buf, sizeof(buf))) != 0) {
                update((unsigned char*)buf, bytes);
        }
        close(fd);
	return true;
}

bool twrpDigest::verify_digest(std::string digest) {
        digestVerify = return_digest_string();
        if (digestVerify.compare(digest) != 0) {
                return false;
        }
        return true;
}
