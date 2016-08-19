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
#include <sys/mman.h>
#include "twrpDigest.hpp"
#include "twrpMD5.hpp"

twrpMD5::twrpMD5(std::string digest_filename) {
	init();
}

void twrpMD5::init(void) {
	MD5Init(&md5c);
	md5string = "";
}

bool twrpMD5::update(const unsigned char* stream, size_t len) {
	if (digest_filename.empty()) {
		MD5Update(&md5c, stream, len);
	}
	else {
		return false;
	}
	return true;
}

void twrpMD5::finalize() {
	MD5Final(md5sum, &md5c);
}

std::string twrpMD5::return_digest_string(void) {
	std::string line;
	FILE *file;
	int len;
	unsigned char buf[1024];
	file = fopen(digest_filename.c_str(), "rb");
	if (file == NULL)
		return "";
	twrpDigest::read_file(digest_filename);
	fclose(file);
	finalize();

	md5string = hexify(md5sum, sizeof(md5sum));

	if (!digest_filename.empty()) {
		md5string += "  ";
		md5string += basename((char*) digest_filename.c_str());
		md5string +=  + "\n";
	}
	return md5string;
}

bool twrpMD5::verify_digest(std::string digest) {
	md5verify = return_digest_string();
	if (md5verify.compare(digest) != 0) {
		return false;
	}
	return true;
}
