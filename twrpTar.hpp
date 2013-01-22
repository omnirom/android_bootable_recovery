/*
        Copyright 2012 bigbiff/Dees_Troy TeamWin
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

extern "C" {
        #include "libtar/libtar.h"
}
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <fstream>
#include <string>
#include <vector>

using namespace std;

class twrpTar {
	public:
		int extract();
		int compress(string fn);
		int uncompress(string fn);
                int addFilesToExistingTar(vector <string> files, string tarFile);
		int createTar();
		int addFile(string fn, bool include_root);
		int closeTar(bool gzip);
		int createTarGZThread();
		int createTarThread();
		int extractTarThread();
		int splitArchiveThread();
                void setfn(string fn);
                void setdir(string dir);
	private:
		int createTGZ();
		int create();
		int Split_Archive();
		int removeEOT(string tarFile);
		int extractTar();
		int tarDirs(bool include_root);
		int Generate_Multiple_Archives(string Path);
		string Strip_Root_Dir(string Path);
		int extractTGZ();
		int openTar(bool gzip);
		int has_data_media;
		int Archive_File_Count;
		unsigned long long Archive_Current_Size;
		TAR *t;
		FILE* p;
		int fd;
		string tardir;
		string tarfn;
		string basefn;
		typedef int (twrpTar::*ThreadPtr)(void);
		typedef void* (*PThreadPtr)(void*);
}; 
