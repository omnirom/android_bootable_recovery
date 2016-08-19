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

class twrpDigest
{
public:
	virtual ~twrpDigest() {};
	void set_filename(std::string& filename); 
	virtual bool update_stream(unsigned char* stream, int len) = 0;
	virtual std::string create_digest_string(void) = 0;
	virtual std::string return_digest_string(void) = 0;
	virtual bool verify_digest(std::string digest) = 0;

protected:
	std::string digest_filename;
};
