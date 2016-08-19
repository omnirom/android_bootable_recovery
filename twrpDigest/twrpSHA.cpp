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
#include <openssl/sha.h>
#include "twrpDigest.hpp"
#include "twrpSHA.hpp"

twrpSHA256::twrpSHA256() {
	twrpSHA256::init();
}

void twrpSHA256::init(void) {
	SHA256_Init(&sha256_ctx);
}

void twrpSHA256::update(const unsigned char* stream, size_t len) {
	SHA256_Update(&sha256_ctx, stream, len);
}

void twrpSHA256::finalize(void) {
	SHA256_Final(sha256_store, &sha256_ctx);
}

std::string twrpSHA256::return_digest_string(void) {
	twrpSHA256::finalize();
	std::string digest_str = twrpDigest::hexify(sha256_store, SHA256_DIGEST_LENGTH);
	return digest_str;
}

twrpSHA512::twrpSHA512() {
	twrpSHA512::init();
}

void twrpSHA512::init(void) {
	SHA512_Init(&sha512_ctx);
}

void twrpSHA512::update(const unsigned char* stream, size_t len) {
	SHA512_Update(&sha512_ctx, stream, len);
}

void twrpSHA512::finalize(void) {
	SHA512_Final(sha512_store, &sha512_ctx);
}

std::string twrpSHA512::return_digest_string(void) {
	twrpSHA512::finalize();
	std::string digest_str = twrpDigest::hexify(sha512_store, SHA512_DIGEST_LENGTH);
	return digest_str;
}
