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
#include "twrpDU.hpp"
#include "partitions.hpp"
#include "orscmd/orscmd.h"
#include "twrpTar.h"
#include "twrp-functions.hpp"

using namespace std;

struct TarListStruct {
	std::string fn;
	unsigned thread_id;
};

struct thread_data_struct {
	std::vector<TarListStruct> *TarList;
	unsigned thread_id;
};


class twrpTar {
public:
	twrpTar();
	virtual ~twrpTar();
	int createTarFork(const uint64_t *overall_size, const uint64_t *other_backups_size, pid_t *fork_pid);
	int extractTarFork(RestorePartitionSettings *part_settings);
	void setfn(string fn);
	void setdir(string dir);
	void setsize(unsigned long long backup_size);
	void setpassword(string pass);
	unsigned long long get_size();
	void Set_Archive_Type(Archive_Type archive_type);

public:
	int use_encryption;
	int userdata_encryption;
	int use_compression;
	int split_archives;
	int has_data_media;
	string backup_name;
	int progress_pipe_fd;
	string partition_name;
	string backup_folder;
	enum Archive_Type current_archive_type;
	bool adbbackup;
	bool adb_compression;

private:
	int extract();
	int addFilesToExistingTar(vector <string> files, string tarFile);
	int createTar();
	int addFile(string fn, bool include_root);
	int entryExists(string entry);
	int closeTar();
	int removeEOT(string tarFile);
	int extractTar();
	string Strip_Root_Dir(string Path);
	int openTar();
	int Generate_TarList(string Path, std::vector<TarListStruct> *TarList, unsigned long long *Target_Size, unsigned *thread_id);
	static void* createList(void *cookie);
	static void* extractMulti(void *cookie);
	int tarList(std::vector<TarListStruct> *TarList, unsigned thread_id);
	unsigned long long uncompressedSize(string filename);
	static void Signal_Kill(int signum);

	unsigned long long Archive_Current_Size;
	unsigned long long Total_Backup_Size;
	bool include_root_dir;
	TAR *t;
	int fd;
	int input_fd;
	pid_t pigz_pid;
	pid_t oaes_pid;
	unsigned long long file_count;

	string tardir;
	string tarfn;
	string basefn;
	string password;

	std::vector<TarListStruct> *ItemList;
	int output_fd;
	int adb_control_fd;
	unsigned thread_id;
};
