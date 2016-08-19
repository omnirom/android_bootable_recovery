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

using namespace std;

class twrpDigest
{
public:
	virtual ~twrpDigest() {};
	void setfn(const string& fn);
	virtual int computeDigest(void) = 0;
	virtual int verify_digest(void) = 0;
	virtual int write_digest(void) = 0;
	virtual int read_digest(void) = 0;
	virtual int updateStream(unsigned char* stream, int len) = 0;
	virtual void finalizeStream(void) = 0;
	virtual string createDigestString(void) = 0;

protected:
	int write_file(string fn, string& line);
	bool Path_Exists(string Path);
	int read_file(string fn, string& results);
	string digestfn;
};
