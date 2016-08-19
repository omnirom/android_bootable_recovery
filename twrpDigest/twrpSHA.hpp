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
#ifndef __TWRPSHA_H
#define __TWRPSHA_H

#include <openssl/sha.h>
#include "twrpDigest.hpp"

class twrpSHA256: public twrpDigest {
public:
	twrpSHA256();                                            // Initialize the SHA256 digest for streaming activities only
	void init();                                             // Initialize the SHA256 digest algorithm

protected:
	void update(const unsigned char* stream, size_t len);    // Update the SHA256 digest stream
	void finalize();                                         // Finalize the SHA256 digest for computation
	std::string return_digest_string();                      // Return the digest string computed to the callee

private:
	uint8_t sha256_store[SHA256_DIGEST_LENGTH];              // Initialize the SHA256 digest array that holds the computation
	SHA256_CTX sha256_ctx;                                   // Initialize the SHA256 control structure
};

class twrpSHA512: public twrpDigest {
public:
	twrpSHA512();                                            // Initialize the SHA512 digest for streaming activities only
	void init();                                             // Initialize the SHA512 digest algorithm

protected:
	void update(const unsigned char* stream, size_t len);    // Update the SHA512 digest stream
	void finalize();                                         // Finalize the SHA512 digest for computation
	std::string return_digest_string();                      // Return the digest string computed to the callee

private:
	uint8_t sha512_store[SHA512_DIGEST_LENGTH];              // Initialize the SHA512 digest array that holds the computation
	SHA512_CTX sha512_ctx;                                   // Initialize the SHA512 control structure
};

#endif //__TWRPSHA_H
