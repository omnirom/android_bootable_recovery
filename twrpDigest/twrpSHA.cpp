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

twrpSHA::twrpSHA(std::string filename) : twrpDigest(filename) {}

void twrpSHA::check_sha_type(void) {
	if (sha_type == T_SHA256) {
		twrp_sha = new twrpSHA256();
	}
}

void twrpSHA::init(void) {
	init_sha_digest();
}

std::string twrpSHA::create_digest_string() {
	return twrp_sha->create_sha_digest_string();
}

bool twrpSHA::update(const unsigned char* stream, size_t len) {
	return update_sha(stream, len);
}

void twrpSHA::finalize(void) {
	finalize_sha();
	return;
}

std::string twrpSHA::return_digest_string(void) {
	check_sha_type();
	return return_sha_digest_string();
}

bool twrpSHA::verify_digest(std::string digest) {
	return verify_sha_digest(digest);;
}

twrpSHA256::twrpSHA256(std::string filename) : twrpSHA(filename), twrpDigest(filename) {}

void twrpSHA256::init_sha_digest(void) {
	sha_type = T_SHA256;
	SHA256_Init(&sha256_ctx);
}

std::string twrpSHA256::create_sha_digest_string() {
	std::string digest_str = twrpDigest::hexify(&sha256, SHA256_DIGEST_LENGTH);
	digest_str += "  ";
	digest_str += basename((char*) twrpDigest::digest_filename.c_str());
	digest_str += "\n";
	return digest_str;
}

bool twrpSHA256::update_sha(const unsigned char* stream, size_t len) {
	SHA256_Update(&sha256_ctx, stream, len);
	return true;
}

void twrpSHA256::finalize_sha(void) {
	SHA256_Final(&sha256, &sha256_ctx);
}

std::string twrpSHA256::return_sha_digest_string(void) {
	twrpSHA256::init_sha_digest();

	twrpDigest::read_file();
	twrpSHA256::finalize();

	return twrpSHA256::create_sha_digest_string();
}

bool twrpSHA256::verify_sha_digest(std::string digest) {
	shaverify = twrpSHA256::return_sha_digest_string();
	printf("shaverify: %s\n", shaverify.c_str());
	printf("digest: %s\n", digest.c_str());
	if (shaverify != digest) {
		return false;
	}
	return true;
}

twrpSHA512::twrpSHA512(std::string filename) : twrpSHA(filename), twrpDigest(filename) {}

void twrpSHA512::init_sha_digest(void) {
	sha_type = T_SHA512;
	SHA512_Init(&sha512_ctx);
}

std::string twrpSHA512::create_sha_digest_string() {
	return twrpDigest::hexify(&sha512, SHA512_DIGEST_LENGTH);
}

bool twrpSHA512::update_sha(const unsigned char* stream, size_t len) {
	SHA512_Update(&sha512_ctx, stream, len);
	return true;
}

void twrpSHA512::finalize_sha(void) {
	SHA512_Final(&sha512, &sha512_ctx);
}

std::string twrpSHA512::return_sha_digest_string(void) {
	char buf[1024];
	int bytes;
	int fd;

	fd = open(twrpDigest::digest_filename.c_str(), O_RDONLY);
	if (fd < 0) {
		return "";
	}

	twrpSHA512::init_sha_digest();
	while ((bytes = read(fd, &buf, sizeof(buf))) == sizeof(buf)) {
		twrpSHA512::update((unsigned char*)buf, bytes);
	}

	twrpSHA512::finalize();
	close(fd);

	return twrpSHA512::create_sha_digest_string();
}

bool twrpSHA512::verify_sha_digest(std::string digest) {
	shaverify = twrpSHA512::create_sha_digest_string();
	if (shaverify != digest) {
		return false;
	}
	return true;
}
