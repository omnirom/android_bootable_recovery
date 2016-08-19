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

#ifndef __TWRPDIGEST_H
#define __TWRPDIGEST_H

class twrpDigest {
friend class twrpMD5;
public:
	twrpDigest();
	twrpDigest(std::string filename);
	virtual ~twrpDigest() {};
	bool read_file(void);
	virtual bool update_stream(unsigned char* stream, int len) = 0;
	virtual std::string return_digest_string(void) = 0;
	virtual bool verify_digest(std::string digest) = 0;
	virtual std::string create_digest_string(void) = 0;
	virtual void init_digest(void) = 0;
	virtual void finalize_stream(void) = 0;

protected:
	std::string hexify(uint8_t* hash, int len);
	std::string digest_filename;
	twrpDigest *digest;
};

#endif //__TWRPDIGEST_H
