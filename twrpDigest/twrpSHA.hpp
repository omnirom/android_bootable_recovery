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
#ifndef __TWRPSHA_H
#define __TWRPSHA_H

#include <openssl/sha.h>
#include "twrpDigest.hpp"

class twrpSHA256: public twrpDigest {
friend class twrpSHA;

public:
	twrpSHA256();
	twrpSHA256(std::string filename);

protected:
	void init(void);
	bool update(const unsigned char* stream, size_t len);
	void finalize(void);
	std::string return_digest_string(void);
	bool verify_digest(std::string digest);

private:
	uint8_t sha256;
	SHA256_CTX sha256_ctx;
	std::string digest_filename;
	std::string shaverify;
};

class twrpSHA512: public twrpDigest {
friend class twrpSHA;

public:
	twrpSHA512();
	twrpSHA512(std::string filename);

protected:
	void init(void);
	bool update(const unsigned char* stream, size_t len);
	void finalize(void);
	std::string return_digest_string(void);
	bool verify_digest(std::string digest) ;

private:
	uint8_t sha512;
	SHA512_CTX sha512_ctx;
	std::string digest_filename;
	std::string shaverify;
};

#endif //__TWRPSHA_H
