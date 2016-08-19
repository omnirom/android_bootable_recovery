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

extern "C"
{
	#include "digest/md5/md5.h"
	#include "../libcrecovery/common.h"
}

#include <vector>
#include <string>
#include <sstream>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <string.h>
#include <libgen.h>
#include <errno.h>
#include <fcntl.h>
#include <fstream>
#include "twrpDigest.hpp"
#include "digest/sha2/sha256.h"
#include "digest/md5/md5.h"

using namespace std;

void twrpDigest::set_type(TWRP_Digest_Type type) {
	digest_type = type;
	printf("digest_type: %d\n", digest_type);
}

void twrpDigest::init_digest(void) {
	digest_type = DIGEST_MD5;
	MD5Init(&md5c);
}

void twrpDigest::init_digest(const string& fn) {
	digestfn = fn;
	int extpos = digestfn.find_last_of(".");
	string digest_ext = digestfn.substr(extpos + 1, digestfn.length());

	if (digest_type == DIGEST_SHA2) {
		SHA256 sha2_hash;
		SHA256 sha2_stream;
	}

	if (digest_type == DIGEST_SHA2) {
		ext.push_back(".sha");
		ext.push_back(".sha2");
	}
	else if (digest_type == DIGEST_MD5) {
		ext.push_back(".md5");
		ext.push_back(".md5sum");
	}
}

int twrpDigest::write_file(string fn, string& line) {
        FILE *file;
        file = fopen(fn.c_str(), "w");
        if (file != NULL) {
                fwrite(line.c_str(), line.size(), 1, file);
                fclose(file);
                return 0;
        }
        printf("Cannot find file %s\n", fn.c_str());
        return -1;
}

bool twrpDigest::Path_Exists(string Path) {
        struct stat st;
        if (stat(Path.c_str(), &st) != 0)
                return false;
        else
                return true;
}

int twrpDigest::read_file(string filename, string& results) {
        ifstream file;
        file.open(filename.c_str(), ios::in);

        if (file.is_open()) {
                file >> results;
                file.close();
                return 0;
        }

        printf("Cannot find file %s\n", filename.c_str());
        return -1;
}

std::string twrpDigest::create_digest_string(void) {
	if (digest_type == DIGEST_SHA2) {
		return sha2_stream.getHash();
	}
	else if (digest_type == DIGEST_MD5) {
		unsigned char md5sum[MD5LENGTH];	
		MD5Final(md5sum, &md5c);
		char hex[3];
		for (int i = 0; i < 16; ++i) {
			snprintf(hex, 3, "%02x", md5sum[i]);
			printf("hex: %02x", md5sum[i]);
			digest_string += hex;
		}
		if (!digestfn.empty()) {
			digest_string += "  ";
			digest_string += basename((char*) digestfn.c_str());
			digest_string +=  + "\n";
		}
	}
	return digest_string;
}

bool twrpDigest::compute_digest(void) {
        unsigned char buf[1024];
        uint64_t bytes;

        int fd = open(digestfn.c_str(), O_RDONLY);
        if (fd < 0) {
                printf("Unable to open %s for reading.\n", digestfn.c_str());
                close(fd);
                return false;
        }

        while (( bytes = read(fd, &buf, sizeof(buf))) == sizeof(buf)) {
                if (digest_type == DIGEST_SHA2) {
			sha2_stream.add(buf, bytes);
		}
		else if (digest_type == DIGEST_MD5) {
			printf("adding md5 data\n");
			MD5Update(&md5c, buf, bytes);
		}
        }

        close(fd);
        return true;
}

bool twrpDigest::write_digest(void) {
        digest_string = create_digest_string();
        digest_string += "  ";
        digest_string = digest_string + basename((char*) digestfn.c_str());
        write_file(digestfn, digest_string);
        printf("Digest for %s: %s\n", digestfn.c_str(), digest_string.c_str());
	return true;
}

bool twrpDigest::find_digest_file(void) {
        bool foundFile = false;
	for ( size_t extsize = 0; extsize < ext.size(); ++extsize) {
		string file = digestfn + ext[extsize];
		if (Path_Exists(file)) {
			foundFile = true;
			break;
		}
	}
	return foundFile;
}

int twrpDigest::read_digest(void) {
        string file = "";
        string line;

        bool foundFile = find_digest_file();
	if (!foundFile) {
                printf("Skipping Digest check: no Digest file found");
                return -1;
        } else if (read_file(file, digest_verify) != 0) {
                printf("E: Skipping Digest check: Digest file unreadable %s\n", strerror(errno));
                return 1;
        }
	return 0;
}

/* verify_digest return codes:
        -2: digest did not match
        -1: no digest file found
         0: digest matches
         1: digest file unreadable
*/

int twrpDigest::verify_digest(void) {
        string buf, digest_str;
        char hex[3];
        int i, ret;

        ret = read_digest();
        if (ret != 0)
                return ret;

        digest_verify += "  ";
        digest_verify = digest_verify + basename((char*) digestfn.c_str());
        ret = compute_digest();
        if (ret == -1)
                return ret;
        digest_string = create_digest_string();
        digest_string += "  ";
        digest_string = digest_string + basename((char*) digestfn.c_str());

        printf("Digest: %s\n", digest_string.c_str());
        printf("Digest Verification: %s\n", digest_verify.c_str());
        if (digest_string != digest_verify) {
                printf("E: Digest does not match\n");
                return -2;
        }

        printf("Digest matched\n");
        return 0;
}

bool twrpDigest::update_stream(unsigned char* stream, int len) {
        if (digestfn.empty()) {
		if (digest_type == DIGEST_MD5) {
			MD5Update(&md5c, stream, len);
		}
        }
        else {  
                return false;
        }
        return true;
}
