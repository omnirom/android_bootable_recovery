#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <unistd.h>
#include <vector>
#include <dirent.h>
#include <time.h>
#include <errno.h>

#include "twrp-functions.hpp"
#include "partitions.hpp"
#include "common.h"
#include "data.hpp"

/*  Checks md5 for a path
    Return values:
        -1 : MD5 does not exist
        0 : Failed
        1 : Success */
int TWFunc::Check_MD5(string File) {
	int ret;
	string Command, DirPath, MD5_File, Sline, Filename, MD5_File_Filename, OK;
	char line[255];
	size_t pos;

	MD5_File = File + ".md5";
	if (Path_Exists(MD5_File)) {
		DirPath = Get_Path(File);
		MD5_File = Get_Filename(MD5_File);
		Command = "cd '" + DirPath + "' && /sbin/busybox md5sum -c '" + MD5_File + "' > /tmp/md5output";
		system(Command.c_str());
		FILE * cs = fopen("/tmp/md5output", "r");
		if (cs == NULL) {
			LOGE("Unable to open md5 output file.\n");
			return 0;
		}

		fgets(line, sizeof(line), cs);
		fclose(cs);

		Sline = line;
		pos = Sline.find(":");
		if (pos != string::npos) {
			Filename = Get_Filename(File);
			MD5_File_Filename = Sline.substr(0, pos);
			OK = Sline.substr(pos + 2, Sline.size() - pos - 2);
			if (Filename == MD5_File_Filename && (OK == "OK" || OK == "OK\n")) {
				//MD5 is good, return 1
				ret = 1;
			} else {
				// MD5 is bad, return 0
				ret = 0;
			}
		} else {
			// MD5 is bad, return 0
			ret = 0;
		}
	} else {
		//No md5 file, return -1
		ret = -1;
	}

    return ret;
}

// Returns "file.name" from a full /path/to/file.name
string TWFunc::Get_Filename(string Path) {
	size_t pos = Path.find_last_of("/");
	if (pos != string::npos) {
		string Filename;
		Filename = Path.substr(pos + 1, Path.size() - pos - 1);
		return Filename;
	} else
		return Path;
}

// Returns "/path/to/" from a full /path/to/file.name
string TWFunc::Get_Path(string Path) {
	size_t pos = Path.find_last_of("/");
	if (pos != string::npos) {
		string Pathonly;
		Pathonly = Path.substr(0, pos + 1);
		return Pathonly;
	} else
		return Path;
}

// Returns "/path" from a full /path/to/file.name
string TWFunc::Get_Root_Path(string Path) {
	string Local_Path = Path;

	// Make sure that we have a leading slash
	if (Local_Path.substr(0, 1) != "/")
		Local_Path = "/" + Local_Path;

	// Trim the path to get the root path only
	size_t position = Local_Path.find("/", 2);
	if (position != string::npos) {
		Local_Path.resize(position);
	}
	return Local_Path;
}

void TWFunc::install_htc_dumlock(void) {
	int need_libs = 0;

	if (!PartitionManager.Mount_By_Path("/system", true))
		return;

	if (!PartitionManager.Mount_By_Path("/data", true))
		return;

	ui_print("Installing HTC Dumlock to system...\n");
	system("cp /res/htcd/htcdumlocksys /system/bin/htcdumlock && chmod 755 /system/bin/htcdumlock");
	if (!Path_Exists("/system/bin/flash_image")) {
		ui_print("Installing flash_image...\n");
		system("cp /res/htcd/flash_imagesys /system/bin/flash_image && chmod 755 /system/bin/flash_image");
		need_libs = 1;
	} else
		ui_print("flash_image is already installed, skipping...\n");
	if (!Path_Exists("/system/bin/dump_image")) {
		ui_print("Installing dump_image...\n");
		system("cp /res/htcd/dump_imagesys /system/bin/dump_image && chmod 755 /system/bin/dump_image");
		need_libs = 1;
	} else
		ui_print("dump_image is already installed, skipping...\n");
	if (need_libs) {
		ui_print("Installing libs needed for flash_image and dump_image...\n");
		system("cp /res/htcd/libbmlutils.so /system/lib && chmod 755 /system/lib/libbmlutils.so");
		system("cp /res/htcd/libflashutils.so /system/lib && chmod 755 /system/lib/libflashutils.so");
		system("cp /res/htcd/libmmcutils.so /system/lib && chmod 755 /system/lib/libmmcutils.so");
		system("cp /res/htcd/libmtdutils.so /system/lib && chmod 755 /system/lib/libmtdutils.so");
	}
	ui_print("Installing HTC Dumlock app...\n");
	mkdir("/data/app", 0777);
	system("rm /data/app/com.teamwin.htcdumlock*");
	system("cp /res/htcd/HTCDumlock.apk /data/app/com.teamwin.htcdumlock.apk");
	sync();
	ui_print("HTC Dumlock is installed.\n");
}

void TWFunc::htc_dumlock_restore_original_boot(void) {
	if (!PartitionManager.Mount_By_Path("/sdcard", true))
		return;

	ui_print("Restoring original boot...\n");
	system("htcdumlock restore");
	ui_print("Original boot restored.\n");
}

void TWFunc::htc_dumlock_reflash_recovery_to_boot(void) {
	if (!PartitionManager.Mount_By_Path("/sdcard", true))
		return;

	ui_print("Reflashing recovery to boot...\n");
	system("htcdumlock recovery noreboot");
	ui_print("Recovery is flashed to boot.\n");
}

int TWFunc::Recursive_Mkdir(string Path) {
	string pathCpy = Path;
	string wholePath;
	size_t pos = pathCpy.find("/", 2);

	while (pos != string::npos)
	{
		wholePath = pathCpy.substr(0, pos);
		if (mkdir(wholePath.c_str(), 0777) && errno != EEXIST) {
			LOGE("Unable to create folder: %s  (errno=%d)\n", wholePath.c_str(), errno);
			return false;
		}

		pos = pathCpy.find("/", pos + 1);
	}
	if (mkdir(wholePath.c_str(), 0777) && errno != EEXIST)
		return false;
	return true;
}

unsigned long long TWFunc::Get_Folder_Size(string Path, bool Display_Error) {
	DIR* d;
	struct dirent* de;
	struct stat st;
	char path2[1024], filename[1024];
	unsigned long long dusize = 0;

	// Make a copy of path in case the data in the pointer gets overwritten later
	strcpy(path2, Path.c_str());

	d = opendir(path2);
	if (d == NULL)
	{
		LOGE("error opening '%s'\n", path2);
		return 0;
	}

	while ((de = readdir(d)) != NULL)
	{
		if (de->d_type == DT_DIR && strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0)
		{
			strcpy(filename, path2);
			strcat(filename, "/");
			strcat(filename, de->d_name);
			dusize += Get_Folder_Size(filename, Display_Error);
		}
		else if (de->d_type == DT_REG)
		{
			strcpy(filename, path2);
			strcat(filename, "/");
			strcat(filename, de->d_name);
			stat(filename, &st);
			dusize += (unsigned long long)(st.st_size);
		}
	}
	closedir(d);

	return dusize;
}

bool TWFunc::Path_Exists(string Path) {
	// Check to see if the Path exists
	struct statfs st;

	if (statfs(Path.c_str(), &st) != 0)
		return false;
	else
		return true;
}

void TWFunc::GUI_Operation_Text(string Read_Value, string Default_Text) {
	string Display_Text;

	DataManager::GetValue(Read_Value, Display_Text);
	if (Display_Text.empty())
		Display_Text = Default_Text;

	DataManager::SetValue("tw_operation", Display_Text);
	DataManager::SetValue("tw_partition", "");
}

void TWFunc::GUI_Operation_Text(string Read_Value, string Partition_Name, string Default_Text) {
	string Display_Text;

	DataManager::GetValue(Read_Value, Display_Text);
	if (Display_Text.empty())
		Display_Text = Default_Text;

	DataManager::SetValue("tw_operation", Display_Text);
	DataManager::SetValue("tw_partition", Partition_Name);
}