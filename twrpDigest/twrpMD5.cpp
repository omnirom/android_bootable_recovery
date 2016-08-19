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

using namespace std;

void twrpMD5::init_digest(void) {
	MD5Init(&md5c);
	md5string = "";
}

bool twrpMD5::update_stream(unsigned char* stream, int len) {
	if (digest_filename.empty()) {
		MD5Update(&md5c, stream, len);
	}
	else {
		return false;
	}
	return true;
}

void twrpMD5::finalize_stream() {
	MD5Final(md5sum, &md5c);
}

std::string twrpMD5::create_digest_string() {
	int i;
	char hex[3];

	md5string = hexify(md5sum, sizeof(md5sum));
	if (!digest_filename.empty()) {
		md5string += "  ";
		md5string += basename((char*) digest_filename.c_str());
		md5string +=  + "\n";
	}
	return md5string;
}

std::string twrpMD5::return_digest_string(void) {
	string line;
	FILE *file;
	int len;
	unsigned char buf[1024];
	init_digest();
	file = fopen(digest_filename.c_str(), "rb");
	if (file == NULL)
		return "";
	while ((len = fread(buf, 1, sizeof(buf), file)) > 0) {
		MD5Update(&md5c, buf, len);
	}
	fclose(file);
	MD5Final(md5sum, &md5c);
	return create_digest_string();
}

bool twrpMD5::verify_digest(std::string digest) {
	md5verify = create_digest_string();
	if (md5verify != digest) {
		return false;
	}
	return true;
}
