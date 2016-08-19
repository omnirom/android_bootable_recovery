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

enum SHA_TYPE {
        T_SHA256 = 0,
        T_SHA512 = 1
};

class twrpSHA: public virtual twrpDigest {
friend class twrpSHA256;
friend class twrpSHA512;

public:
	twrpSHA() : twrpDigest() {};
	twrpSHA(std::string filename);
	void init_digest(void);
	bool update(const unsigned char* stream, size_t len);
	void finalize(void);
	std::string return_digest_string(void);
	bool verify_digest(std::string digest);

protected:
	void set_sha_type(enum SHA_TYPE shatype);
	void check_sha_type(void);
	virtual void init_sha_digest(void) = 0;
	virtual bool update_sha(const unsigned char* stream, size_t len) = 0;
	virtual void finalize_sha(void) = 0;
	virtual std::string create_sha_digest_string(void) = 0;
	virtual std::string return_sha_digest_string(void) = 0;
	virtual bool verify_sha_digest(std::string digest) = 0;
	std::string create_digest_string(void);
	std::string shaverify;
	std::string shafile;

private:
	enum SHA_TYPE sha_type;
	twrpSHA *twrp_sha;
};

class twrpSHA256: public twrpSHA {
friend class twrpSHA;
public:
	twrpSHA256() : twrpSHA() {};
	twrpSHA256(std::string filename);

protected:
	void init_sha_digest(void);
	bool update_sha(const unsigned char* stream, size_t len);
	std::string create_sha_digest_string(void);
	void finalize_sha(void);
	std::string return_sha_digest_string(void);
	bool verify_sha_digest(std::string digest) ;

private:
	uint8_t sha256;
	SHA256_CTX sha256_ctx;
};

class twrpSHA512: public twrpSHA {
friend class twrpSHA;
protected:
	twrpSHA512() : twrpSHA() {};
	twrpSHA512(std::string filename);
	void init_sha_digest(void);
	bool update_sha(const unsigned char* stream, size_t len);
	void finalize_sha(void);
	std::string create_sha_digest_string(void);
	std::string return_sha_digest_string(void);
	bool verify_sha_digest(std::string digest) ;
private:
	uint8_t sha512;
	SHA512_CTX sha512_ctx;
};

#endif //__TWRPSHA_H
