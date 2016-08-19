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

#ifndef __TWRPDIGEST_H
#define __TWRPDIGEST_H

class twrpDigest {
public:
	twrpDigest() {};
	virtual ~twrpDigest() {};
	bool read_file(std::string digest_filename);                     // Read file into digest algorithm
	virtual bool update(const unsigned char* stream, size_t len) = 0;         // Update the digest with new data
	virtual std::string return_digest_string(void) = 0;              // Returns the digest of the file as a string instead of the raw format
	bool verify_digest(std::string digest); 	                 // Verify the digest of the file against the passed string

protected:
	virtual void init(void) = 0;                                     // Initialize the digest according to the algorithm
	virtual void finalize(void) = 0;                                 // Finalize the digest input for creating the final digest
	std::string hexify(uint8_t* hash, size_t len);                   // Take an 8 bit byte and turn it into a hex string
	std::string digestVerify;
};

#endif //__TWRPDIGEST_H
