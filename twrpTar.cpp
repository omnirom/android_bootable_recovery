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
	#include "twrpTar.h"
	#include "tarWrite.h"
	#include "libcrecovery/common.h"
}
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <string>
#include <sstream>
#include <dirent.h>
#include <sys/mman.h>
#include "twrpTar.hpp"
#include "twcommon.h"
#include "data.hpp"
#include "variables.h"
#include "twrp-functions.hpp"

using namespace std;

void twrpTar::setfn(string fn) {
	tarfn = fn;
}

void twrpTar::setdir(string dir) {
	tardir = dir;
}

int twrpTar::createTarGZFork() {
	int status;
	pid_t pid;
	if ((pid = fork()) == -1) {
		LOGINFO("create tar failed to fork.\n");
		return -1;
	}
	if (pid == 0) {
		if (createTGZ() != 0)
			exit(-1);
		else
			exit(0);
	}
	else {
		if ((pid = wait(&status)) == -1) {
			LOGINFO("Tar creation failed\n");
			return -1;
		}
		else {
			if (WIFSIGNALED(status) != 0) {
				LOGINFO("Child process ended with signal: %d\n", WTERMSIG(status));
				return -1;
			}
			else if (WIFEXITED(status) != 0)
				LOGINFO("Tar creation successful\n");
			else {
				LOGINFO("Tar creation failed\n");
				return -1;
			}
		}
	}
	return 0;
}

int twrpTar::createTarFork() {
	int status;
	pid_t pid;
	if ((pid = fork()) == -1) {
		LOGINFO("create tar failed to fork.\n");
		return -1;
	}
	if (pid == 0) {
		if (create() != 0)
			exit(-1);
		else
			exit(0);
	}
	else {
		if ((pid = wait(&status)) == -1) {
			LOGINFO("Tar creation failed\n");
			return -1;
		}
		else {
			if (WIFSIGNALED(status) != 0) {
				LOGINFO("Child process ended with signal: %d\n", WTERMSIG(status));
				return -1;
			}
			else if (WEXITSTATUS(status) == 0)
				LOGINFO("Tar creation successful\n");
			else {
				LOGINFO("Tar creation failed\n");
				return -1;
			}
		}
	}
	return 0;
}

int twrpTar::extractTarFork() {
	int status;
	pid_t pid;
	if ((pid = fork()) == -1) {
		LOGINFO("extract tar failed to fork.\n");
		return -1;
	}
	if (pid == 0) {
		if (extract() != 0)
			exit(-1);
		else
			exit(0);
	}
	else {
		if ((pid = wait(&status)) == -1) {
			LOGINFO("Tar extraction failed\n");
			return -1;
		}
		else {
			if (WIFSIGNALED(status) != 0) {
				LOGINFO("Child process ended with signal: %d\n", WTERMSIG(status));
				return -1;
			}
			else if (WEXITSTATUS(status) == 0)
				LOGINFO("Tar extraction successful\n");
			else {
				LOGINFO("Tar extraction failed\n");
				return -1;
			}
		}
	}
	return 0;
}

int twrpTar::splitArchiveFork() {
	int status;
	pid_t pid;
	if ((pid = fork()) == -1) {
		LOGINFO("create tar failed to fork.\n");
		return -1;
	}
	if (pid == 0) {
		if (Split_Archive() != 0)
			exit(-1);
		else
			exit(0);
	}
	else {
		if ((pid = wait(&status)) == -1) {
			LOGINFO("Tar creation failed\n");
			return -1;
		}
		else {
			if (WIFSIGNALED(status) != 0) {
				LOGINFO("Child process ended with signal: %d\n", WTERMSIG(status));
				return -1;
			}
			else if (WIFEXITED(status) != 0)
				LOGINFO("Tar creation successful\n");
			else {
				LOGINFO("Tar creation failed\n");
				return -1;
			}
		}
	}
	return 0;
}

int twrpTar::Generate_Multiple_Archives(string Path) {
	DIR* d;
	struct dirent* de;
	struct stat st;
	string FileName;
	char actual_filename[255];

	if (has_data_media == 1 && Path.size() >= 11 && strncmp(Path.c_str(), "/data/media", 11) == 0)
		return 0; // Skip /data/media
	LOGINFO("Path: '%s', archive filename: '%s'\n", Path.c_str(), tarfn.c_str());

	d = opendir(Path.c_str());
	if (d == NULL)
	{
		LOGERR("error opening '%s' -- error: %s\n", Path.c_str(), strerror(errno));
		closedir(d);
		return -1;
	}
	while ((de = readdir(d)) != NULL)
	{
		FileName = Path + "/";
		FileName += de->d_name;
		if (has_data_media == 1 && FileName.size() >= 11 && strncmp(FileName.c_str(), "/data/media", 11) == 0)
			continue; // Skip /data/media
		if (de->d_type == DT_BLK || de->d_type == DT_CHR)
			continue;
		if (de->d_type == DT_DIR && strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0)
		{
			unsigned long long folder_size = TWFunc::Get_Folder_Size(FileName, false);
			if (Archive_Current_Size + folder_size > MAX_ARCHIVE_SIZE) {
				LOGINFO("Calling Generate_Multiple_Archives\n");
				if (Generate_Multiple_Archives(FileName) < 0)
					return -1;
			} else {
				//FileName += "/";
				LOGINFO("Adding folder '%s'\n", FileName.c_str());
				tardir = FileName;
				if (tarDirs(true) < 0)
					return -1;
				Archive_Current_Size += folder_size;
			}
		}
		else if (de->d_type == DT_REG || de->d_type == DT_LNK)
		{
			stat(FileName.c_str(), &st);

			if (Archive_Current_Size != 0 && Archive_Current_Size + st.st_size > MAX_ARCHIVE_SIZE) {
				LOGINFO("Closing tar '%s', ", tarfn.c_str());
				closeTar(false);
				reinit_libtar_buffer();
				if (TWFunc::Get_File_Size(tarfn) == 0) {
					LOGERR("Backup file size for '%s' is 0 bytes.\n", tarfn.c_str());
					return -1;
				}
				Archive_File_Count++;
				if (Archive_File_Count > 999) {
					LOGERR("Archive count is too large!\n");
					return -1;
				}
				string temp = basefn + "%03i";
				sprintf(actual_filename, temp.c_str(), Archive_File_Count);
				tarfn = actual_filename;
				Archive_Current_Size = 0;
				LOGINFO("Creating tar '%s'\n", tarfn.c_str());
				gui_print("Creating archive %i...\n", Archive_File_Count + 1);
				if (createTar() != 0)
					return -1;
			}
			LOGINFO("Adding file: '%s'... ", FileName.c_str());
			if (addFile(FileName, true) < 0)
				return -1;
			Archive_Current_Size += st.st_size;
			LOGINFO("added successfully, archive size: %llu\n", Archive_Current_Size);
			if (st.st_size > 2147483648LL)
				LOGERR("There is a file that is larger than 2GB in the file system\n'%s'\nThis file may not restore properly\n", FileName.c_str());
		}
	}
	closedir(d);
	return 0;
}

int twrpTar::Split_Archive()
{
	string temp = tarfn + "%03i";
	char actual_filename[255];

	basefn = tarfn;
	Archive_File_Count = 0;
	Archive_Current_Size = 0;
	sprintf(actual_filename, temp.c_str(), Archive_File_Count);
	tarfn = actual_filename;
	init_libtar_buffer(0);
	createTar();
	DataManager::GetValue(TW_HAS_DATA_MEDIA, has_data_media);
	gui_print("Creating archive 1...\n");
	if (Generate_Multiple_Archives(tardir) < 0) {
		LOGERR("Error generating multiple archives\n");
		free_libtar_buffer();
		return -1;
	}
	closeTar(false);
	free_libtar_buffer();
	LOGINFO("Done, created %i archives.\n", (Archive_File_Count++));
	return (Archive_File_Count);
}

int twrpTar::extractTar() {
	char* charRootDir = (char*) tardir.c_str();
	bool gzip = false;
	if (openTar(gzip) == -1)
		return -1;
	if (tar_extract_all(t, charRootDir) != 0) {
		LOGERR("Unable to extract tar archive '%s'\n", tarfn.c_str());
		return -1;
	}
	if (tar_close(t) != 0) {
		LOGERR("Unable to close tar file\n");
		return -1;
	}
	return 0;
}

int twrpTar::getArchiveType() {
        int type = 0;
        string::size_type i = 0;
        int firstbyte = 0, secondbyte = 0;
	char header[3];
        
        ifstream f;
        f.open(tarfn.c_str(), ios::in | ios::binary);
        f.get(header, 3);
        f.close();
        firstbyte = header[i] & 0xff;
        secondbyte = header[++i] & 0xff;

        if (firstbyte == 0x1f && secondbyte == 0x8b)
		type = 1; // Compressed
	else
		type = 0; // Uncompressed

	return type;
}

int twrpTar::extract() {
	int Archive_Current_Type = getArchiveType();

	if (Archive_Current_Type == 1) {
		//if you return the extractTGZ function directly, stack crashes happen
		LOGINFO("Extracting gzipped tar\n");
		int ret = extractTGZ();
		return ret;
	}
	else {
		LOGINFO("Extracting uncompressed tar\n");
		return extractTar();
	}
}

int twrpTar::tarDirs(bool include_root) {
	DIR* d;
	string mainfolder = tardir + "/", subfolder;
	char buf[PATH_MAX];
	d = opendir(tardir.c_str());
	if (d != NULL) {
		struct dirent* de;
		while ((de = readdir(d)) != NULL) {
#ifdef RECOVERY_SDCARD_ON_DATA
			if ((tardir == "/data" || tardir == "/data/") && strcmp(de->d_name, "media") == 0) continue;
#endif
			if (de->d_type == DT_BLK || de->d_type == DT_CHR || strcmp(de->d_name, "..") == 0)
				continue;
			subfolder = mainfolder;
			if (strcmp(de->d_name, ".") != 0) {
				subfolder += de->d_name;
			} else {
				LOGINFO("adding '%s'\n", subfolder.c_str());
				if (addFile(subfolder, include_root) != 0)
					return -1;
				continue;
			}
			LOGINFO("adding '%s'\n", subfolder.c_str());
			strcpy(buf, subfolder.c_str());
			if (de->d_type == DT_DIR) {
				char* charTarPath;
				if (include_root) {
					charTarPath = NULL;
				} else {
					string temp = Strip_Root_Dir(buf);
					charTarPath = (char*) temp.c_str();
				}
				if (tar_append_tree(t, buf, charTarPath) != 0) {
					LOGERR("Error appending '%s' to tar archive '%s'\n", buf, tarfn.c_str());
					return -1;
				}
			} else if (tardir != "/" && (de->d_type == DT_REG || de->d_type == DT_LNK)) {
				if (addFile(buf, include_root) != 0)
					return -1;
			}
			fflush(NULL);
		}
		closedir(d);
	}
	return 0;
}

int twrpTar::createTGZ() {
	bool gzip = true;

	init_libtar_buffer(0);
	if (createTar() == -1)
		return -1;
	if (tarDirs(false) == -1)
		return -1;
	if (closeTar(gzip) == -1)
		return -1;
	free_libtar_buffer();
	return 0;
}

int twrpTar::create() {
	bool gzip = false;

	init_libtar_buffer(0);
	if (createTar() == -1)
		return -1;
	if (tarDirs(false) == -1)
		return -1;
	if (closeTar(gzip) == -1)
		return -1;
	free_libtar_buffer();
	return 0;
}

int twrpTar::addFilesToExistingTar(vector <string> files, string fn) {
	char* charTarFile = (char*) fn.c_str();
	static tartype_t type = { open, close, read, write_tar };

	init_libtar_buffer(0);
	if (tar_open(&t, charTarFile, &type, O_RDONLY | O_LARGEFILE, 0644, TAR_GNU) == -1)
		return -1;
	removeEOT(charTarFile);
	if (tar_open(&t, charTarFile, &type, O_WRONLY | O_APPEND | O_LARGEFILE, 0644, TAR_GNU) == -1)
		return -1;
	for (unsigned int i = 0; i < files.size(); ++i) {
		char* file = (char*) files.at(i).c_str();
		if (tar_append_file(t, file, file) == -1)
			return -1;
	}
	flush_libtar_buffer(t->fd);
	if (tar_append_eof(t) == -1)
		return -1;
	if (tar_close(t) == -1)
		return -1;
	free_libtar_buffer();
	return 0;
}

int twrpTar::createTar() {
	char* charTarFile = (char*) tarfn.c_str();
	char* charRootDir = (char*) tardir.c_str();
	int use_compression = 0;
	static tartype_t type = { open, close, read, write_tar };

	DataManager::GetValue(TW_USE_COMPRESSION_VAR, use_compression);
	if (use_compression) {
		string cmd = "pigz - > '" + tarfn + "'";
		p = popen(cmd.c_str(), "w");
		fd = fileno(p);
		if (!p) return -1;
		if(tar_fdopen(&t, fd, charRootDir, &type, O_RDONLY | O_LARGEFILE, 0644, TAR_GNU) != 0) {
			pclose(p);
			return -1;
		}
	}
	else {
		if (tar_open(&t, charTarFile, &type, O_WRONLY | O_CREAT | O_LARGEFILE, 0644, TAR_GNU) == -1)
			return -1;
	}
	return 0;
}

int twrpTar::openTar(bool gzip) {
	char* charRootDir = (char*) tardir.c_str();
	char* charTarFile = (char*) tarfn.c_str();

	if (gzip) {
		LOGINFO("Opening as a gzip\n");
		string cmd = "pigz -d -c '" + tarfn + "'";
		FILE* pipe = popen(cmd.c_str(), "r");
		int fd = fileno(pipe);
		if (!pipe) return -1;
		if(tar_fdopen(&t, fd, charRootDir, NULL, O_RDONLY | O_LARGEFILE, 0644, TAR_GNU) != 0) {
			LOGINFO("tar_fdopen returned error\n");
			__pclose(pipe);
			return -1;
		}
	}
	else {
		if (tar_open(&t, charTarFile, NULL, O_RDONLY | O_LARGEFILE, 0644, TAR_GNU) != 0) {
			LOGERR("Unable to open tar archive '%s'\n", charTarFile);
			return -1;
		}
	}
	return 0;
}

string twrpTar::Strip_Root_Dir(string Path) {
	string temp;
	size_t slash;

	if (Path.substr(0, 1) == "/")
		temp = Path.substr(1, Path.size() - 1);
	else
		temp = Path;
	slash = temp.find("/");
	if (slash == string::npos)
		return temp;
	else {
		string stripped;

		stripped = temp.substr(slash, temp.size() - slash);
		return stripped;
	}
	return temp;
}

int twrpTar::addFile(string fn, bool include_root) {
	char* charTarFile = (char*) fn.c_str();
	if (include_root) {
		if (tar_append_file(t, charTarFile, NULL) == -1)
			return -1;
	} else {
		string temp = Strip_Root_Dir(fn);
		char* charTarPath = (char*) temp.c_str();
		if (tar_append_file(t, charTarFile, charTarPath) == -1)
			return -1;
	}
	return 0;
}

int twrpTar::closeTar(bool gzip) {
	int use_compression;
	DataManager::GetValue(TW_USE_COMPRESSION_VAR, use_compression);

	flush_libtar_buffer(t->fd);
	if (tar_append_eof(t) != 0) {
		LOGERR("tar_append_eof(): %s\n", strerror(errno));
		tar_close(t);
		return -1;
	}
	if (tar_close(t) != 0) {
		LOGERR("Unable to close tar archive: '%s'\n", tarfn.c_str());
		return -1;
	}
	if (use_compression || gzip) {
		LOGINFO("Closing popen and fd\n");
		pclose(p);
		close(fd);
	}
	return 0;
}

int twrpTar::removeEOT(string tarFile) {
	char* charTarFile = (char*) tarFile.c_str();
	off_t tarFileEnd;
	while (th_read(t) == 0) {
		if (TH_ISREG(t))
			tar_skip_regfile(t);
		tarFileEnd = lseek(t->fd, 0, SEEK_CUR);
	}
	if (tar_close(t) == -1)
		return -1;
	if (truncate(charTarFile, tarFileEnd) == -1)
		return -1;
	return 0;
}

int twrpTar::compress(string fn) {
	string cmd = "pigz " + fn;
	p = popen(cmd.c_str(), "r");
	if (!p) return -1;
	char buffer[128];
	string result = "";
	while(!feof(p)) {
		if(fgets(buffer, 128, p) != NULL)
			result += buffer;
	}
	__pclose(p);
	return 0;
}

int twrpTar::extractTGZ() {
	string splatrootdir(tardir);
	bool gzip = true;
	char* splatCharRootDir = (char*) splatrootdir.c_str();
	if (openTar(gzip) == -1)
		return -1;
	int ret = tar_extract_all(t, splatCharRootDir);
	if (tar_close(t) != 0) {
		LOGERR("Unable to close tar file\n");
		return -1;
	}
	return 0;
}

int twrpTar::entryExists(string entry) {
	char* searchstr = (char*)entry.c_str();
	int ret;

	int Archive_Current_Type = getArchiveType();

	if (openTar(Archive_Current_Type) == -1)
		ret = 0;
	else
		ret = tar_find(t, searchstr);

	if (tar_close(t) != 0)
		LOGINFO("Unable to close tar file after searching for entry '%s'.\n", entry.c_str());

	return ret;
}

extern "C" ssize_t write_tar(int fd, const void *buffer, size_t size) {
	return (ssize_t) write_libtar_buffer(fd, buffer, size);
}
