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
#include <openssl/sha.h>
#include <fstream>
#include <sys/mman.h>
#include "twrpDigest.hpp"
#include "twrpSHA2.hpp"

void twrpSHA2::init_digest(void) {
	SHA256_CTX sha256_ctx;
	SHA256_Init(&sha256_ctx);
}

std::string twrpSHA2::create_digest_string() {
	return hexify(&sha256, sizeof(sha256));
}

bool twrpSHA2::update_stream(unsigned char* stream, int len) {
	SHA256_Update(&sha256_ctx, stream, len);
	return true;
}

void twrpSHA2::finalize_stream(void) {
	SHA256_Final(&sha256, &sha256_ctx);
	return;
}

std::string twrpSHA2::return_digest_string(void) {
	unsigned char buf[1024];
	uint64_t bytes;

	init_digest();
        int fd = open(digest_filename.c_str(), O_RDONLY);
        if (fd < 0) {
                printf("Unable to open %s for reading.\n", digest_filename.c_str());
		close(fd);
                return "";
        }
	while (( bytes = read(fd, &buf, sizeof(buf))) == sizeof(buf)) {
		update_stream(buf, bytes);
	}

	close(fd);
	finalize_stream();
	return create_digest_string();
}

bool twrpSHA2::verify_digest(std::string digest) {
	sha2verify = create_digest_string();
	if (sha2verify != digest) {
		return false;
	}
	return true;
}
