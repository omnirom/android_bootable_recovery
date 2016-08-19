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
#include "twrpSHA2.hpp"

void twrpSHA2::initSHA2(void) {
	SHA256 sha2_hash;
	SHA256 sha2_stream;
}


std::string twrpSHA2::createDigestString() {
	return sha2_stream.getHash();
}

int twrpSHA2::computeDigest(void) {
	unsigned char buf[1024];
	uint64_t bytes;

	initSHA2();
        int fd = open(digestfn.c_str(), O_RDONLY);
        if (fd < 0) {
                printf("Unable to open %s for reading.\n", digestfn.c_str());
		close(fd);
                return -1;
        }

	while (( bytes = read(fd, &buf, sizeof(buf))) == sizeof(buf)) {
		sha2_stream.add(buf, bytes);
	}

	close(fd);
	return 0;
}

int twrpSHA2::write_digest(void) {
	sha2file = digestfn + ".sha2";

	sha2string = createDigestString();
	sha2string += "  ";
	sha2string = sha2string + basename((char*) digestfn.c_str());
	twrpDigest::write_file(sha2file, sha2string);
	printf("SHA2 for %s: %s\n", digestfn.c_str(), sha2string.c_str());
	return 0;
}

int twrpSHA2::read_digest(void) {
	size_t i = 0;
	bool foundFile = false;
	std::string file = "";
	std::string line;
	std::vector<string> sha2ext;
	sha2ext.push_back(".sha");
	sha2ext.push_back(".sha2");

	while (i < sha2ext.size()) {
		file = digestfn + sha2ext[i];
		if (Path_Exists(file)) {
			foundFile = true;
			break;
		}
		i++;
	}

	if (!foundFile) {
		printf("Skipping SHA2 check: no SHA2 file found");
		return -1;
	} else if (read_file(file, sha2verify) != 0) {
		printf("E: Skipping SHA2 check: SHA2 file unreadable %s\n", strerror(errno));
		return 1;
	}

	return 0;
}

int twrpSHA2::updateStream(unsigned char* stream, int len) {
	return 0;
}

void twrpSHA2::finalizeStream(void) {
}

/* verify_digest return codes:
	-2: sha2 did not match
	-1: no sha2 file found
	 0: sha2 matches
	 1: sha2 file unreadable
*/

int twrpSHA2::verify_digest(void) {
	std::string buf, sha2str;
	char hex[3];
	int i, ret;

	ret = read_digest();
	if (ret != 0)
		return ret;

	sha2verify += "  ";
	sha2verify = sha2verify + basename((char*) digestfn.c_str());
	ret = computeDigest();
	if (ret == -1)
		return ret;
	sha2string = createDigestString();
	sha2string += "  ";
	sha2string = sha2string + basename((char*) digestfn.c_str());

	printf("sha2string: %s\n", sha2string.c_str());
	printf("sha2verify: %s\n", sha2verify.c_str());
	if (sha2string != sha2verify) {
		printf("E: SHA2 does not match\n");
		return -2;
	}

	printf("SHA2 matched\n");
	return 0;
}
