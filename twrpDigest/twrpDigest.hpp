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

#include <vector>
#include "digest/md5/md5.h"
#include "digest/sha2/sha256.h"

enum TWRP_Digest_Type {
	DIGEST_SHA2,
	DIGEST_MD5,
};

class twrpDigest
{
public:
	~twrpDigest() {};
	void set_type(TWRP_Digest_Type type);
	void init_digest(void);
	void init_digest(const string& fn);
	std::string create_digest_string(void);
	bool compute_digest(void);
	bool write_digest(void);
	int read_digest(void);
	int verify_digest(void);
	bool update_stream(unsigned char* stream, int len);

protected:
	int write_file(string fn, string& line);
	bool Path_Exists(string Path);
	int read_file(string fn, string& results);
	bool find_digest_file(void);
	string digestfn;
	string digest_string;
	string digest_verify;
	vector<string> ext;
	TWRP_Digest_Type digest_type;
	SHA256 sha2_hash;
	SHA256 sha2_stream;
	struct MD5Context md5c;
};
