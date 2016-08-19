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

void twrpMD5::initMD5(void) {
	MD5Init(&md5c);
	md5string = "";
}

int twrpMD5::updateStream(unsigned char* stream, int len) {
	if (digestfn.empty()) {
		MD5Update(&md5c, stream, len);
	}
	else {
		return -1;
	}
	return 0;
}

void twrpMD5::finalizeStream() {
	MD5Final(md5sum, &md5c);
}

string twrpMD5::createDigestString() {
	int i;
	char hex[3];

	for (i = 0; i < 16; ++i) {
		snprintf(hex, 3, "%02x", md5sum[i]);
		md5string += hex;
	}
	if (!digestfn.empty()) {
		md5string += "  ";
		md5string += basename((char*) digestfn.c_str());
		md5string +=  + "\n";
	}
	return md5string;
}

int twrpMD5::computeDigest(void) {
	string line;
	FILE *file;
	int len;
	unsigned char buf[1024];
	initMD5();
	file = fopen(digestfn.c_str(), "rb");
	if (file == NULL)
		return -1;
	while ((len = fread(buf, 1, sizeof(buf), file)) > 0) {
		MD5Update(&md5c, buf, len);
	}
	fclose(file);
	MD5Final(md5sum, &md5c);
	return 0;
}

int twrpMD5::write_digest(void) {
	string md5file, md5str;
	md5file = digestfn + ".md5";

	md5str = createDigestString();
	write_file(md5file, md5str);
	printf("MD5 for %s: %s\n", digestfn.c_str(), md5str.c_str());
	return 0;
}

int twrpMD5::read_digest(void) {
	size_t i = 0;
	bool foundMd5File = false;
	string md5file = "";
	string line;
	vector<string> md5ext;
	md5ext.push_back(".md5");
	md5ext.push_back(".md5sum");

	while (i < md5ext.size()) {
		md5file = digestfn + md5ext[i];
		if (Path_Exists(md5file)) {
			foundMd5File = true;
			break;
		}
		i++;
	}

	if (!foundMd5File) {
		printf("Skipping MD5 check: no MD5 file found");
		return -1;
	} else if (read_file(md5file, line) != 0) {
		printf("E: Skipping MD5 check: MD5 file unreadable %s\n", strerror(errno));
		return 1;
	}

	return 0;
}

/* verify_digest return codes:
	-2: md5 did not match
	-1: no md5 file found
	 0: md5 matches
	 1: md5 file unreadable
*/

int twrpMD5::verify_digest(void) {
	string buf;
	char hex[3];
	int i, ret;
	string md5str, line;

	ret = read_digest();
	if (ret != 0)
		return ret;
	stringstream ss(line);
	vector<string> tokens;
	while (ss >> buf)
		tokens.push_back(buf);
	computeDigest();
	for (i = 0; i < 16; ++i) {
		snprintf(hex, 3, "%02x", md5sum[i]);
		md5str += hex;
	}
	if (tokens.at(0) != md5str) {
		printf("E: MD5 does not match");
		return -2;
	}

	printf("MD5 matched");
	return 0;
}
