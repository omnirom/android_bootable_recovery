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
#include <string>
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
#include "twrpSHA.hpp"

twrpSHA256::twrpSHA256(std::string filename) {
	digest_filename = filename;
}

void twrpSHA256::init(void) {
	SHA256_Init(&sha256_ctx);
}

bool twrpSHA256::update(const unsigned char* stream, size_t len) {
	SHA256_Update(&sha256_ctx, stream, len);
	return true;
}

void twrpSHA256::finalize(void) {
	SHA256_Final(&sha256, &sha256_ctx);
}

std::string twrpSHA256::return_digest_string(void) {
	twrpSHA256::init();
	twrpDigest::read_file(digest_filename);
	twrpSHA256::finalize();
	std::string digest_str = twrpDigest::hexify(&sha256, SHA256_DIGEST_LENGTH);
	digest_str += "  ";
	digest_str += basename((char*) digest_filename.c_str());
	digest_str += "\n";
	return digest_str;
}

twrpSHA512::twrpSHA512(std::string filename) {
	digest_filename = filename;
}

void twrpSHA512::init(void) {
	SHA512_Init(&sha512_ctx);
}

bool twrpSHA512::update(const unsigned char* stream, size_t len) {
	SHA512_Update(&sha512_ctx, stream, len);
	return true;
}

void twrpSHA512::finalize(void) {
	SHA512_Final(&sha512, &sha512_ctx);
}

std::string twrpSHA512::return_digest_string(void) {
	twrpSHA512::init();
	twrpDigest::read_file(digest_filename);
	twrpSHA512::finalize();
	std::string digest_str = twrpDigest::hexify(&sha512, SHA512_DIGEST_LENGTH);
	digest_str += "  ";
	digest_str += basename((char*) digest_filename.c_str());
	digest_str += "\n";
	return digest_str;
}
