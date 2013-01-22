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
#include <fstream>
#include <iostream>
#include <string>
#include <sstream>
#include <dirent.h>
#include <sys/mman.h>
#include "twrpTar.hpp"
#include "common.h"
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

int twrpTar::createTarGZThread() {
	pthread_t thread;
	ThreadPtr tarptr = &twrpTar::createTGZ;
	PThreadPtr p = *(PThreadPtr*)&tarptr;
	pthread_create(&thread, NULL, p, this);
	if(pthread_join(thread, NULL)) {
		return -1;
	}
	return 0;
}

int twrpTar::createTarThread() {
	pthread_t thread;
	ThreadPtr tarptr = &twrpTar::create;
	PThreadPtr p = *(PThreadPtr*)&tarptr;
	pthread_create(&thread, NULL, p, this);
	if(pthread_join(thread, NULL)) {
		return -1;
	}
	return 0;
}

int twrpTar::extractTarThread() {
	pthread_t thread;
	ThreadPtr tarptr = &twrpTar::extract;
	PThreadPtr p = *(PThreadPtr*)&tarptr;
	pthread_create(&thread, NULL, p, this);
	if(pthread_join(thread, NULL)) {
		return -1;
	}
	return 0;
}

int twrpTar::splitArchiveThread() {
	pthread_t thread;
	ThreadPtr tarptr = &twrpTar::Split_Archive;
	PThreadPtr p = *(PThreadPtr*)&tarptr;
	pthread_create(&thread, NULL, p, this);
	if(pthread_join(thread, NULL)) {
		return -1;
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
	LOGI("Path: '%s', archive filename: '%s'\n", Path.c_str(), tarfn.c_str());

	d = opendir(Path.c_str());
	if (d == NULL)
	{
		LOGE("error opening '%s' -- error: %s\n", Path.c_str(), strerror(errno));
		closedir(d);
		return -1;
	}
	while ((de = readdir(d)) != NULL)
	{
		FileName = Path + "/";
		FileName += de->d_name;
		if (has_data_media == 1 && FileName.size() >= 11 && strncmp(FileName.c_str(), "/data/media", 11) == 0)
			continue; // Skip /data/media
		if (de->d_type == DT_DIR && strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0)
		{
			unsigned long long folder_size = TWFunc::Get_Folder_Size(FileName, false);
			tardir = FileName;
			if (Archive_Current_Size + folder_size > MAX_ARCHIVE_SIZE) {
				LOGI("Calling Generate_Multiple_Archives\n");
				if (Generate_Multiple_Archives(FileName) < 0)
					return -1;
			} else {
				//FileName += "/";
				LOGI("Adding folder '%s'\n", FileName.c_str());
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
				LOGI("Closing tar '%s', ", tarfn.c_str());
				closeTar(false);
				if (TWFunc::Get_File_Size(tarfn) == 0) {
					LOGE("Backup file size for '%s' is 0 bytes.\n", tarfn.c_str());
					return -1;
				}
				Archive_File_Count++;
				if (Archive_File_Count > 999) {
					LOGE("Archive count is too large!\n");
					return -1;
				}
				string temp = basefn + "%03i";
				sprintf(actual_filename, temp.c_str(), Archive_File_Count);
				tarfn = actual_filename;
				Archive_Current_Size = 0;
				LOGI("Creating tar '%s'\n", tarfn.c_str());
				ui_print("Creating archive %i...\n", Archive_File_Count + 1);
				if (createTar() != 0)
					return -1;
			}
			LOGI("Adding file: '%s'... ", FileName.c_str());
			if (addFile(FileName, true) < 0)
				return -1;
			Archive_Current_Size += st.st_size;
			LOGI("added successfully, archive size: %llu\n", Archive_Current_Size);
			if (st.st_size > 2147483648LL)
				LOGE("There is a file that is larger than 2GB in the file system\n'%s'\nThis file may not restore properly\n", FileName.c_str());
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
	createTar();
	DataManager::GetValue(TW_HAS_DATA_MEDIA, has_data_media);
	ui_print("Creating archive 1...\n");
	if (Generate_Multiple_Archives(tardir) < 0) {
		LOGE("Error generating file list\n");
		return -1;
	}
	closeTar(false);
	LOGI("Done, created %i archives.\n", (Archive_File_Count++));
	return (Archive_File_Count);
}

int twrpTar::extractTar() {
        char* charRootDir = (char*) tardir.c_str();
	bool gzip = false;
	if (openTar(gzip) == -1)
		return -1;
	if (tar_extract_all(t, charRootDir) != 0) {
		LOGE("Unable to extract tar archive '%s'\n", tarfn.c_str());
		return -1;
	}
	if (tar_close(t) != 0) {
		LOGE("Unable to close tar file\n");
		return -1;
	}
	return 0;
}

int twrpTar::extract() {
        int len = 3;
        char header[len];
        string::size_type i = 0;
        int firstbyte = 0;
        int secondbyte = 0;
        int ret;
        ifstream f;
        f.open(tarfn.c_str(), ios::in | ios::binary);
        f.get(header, len);
        firstbyte = header[i] & 0xff;
        secondbyte = header[++i] & 0xff;
        f.close();
        if (firstbyte == 0x1f && secondbyte == 0x8b) {
		//if you return the extractTGZ function directly, stack crashes happen
		LOGI("Extracting gzipped tar\n");
		ret = extractTGZ();
		return ret;
	}
	else {
		LOGI("Extracting uncompressed tar\n");
		return extractTar();
	}
}

int twrpTar::tarDirs(bool include_root) {
        DIR* d;
        string mainfolder = tardir + "/", subfolder;
        char buf[1024];
        char* charTarFile = (char*) tarfn.c_str();
        d = opendir(tardir.c_str());
        if (d != NULL) {
                struct dirent* de;
                while ((de = readdir(d)) != NULL) {
                        LOGI("adding %s\n", de->d_name);
#ifdef RECOVERY_SDCARD_ON_DATA
                        if ((tardir == "/data" || tardir == "/data/") && strcmp(de->d_name, "media") == 0) continue;
#endif
                        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)   continue;

                        subfolder = mainfolder;
                        subfolder += de->d_name;
                        strcpy(buf, subfolder.c_str());
                        if (de->d_type == DT_DIR) {
					if (include_root) {
                                if (tar_append_tree(t, buf, NULL) != 0) {
                                        LOGE("Error appending '%s' to tar archive '%s'\n", buf, charTarFile);
                                        return -1;
                                }
							} else {
								string temp = Strip_Root_Dir(buf);
								char* charTarPath = (char*) temp.c_str();
								if (tar_append_tree(t, buf, charTarPath) != 0) {
                                        LOGE("Error appending '%s' to tar archive '%s'\n", buf, charTarFile);
                                        return -1;
                                }
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
	if (createTar() == -1)
		return -1;
	if (tarDirs(false) == -1)
		return -1;
	if (closeTar(gzip) == -1)
		return -1;
        return 0;
}

int twrpTar::create() {
        bool gzip = false;
	if (createTar() == -1)
		return -1;
	if (tarDirs(false) == -1)
		return -1;
	if (closeTar(gzip) == -1)
		return -1;
	return 0;
}

int twrpTar::addFilesToExistingTar(vector <string> files, string fn) {
	char* charTarFile = (char*) fn.c_str();

	if (tar_open(&t, charTarFile, NULL, O_RDONLY | O_LARGEFILE, 0644, TAR_GNU) == -1)
		return -1;
	removeEOT(charTarFile);
	if (tar_open(&t, charTarFile, NULL, O_WRONLY | O_APPEND | O_LARGEFILE, 0644, TAR_GNU) == -1)
		return -1;
	for (unsigned int i = 0; i < files.size(); ++i) {
		char* file = (char*) files.at(i).c_str();
		if (tar_append_file(t, file, file) == -1)
			return -1;
	}
	if (tar_append_eof(t) == -1)
		return -1;
	if (tar_close(t) == -1)
		return -1;
	return 0;
}

int twrpTar::createTar() {
	char* charTarFile = (char*) tarfn.c_str();
        char* charRootDir = (char*) tardir.c_str();
	int use_compression = 0;

	DataManager::GetValue(TW_USE_COMPRESSION_VAR, use_compression);
	if (use_compression) {
		string cmd = "pigz - > '" + tarfn + "'";
		p = popen(cmd.c_str(), "w");
		fd = fileno(p);
		if (!p) return -1;
		if(tar_fdopen(&t, fd, charRootDir, NULL, O_RDONLY | O_LARGEFILE, 0644, TAR_GNU) != 0) {
			pclose(p);
			return -1;
		}
	}
	else {
		if (tar_open(&t, charTarFile, NULL, O_WRONLY | O_CREAT | O_LARGEFILE, 0644, TAR_GNU) == -1)
			return -1;
	}
	return 0;
}

int twrpTar::openTar(bool gzip) {
        char* charRootDir = (char*) tardir.c_str();
        char* charTarFile = (char*) tarfn.c_str();

	if (gzip) {
		LOGI("Opening as a gzip\n");
		string cmd = "pigz -d -c '" + tarfn + "'";
		FILE* pipe = popen(cmd.c_str(), "r");
		int fd = fileno(pipe);
		if (!pipe) return -1;
		if(tar_fdopen(&t, fd, charRootDir, NULL, O_RDONLY | O_LARGEFILE, 0644, TAR_GNU) != 0) {
			LOGI("tar_fdopen returned error\n");
			pclose(pipe);
			return -1;
		}
	}
	else {
		if (tar_open(&t, charTarFile, NULL, O_RDONLY | O_LARGEFILE, 0644, TAR_GNU) != 0) {
			LOGE("Unable to open tar archive '%s'\n", charTarFile);
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

	if (tar_append_eof(t) != 0) {
		LOGE("tar_append_eof(): %s\n", strerror(errno));
		tar_close(t);
		return -1;
	}
	if (tar_close(t) != 0) {
		LOGE("Unable to close tar archive: '%s'\n", tarfn.c_str());
		return -1;
	}
	if (use_compression || gzip) {
		LOGI("Closing popen and fd\n");
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
	pclose(p);
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
		LOGE("Unable to close tar file\n");
		return -1;
	}
        return 0;
}
