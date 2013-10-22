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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vector>
#include <dirent.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <sys/types.h>
#include <sys/wait.h>
#ifdef ANDROID_RB_POWEROFF
	#include "cutils/android_reboot.h"
#endif
#include <iostream>
#include <fstream>
#include <sstream>
#include "twrp-functions.hpp"
#include "partitions.hpp"
#include "twcommon.h"
#include "data.hpp"
#include "variables.h"
#include "bootloader.h"
#ifndef TW_EXCLUDE_ENCRYPTED_BACKUPS
	#include "openaes/inc/oaes_lib.h"
#endif

extern "C" {
	#include "libcrecovery/common.h"
}

/* Execute a command */
int TWFunc::Exec_Cmd(const string& cmd, string &result) {
	FILE* exec;
	char buffer[130];
	int ret = 0;
	exec = __popen(cmd.c_str(), "r");
	if (!exec) return -1;
	while(!feof(exec)) {
		memset(&buffer, 0, sizeof(buffer));
		if (fgets(buffer, 128, exec) != NULL) {
			buffer[128] = '\n';
			buffer[129] = NULL;
			result += buffer;
		}
	}
	ret = __pclose(exec);
	return ret;
}

int TWFunc::Exec_Cmd(const string& cmd) {
	pid_t pid;
	int status;
	switch(pid = fork())
	{
		case -1:
			LOGERR("Exec_Cmd(): vfork failed!\n");
			return -1;
		case 0: // child
			execl("/sbin/sh", "sh", "-c", cmd.c_str(), NULL);
			_exit(127);
			break;
		default:
		{
			if (TWFunc::Wait_For_Child(pid, &status, cmd) != 0)
				return -1;
			else
				return 0;
		}
	}
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

	gui_print("Installing HTC Dumlock to system...\n");
	copy_file("/res/htcd/htcdumlocksys", "/system/bin/htcdumlock", 0755);
	if (!Path_Exists("/system/bin/flash_image")) {
		gui_print("Installing flash_image...\n");
		copy_file("/res/htcd/flash_imagesys", "/system/bin/flash_image", 0755);
		need_libs = 1;
	} else
		gui_print("flash_image is already installed, skipping...\n");
	if (!Path_Exists("/system/bin/dump_image")) {
		gui_print("Installing dump_image...\n");
		copy_file("/res/htcd/dump_imagesys", "/system/bin/dump_image", 0755);
		need_libs = 1;
	} else
		gui_print("dump_image is already installed, skipping...\n");
	if (need_libs) {
		gui_print("Installing libs needed for flash_image and dump_image...\n");
		copy_file("/res/htcd/libbmlutils.so", "/system/lib/libbmlutils.so", 0755);
		copy_file("/res/htcd/libflashutils.so", "/system/lib/libflashutils.so", 0755);
		copy_file("/res/htcd/libmmcutils.so", "/system/lib/libmmcutils.so", 0755);
		copy_file("/res/htcd/libmtdutils.so", "/system/lib/libmtdutils.so", 0755);
	}
	gui_print("Installing HTC Dumlock app...\n");
	mkdir("/data/app", 0777);
	unlink("/data/app/com.teamwin.htcdumlock*");
	copy_file("/res/htcd/HTCDumlock.apk", "/data/app/com.teamwin.htcdumlock.apk", 0777);
	sync();
	gui_print("HTC Dumlock is installed.\n");
}

void TWFunc::htc_dumlock_restore_original_boot(void) {
	if (!PartitionManager.Mount_By_Path("/sdcard", true))
		return;

	gui_print("Restoring original boot...\n");
	Exec_Cmd("htcdumlock restore");
	gui_print("Original boot restored.\n");
}

void TWFunc::htc_dumlock_reflash_recovery_to_boot(void) {
	if (!PartitionManager.Mount_By_Path("/sdcard", true))
		return;
	gui_print("Reflashing recovery to boot...\n");
	Exec_Cmd("htcdumlock recovery noreboot");
	gui_print("Recovery is flashed to boot.\n");
}

int TWFunc::Recursive_Mkdir(string Path) {
	string pathCpy = Path;
	string wholePath;
	size_t pos = pathCpy.find("/", 2);

	while (pos != string::npos)
	{
		wholePath = pathCpy.substr(0, pos);
		if (mkdir(wholePath.c_str(), 0777) && errno != EEXIST) {
			LOGERR("Unable to create folder: %s  (errno=%d)\n", wholePath.c_str(), errno);
			return false;
		}

		pos = pathCpy.find("/", pos + 1);
	}
	if (mkdir(wholePath.c_str(), 0777) && errno != EEXIST)
		return false;
	return true;
}

unsigned long long TWFunc::Get_Folder_Size(const string& Path, bool Display_Error) {
	DIR* d;
	struct dirent* de;
	struct stat st;
	unsigned long long dusize = 0;
	unsigned long long dutemp = 0;

	d = opendir(Path.c_str());
	if (d == NULL)
	{
		LOGERR("error opening '%s'\n", Path.c_str());
		LOGERR("error: %s\n", strerror(errno));
		return 0;
	}

	while ((de = readdir(d)) != NULL)
	{
		if (de->d_type == DT_DIR && strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0 && strcmp(de->d_name, "lost+found") != 0)
		{
			dutemp = Get_Folder_Size((Path + "/" + de->d_name), Display_Error);
			dusize += dutemp;
			dutemp = 0;
		}
		else if (de->d_type == DT_REG)
		{
			stat((Path + "/" + de->d_name).c_str(), &st);
			dusize += (unsigned long long)(st.st_size);
		}
	}
	closedir(d);
	return dusize;
}

bool TWFunc::Path_Exists(string Path) {
	// Check to see if the Path exists
	struct stat st;
	if (stat(Path.c_str(), &st) != 0)
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

unsigned long TWFunc::Get_File_Size(string Path) {
	struct stat st;

	if (stat(Path.c_str(), &st) != 0)
		return 0;
	return st.st_size;
}

void TWFunc::Copy_Log(string Source, string Destination) {
	PartitionManager.Mount_By_Path(Destination, false);
	FILE *destination_log = fopen(Destination.c_str(), "a");
	if (destination_log == NULL) {
		LOGERR("TWFunc::Copy_Log -- Can't open destination log file: '%s'\n", Destination.c_str());
	} else {
		FILE *source_log = fopen(Source.c_str(), "r");
		if (source_log != NULL) {
			fseek(source_log, Log_Offset, SEEK_SET);
			char buffer[4096];
			while (fgets(buffer, sizeof(buffer), source_log))
				fputs(buffer, destination_log); // Buffered write of log file
			Log_Offset = ftell(source_log);
			fflush(source_log);
			fclose(source_log);
		}
		fflush(destination_log);
		fclose(destination_log);
	}
}

void TWFunc::Update_Log_File(void) {
	// Copy logs to cache so the system can find out what happened.
	if (PartitionManager.Mount_By_Path("/cache", false)) {
		if (!TWFunc::Path_Exists("/cache/recovery/.")) {
			LOGINFO("Recreating /cache/recovery folder.\n");
			if (mkdir("/cache/recovery", S_IRWXU | S_IRWXG | S_IWGRP | S_IXGRP) != 0)
				LOGINFO("Unable to create /cache/recovery folder.\n");
		}
		Copy_Log(TMP_LOG_FILE, "/cache/recovery/log");
		copy_file("/cache/recovery/log", "/cache/recovery/last_log", 600);
		chown("/cache/recovery/log", 1000, 1000);
		chmod("/cache/recovery/log", 0600);
		chmod("/cache/recovery/last_log", 0640);
	} else {
		LOGINFO("Failed to mount /cache for TWFunc::Update_Log_File\n");
	}

	// Reset bootloader message
	TWPartition* Part = PartitionManager.Find_Partition_By_Path("/misc");
	if (Part != NULL) {
		struct bootloader_message boot;
		memset(&boot, 0, sizeof(boot));
		if (Part->Current_File_System == "mtd") {
			if (set_bootloader_message_mtd_name(&boot, Part->MTD_Name.c_str()) != 0)
				LOGERR("Unable to set MTD bootloader message.\n");
		} else if (Part->Current_File_System == "emmc") {
			if (set_bootloader_message_block_name(&boot, Part->Actual_Block_Device.c_str()) != 0)
				LOGERR("Unable to set emmc bootloader message.\n");
		} else {
			LOGERR("Unknown file system for /misc: '%s'\n", Part->Current_File_System.c_str());
		}
	}

	if (PartitionManager.Mount_By_Path("/cache", true)) {
		if (unlink("/cache/recovery/command") && errno != ENOENT) {
			LOGINFO("Can't unlink %s\n", "/cache/recovery/command");
		}
	}

	sync();
}

void TWFunc::Update_Intent_File(string Intent) {
	if (PartitionManager.Mount_By_Path("/cache", false) && !Intent.empty()) {
		TWFunc::write_file("/cache/recovery/intent", Intent);
	}
}

// reboot: Reboot the system. Return -1 on error, no return on success
int TWFunc::tw_reboot(RebootCommand command)
{
	// Always force a sync before we reboot
	sync();

	switch (command) {
		case rb_current:
		case rb_system:
			Update_Log_File();
			Update_Intent_File("s");
			sync();
			check_and_run_script("/sbin/rebootsystem.sh", "reboot system");
			return reboot(RB_AUTOBOOT);
		case rb_recovery:
			check_and_run_script("/sbin/rebootrecovery.sh", "reboot recovery");
			return __reboot(LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, LINUX_REBOOT_CMD_RESTART2, (void*) "recovery");
		case rb_bootloader:
			check_and_run_script("/sbin/rebootbootloader.sh", "reboot bootloader");
			return __reboot(LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, LINUX_REBOOT_CMD_RESTART2, (void*) "bootloader");
		case rb_poweroff:
			check_and_run_script("/sbin/poweroff.sh", "power off");
#ifdef ANDROID_RB_POWEROFF
			android_reboot(ANDROID_RB_POWEROFF, 0, 0);
#endif
			return reboot(RB_POWER_OFF);
		case rb_download:
			check_and_run_script("/sbin/rebootdownload.sh", "reboot download");
			return __reboot(LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, LINUX_REBOOT_CMD_RESTART2, (void*) "download");
		default:
			return -1;
	}
	return -1;
}

void TWFunc::check_and_run_script(const char* script_file, const char* display_name)
{
	// Check for and run startup script if script exists
	struct stat st;
	if (stat(script_file, &st) == 0) {
		gui_print("Running %s script...\n", display_name);
		chmod(script_file, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
		TWFunc::Exec_Cmd(script_file);
		gui_print("\nFinished running %s script.\n", display_name);
	}
}

int TWFunc::removeDir(const string path, bool skipParent) {
	DIR *d = opendir(path.c_str());
	int r = 0;
	string new_path;

	if (d == NULL) {
		LOGERR("Error opening '%s'\n", path.c_str());
		return -1;
	}

	if (d) {
		struct dirent *p;
		while (!r && (p = readdir(d))) {
			if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, ".."))
				continue;
			new_path = path + "/";
			new_path.append(p->d_name);
			if (p->d_type == DT_DIR) {
				r = removeDir(new_path, true);
				if (!r) {
					if (p->d_type == DT_DIR) 
						r = rmdir(new_path.c_str());
					else
						LOGINFO("Unable to removeDir '%s': %s\n", new_path.c_str(), strerror(errno));
				}
			} else if (p->d_type == DT_REG || p->d_type == DT_LNK || p->d_type == DT_FIFO || p->d_type == DT_SOCK) {
				r = unlink(new_path.c_str());
				if (r != 0) {
					LOGINFO("Unable to unlink '%s: %s'\n", new_path.c_str(), strerror(errno));
				}
			}
		}
		closedir(d);

		if (!r) { 
			if (skipParent)
				return 0;
			else
				r = rmdir(path.c_str());
		}
	}
	return r;
}

int TWFunc::copy_file(string src, string dst, int mode) {
	LOGINFO("Copying file %s to %s\n", src.c_str(), dst.c_str());
	ifstream srcfile(src.c_str(), ios::binary);
	ofstream dstfile(dst.c_str(), ios::binary);
	dstfile << srcfile.rdbuf();
	srcfile.close();
	dstfile.close();
	if (chmod(dst.c_str(), mode) != 0)
		return -1;
	return 0;
}

unsigned int TWFunc::Get_D_Type_From_Stat(string Path) {
	struct stat st;

	stat(Path.c_str(), &st);
	if (st.st_mode & S_IFDIR)
		return DT_DIR;
	else if (st.st_mode & S_IFBLK)
		return DT_BLK;
	else if (st.st_mode & S_IFCHR)
		return DT_CHR;
	else if (st.st_mode & S_IFIFO)
		return DT_FIFO;
	else if (st.st_mode & S_IFLNK)
		return DT_LNK;
	else if (st.st_mode & S_IFREG)
		return DT_REG;
	else if (st.st_mode & S_IFSOCK)
		return DT_SOCK;
	return DT_UNKNOWN;
}

int TWFunc::read_file(string fn, string& results) {
	ifstream file;
	file.open(fn.c_str(), ios::in);

	if (file.is_open()) {
		file >> results;
		file.close();
		return 0;
	}

	LOGINFO("Cannot find file %s\n", fn.c_str());
	return -1;
}

int TWFunc::read_file(string fn, vector<string>& results) {
	ifstream file;
	string line;
	file.open(fn.c_str(), ios::in);
	if (file.is_open()) {
		while (getline(file, line))
			results.push_back(line);
		file.close();
		return 0;
	}
	LOGINFO("Cannot find file %s\n", fn.c_str());
	return -1;
}

int TWFunc::write_file(string fn, string& line) {
	FILE *file;
	file = fopen(fn.c_str(), "w");
	if (file != NULL) {
		fwrite(line.c_str(), line.size(), 1, file);
		fclose(file);
		return 0;
	}
	LOGINFO("Cannot find file %s\n", fn.c_str());
	return -1;
}

vector<string> TWFunc::split_string(const string &in, char del, bool skip_empty) {
	vector<string> res;

	if (in.empty() || del == '\0')
		return res;

	string field;
	istringstream f(in);
	if (del == '\n') {
		while(getline(f, field)) {
			if (field.empty() && skip_empty)
				continue;
			res.push_back(field);
		}
	} else {
		while(getline(f, field, del)) {
			if (field.empty() && skip_empty)
				continue;
			res.push_back(field);
		}
	}
	return res;
}

timespec TWFunc::timespec_diff(timespec& start, timespec& end)
{
	timespec temp;
	if ((end.tv_nsec-start.tv_nsec)<0) {
		temp.tv_sec = end.tv_sec-start.tv_sec-1;
		temp.tv_nsec = 1000000000+end.tv_nsec-start.tv_nsec;
	} else {
		temp.tv_sec = end.tv_sec-start.tv_sec;
		temp.tv_nsec = end.tv_nsec-start.tv_nsec;
	}
	return temp;
}

int TWFunc::drop_caches(void) {
	string file = "/proc/sys/vm/drop_caches";
	string value = "3";
	if (write_file(file, value) != 0)
		return -1;
	return 0;
}

int TWFunc::Check_su_Perms(void) {
	struct stat st;
	int ret = 0;

	if (!PartitionManager.Mount_By_Path("/system", false))
		return 0;

	// Check to ensure that perms are 6755 for all 3 file locations
	if (stat("/system/bin/su", &st) == 0) {
		if ((st.st_mode & (S_ISUID | S_ISGID | S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)) != (S_ISUID | S_ISGID | S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) || st.st_uid != 0 || st.st_gid != 0) {
			ret = 1;
		}
	}
	if (stat("/system/xbin/su", &st) == 0) {
		if ((st.st_mode & (S_ISUID | S_ISGID | S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)) != (S_ISUID | S_ISGID | S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) || st.st_uid != 0 || st.st_gid != 0) {
			ret += 2;
		}
	}
	if (stat("/system/bin/.ext/.su", &st) == 0) {
		if ((st.st_mode & (S_ISUID | S_ISGID | S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)) != (S_ISUID | S_ISGID | S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) || st.st_uid != 0 || st.st_gid != 0) {
			ret += 4;
		}
	}
	return ret;
}

bool TWFunc::Fix_su_Perms(void) {
	if (!PartitionManager.Mount_By_Path("/system", true))
		return false;

	string file = "/system/bin/su";
	if (TWFunc::Path_Exists(file)) {
		if (chown(file.c_str(), 0, 0) != 0) {
			LOGERR("Failed to chown '%s'\n", file.c_str());
			return false;
		}
		if (tw_chmod(file, "6755") != 0) {
			LOGERR("Failed to chmod '%s'\n", file.c_str());
			return false;
		}
	}
	file = "/system/xbin/su";
	if (TWFunc::Path_Exists(file)) {
		if (chown(file.c_str(), 0, 0) != 0) {
			LOGERR("Failed to chown '%s'\n", file.c_str());
			return false;
		}
		if (tw_chmod(file, "6755") != 0) {
			LOGERR("Failed to chmod '%s'\n", file.c_str());
			return false;
		}
	}
	file = "/system/xbin/daemonsu";
	if (TWFunc::Path_Exists(file)) {
		if (chown(file.c_str(), 0, 0) != 0) {
			LOGERR("Failed to chown '%s'\n", file.c_str());
			return false;
		}
		if (tw_chmod(file, "6755") != 0) {
			LOGERR("Failed to chmod '%s'\n", file.c_str());
			return false;
		}
	}
	file = "/system/bin/.ext/.su";
	if (TWFunc::Path_Exists(file)) {
		if (chown(file.c_str(), 0, 0) != 0) {
			LOGERR("Failed to chown '%s'\n", file.c_str());
			return false;
		}
		if (tw_chmod(file, "6755") != 0) {
			LOGERR("Failed to chmod '%s'\n", file.c_str());
			return false;
		}
	}
	file = "/system/etc/install-recovery.sh";
	if (TWFunc::Path_Exists(file)) {
		if (chown(file.c_str(), 0, 0) != 0) {
			LOGERR("Failed to chown '%s'\n", file.c_str());
			return false;
		}
		if (tw_chmod(file, "0755") != 0) {
			LOGERR("Failed to chmod '%s'\n", file.c_str());
			return false;
		}
	}
	file = "/system/etc/init.d/99SuperSUDaemon";
	if (TWFunc::Path_Exists(file)) {
		if (chown(file.c_str(), 0, 0) != 0) {
			LOGERR("Failed to chown '%s'\n", file.c_str());
			return false;
		}
		if (tw_chmod(file, "0755") != 0) {
			LOGERR("Failed to chmod '%s'\n", file.c_str());
			return false;
		}
	}
	file = "/system/app/Superuser.apk";
	if (TWFunc::Path_Exists(file)) {
		if (chown(file.c_str(), 0, 0) != 0) {
			LOGERR("Failed to chown '%s'\n", file.c_str());
			return false;
		}
		if (tw_chmod(file, "0644") != 0) {
			LOGERR("Failed to chmod '%s'\n", file.c_str());
			return false;
		}
	}
	sync();
	if (!PartitionManager.UnMount_By_Path("/system", true))
		return false;
	return true;
}

int TWFunc::tw_chmod(const string& fn, const string& mode) {
	long mask = 0;
	std::string::size_type n = mode.length();
	int cls = 0;

	if(n == 3)
		++cls;
	else if(n != 4)
	{
		LOGERR("TWFunc::tw_chmod used with %u long mode string (should be 3 or 4)!\n", mode.length());
		return -1;
	}

	for (n = 0; n < mode.length(); ++n, ++cls) {
		if (cls == 0) {
			if (mode[n] == '0')
				continue;
			else if (mode[n] == '1')
				mask |= S_ISVTX;
			else if (mode[n] == '2')
				mask |= S_ISGID;
			else if (mode[n] == '4')
				mask |= S_ISUID;
			else if (mode[n] == '5') {
				mask |= S_ISVTX;
				mask |= S_ISUID;
			}
			else if (mode[n] == '6') {
				mask |= S_ISGID;
				mask |= S_ISUID;
			}
			else if (mode[n] == '7') {
				mask |= S_ISVTX;
				mask |= S_ISGID;
				mask |= S_ISUID;
			}
		}
		else if (cls == 1) {
			if (mode[n] == '7') {
				mask |= S_IRWXU;
			}
			else if (mode[n] == '6') {
				mask |= S_IRUSR;
				mask |= S_IWUSR;
			}
			else if (mode[n] == '5') {
				mask |= S_IRUSR;
				mask |= S_IXUSR;
			}
			else if (mode[n] == '4')
				mask |= S_IRUSR;
			else if (mode[n] == '3') {
				mask |= S_IWUSR;
				mask |= S_IRUSR;
			}
			else if (mode[n] == '2')
				mask |= S_IWUSR;
			else if (mode[n] == '1')
				mask |= S_IXUSR;
		}
		else if (cls == 2) {
			if (mode[n] == '7') {
				mask |= S_IRWXG;
			}
			else if (mode[n] == '6') {
				mask |= S_IRGRP;
				mask |= S_IWGRP;
			}
			else if (mode[n] == '5') {
				mask |= S_IRGRP;
				mask |= S_IXGRP;
			}
			else if (mode[n] == '4')
				mask |= S_IRGRP;
			else if (mode[n] == '3') {
				mask |= S_IWGRP;
				mask |= S_IXGRP;
			}
			else if (mode[n] == '2')
				mask |= S_IWGRP;
			else if (mode[n] == '1')
				mask |= S_IXGRP;
		}
		else if (cls == 3) {
			if (mode[n] == '7') {
				mask |= S_IRWXO;
			}
			else if (mode[n] == '6') {
				mask |= S_IROTH;
				mask |= S_IWOTH;
			}
			else if (mode[n] == '5') {
				mask |= S_IROTH;
				mask |= S_IXOTH;
			}
			else if (mode[n] == '4')
				mask |= S_IROTH;
			else if (mode[n] == '3') {
				mask |= S_IWOTH;
				mask |= S_IXOTH;
			}
			else if (mode[n] == '2')
				mask |= S_IWOTH;
			else if (mode[n] == '1')
				mask |= S_IXOTH;
		}
	}

	if (chmod(fn.c_str(), mask) != 0) {
		LOGERR("Unable to chmod '%s' %l\n", fn.c_str(), mask);
		return -1;
	}

	return 0;
}

bool TWFunc::Install_SuperSU(void) {
	if (!PartitionManager.Mount_By_Path("/system", true))
		return false;

	TWFunc::Exec_Cmd("/sbin/chattr -i /system/xbin/su");
	if (copy_file("/supersu/su", "/system/xbin/su", 0755) != 0) {
		LOGERR("Failed to copy su binary to /system/bin\n");
		return false;
	}
	if (!Path_Exists("/system/bin/.ext")) {
		mkdir("/system/bin/.ext", 0777);
	}
	TWFunc::Exec_Cmd("/sbin/chattr -i /system/bin/.ext/su");
	if (copy_file("/supersu/su", "/system/bin/.ext/su", 0755) != 0) {
		LOGERR("Failed to copy su binary to /system/bin/.ext/su\n");
		return false;
	}
	TWFunc::Exec_Cmd("/sbin/chattr -i /system/xbin/daemonsu");
	if (copy_file("/supersu/su", "/system/xbin/daemonsu", 0755) != 0) {
		LOGERR("Failed to copy su binary to /system/xbin/daemonsu\n");
		return false;
	}
	if (Path_Exists("/system/etc/init.d")) {
		TWFunc::Exec_Cmd("/sbin/chattr -i /system/etc/init.d/99SuperSUDaemon");
		if (copy_file("/supersu/99SuperSUDaemon", "/system/etc/init.d/99SuperSUDaemon", 0755) != 0) {
			LOGERR("Failed to copy 99SuperSUDaemon to /system/etc/init.d/99SuperSUDaemon\n");
			return false;
		}
	} else {
		TWFunc::Exec_Cmd("/sbin/chattr -i /system/etc/install-recovery.sh");
		if (copy_file("/supersu/install-recovery.sh", "/system/etc/install-recovery.sh", 0755) != 0) {
			LOGERR("Failed to copy install-recovery.sh to /system/etc/install-recovery.sh\n");
			return false;
		}
	}
	if (copy_file("/supersu/Superuser.apk", "/system/app/Superuser.apk", 0644) != 0) {
		LOGERR("Failed to copy Superuser app to /system/app\n");
		return false;
	}
	if (!Fix_su_Perms())
		return false;
	return true;
}

int TWFunc::Get_File_Type(string fn) {
	string::size_type i = 0;
	int firstbyte = 0, secondbyte = 0;
	char header[3];

	ifstream f;
	f.open(fn.c_str(), ios::in | ios::binary);
	f.get(header, 3);
	f.close();
	firstbyte = header[i] & 0xff;
	secondbyte = header[++i] & 0xff;

	if (firstbyte == 0x1f && secondbyte == 0x8b)
		return 1; // Compressed
	else if (firstbyte == 0x4f && secondbyte == 0x41)
		return 2; // Encrypted
	else
		return 0; // Unknown

	return 0;
}

int TWFunc::Try_Decrypting_File(string fn, string password) {
#ifndef TW_EXCLUDE_ENCRYPTED_BACKUPS
	OAES_CTX * ctx = NULL;
	uint8_t _key_data[32] = "";
	FILE *f;
	uint8_t buffer[4096];
	uint8_t *buffer_out = NULL;
	uint8_t *ptr = NULL;
	size_t read_len = 0, out_len = 0;
	int firstbyte = 0, secondbyte = 0, key_len;
	size_t _j = 0;
	size_t _key_data_len = 0;

	// mostly kanged from OpenAES oaes.c
	for( _j = 0; _j < 32; _j++ )
		_key_data[_j] = _j + 1;
	_key_data_len = password.size();
	if( 16 >= _key_data_len )
		_key_data_len = 16;
	else if( 24 >= _key_data_len )
		_key_data_len = 24;
	else
	_key_data_len = 32;
	memcpy(_key_data, password.c_str(), password.size());

	ctx = oaes_alloc();
	if (ctx == NULL) {
		LOGERR("Failed to allocate OAES\n");
		return -1;
	}

	oaes_key_import_data(ctx, _key_data, _key_data_len);

	f = fopen(fn.c_str(), "rb");
	if (f == NULL) {
		LOGERR("Failed to open '%s' to try decrypt\n", fn.c_str());
		return -1;
	}
	read_len = fread(buffer, sizeof(uint8_t), 4096, f);
	if (read_len <= 0) {
		LOGERR("Read size during try decrypt failed\n");
		fclose(f);
		return -1;
	}
	if (oaes_decrypt(ctx, buffer, read_len, NULL, &out_len) != OAES_RET_SUCCESS) {
		LOGERR("Error: Failed to retrieve required buffer size for trying decryption.\n");
		fclose(f);
		return -1;
	}
	buffer_out = (uint8_t *) calloc(out_len, sizeof(char));
	if (buffer_out == NULL) {
		LOGERR("Failed to allocate output buffer for try decrypt.\n");
		fclose(f);
		return -1;
	}
	if (oaes_decrypt(ctx, buffer, read_len, buffer_out, &out_len) != OAES_RET_SUCCESS) {
		LOGERR("Failed to decrypt file '%s'\n", fn.c_str());
		fclose(f);
		free(buffer_out);
		return 0;
	}
	fclose(f);
	if (out_len < 2) {
		LOGINFO("Successfully decrypted '%s' but read length %i too small.\n", fn.c_str(), out_len);
		free(buffer_out);
		return 1; // Decrypted successfully
	}
	ptr = buffer_out;
	firstbyte = *ptr & 0xff;
	ptr++;
	secondbyte = *ptr & 0xff;
	if (firstbyte == 0x1f && secondbyte == 0x8b) {
		LOGINFO("Successfully decrypted '%s' and file is compressed.\n", fn.c_str());
		free(buffer_out);
		return 3; // Compressed
	}
	if (out_len >= 262) {
		ptr = buffer_out + 257;
		if (strncmp((char*)ptr, "ustar", 5) == 0) {
			LOGINFO("Successfully decrypted '%s' and file is tar format.\n", fn.c_str());
			free(buffer_out);
			return 2; // Tar
		}
	}
	free(buffer_out);
	LOGINFO("No errors decrypting '%s' but no known file format.\n", fn.c_str());
	return 1; // Decrypted successfully
#else
	LOGERR("Encrypted backup support not included.\n");
	return -1;
#endif
}

bool TWFunc::Try_Decrypting_Backup(string Restore_Path, string Password) {
	DIR* d;

	string Filename;
	Restore_Path += "/";
	d = opendir(Restore_Path.c_str());
	if (d == NULL) {
		LOGERR("Error opening '%s'\n", Restore_Path.c_str());
		return false;
	}

	struct dirent* de;
	while ((de = readdir(d)) != NULL) {
		Filename = Restore_Path;
		Filename += de->d_name;
		if (TWFunc::Get_File_Type(Filename) == 2) {
			if (TWFunc::Try_Decrypting_File(Filename, Password) < 2) {
				DataManager::SetValue("tw_restore_password", ""); // Clear the bad password
				DataManager::SetValue("tw_restore_display", "");  // Also clear the display mask
				closedir(d);
				return false;
			}
		}
	}
	closedir(d);
	return true;
}

int TWFunc::Wait_For_Child(pid_t pid, int *status, string Child_Name) {
	pid_t rc_pid;

	rc_pid = waitpid(pid, status, 0);
	if (rc_pid > 0) {
		if (WEXITSTATUS(*status) == 0)
			LOGINFO("%s process ended with RC=%d\n", Child_Name.c_str(), WEXITSTATUS(*status)); // Success
		else if (WIFSIGNALED(*status)) {
			LOGINFO("%s process ended with signal: %d\n", Child_Name.c_str(), WTERMSIG(*status)); // Seg fault or some other non-graceful termination
			return -1;
		} else if (WEXITSTATUS(*status) != 0) {
			LOGINFO("%s process ended with ERROR=%d\n", Child_Name.c_str(), WEXITSTATUS(*status)); // Graceful exit, but there was an error
			return -1;
		}
	} else { // no PID returned
		if (errno == ECHILD)
			LOGINFO("%s no child process exist\n", Child_Name.c_str());
		else {
			LOGINFO("%s Unexpected error\n", Child_Name.c_str());
			return -1;
		}
	}
	return 0;
}

string TWFunc::Get_Current_Date() {
	string Current_Date;
	time_t seconds = time(0);
	struct tm *t = localtime(&seconds);
	char timestamp[255];
	sprintf(timestamp,"%04d-%02d-%02d--%02d-%02d-%02d",t->tm_year+1900,t->tm_mon+1,t->tm_mday,t->tm_hour,t->tm_min,t->tm_sec);
	Current_Date = timestamp;
	return Current_Date;
}

void TWFunc::Auto_Generate_Backup_Name() {
	bool mount_state = PartitionManager.Is_Mounted_By_Path("/system");
	std::vector<string> buildprop;
	if (!PartitionManager.Mount_By_Path("/system", true)) {
		DataManager::SetValue(TW_BACKUP_NAME, Get_Current_Date());
		return;
	}
	if (TWFunc::read_file("/system/build.prop", buildprop) != 0) {
		LOGINFO("Unable to open /system/build.prop for getting backup name.\n");
		DataManager::SetValue(TW_BACKUP_NAME, Get_Current_Date());
		if (!mount_state)
			PartitionManager.UnMount_By_Path("/system", false);
		return;
	}
	int line_count = buildprop.size();
	int index;
	size_t start_pos = 0, end_pos;
	string propname, propvalue;
	for (index = 0; index < line_count; index++) {
		end_pos = buildprop.at(index).find("=", start_pos);
		propname = buildprop.at(index).substr(start_pos, end_pos);
		if (propname == "ro.build.display.id") {
			propvalue = buildprop.at(index).substr(end_pos + 1, buildprop.at(index).size());
			string Backup_Name = Get_Current_Date();
			Backup_Name += " " + propvalue;
			if (Backup_Name.size() > MAX_BACKUP_NAME_LEN)
				Backup_Name.resize(MAX_BACKUP_NAME_LEN);
			// Trailing spaces cause problems on some file systems, so remove them
			string space_check, space = " ";
			space_check = Backup_Name.substr(Backup_Name.size() - 1, 1);
			while (space_check == space) {
				Backup_Name.resize(Backup_Name.size() - 1);
				space_check = Backup_Name.substr(Backup_Name.size() - 1, 1);
			}
			DataManager::SetValue(TW_BACKUP_NAME, Backup_Name);
			break;
		}
	}
	if (propvalue.empty()) {
		LOGINFO("ro.build.display.id not found in build.prop\n");
		DataManager::SetValue(TW_BACKUP_NAME, Get_Current_Date());
	}
	if (!mount_state)
		PartitionManager.UnMount_By_Path("/system", false);
}
