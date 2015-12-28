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
#include <string>
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
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include "twrp-functions.hpp"
#include "twcommon.h"
#ifndef BUILD_TWRPTAR_MAIN
#include "data.hpp"
#include "partitions.hpp"
#include "variables.h"
#include "bootloader.h"
#include "cutils/properties.h"
#include "cutils/android_reboot.h"
#include "gui/gui.hpp"
#include <sys/reboot.h>
#endif // ndef BUILD_TWRPTAR_MAIN
#ifndef TW_EXCLUDE_ENCRYPTED_BACKUPS
	#include "openaes/inc/oaes_lib.h"
#endif

extern "C" {
	#include "libcrecovery/common.h"
	#include "set_metadata.h"
}

/* Execute a command */
int TWFunc::Exec_Cmd(const string& cmd, string &result) {
	FILE* exec;
	char buffer[130];
	int ret = 0;
	exec = __popen(cmd.c_str(), "r");
	if (!exec) return -1;
	while(!feof(exec)) {
		if (fgets(buffer, 128, exec) != NULL) {
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
			LOGERR("Exec_Cmd(): vfork failed: %d!\n", errno);
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

int TWFunc::Wait_For_Child(pid_t pid, int *status, string Child_Name) {
	pid_t rc_pid;

	rc_pid = waitpid(pid, status, 0);
	if (rc_pid > 0) {
		if (WIFSIGNALED(*status)) {
			gui_msg(Msg(msg::kError, "pid_signal={1} process ended with signal: {2}")(Child_Name)(WTERMSIG(*status))); // Seg fault or some other non-graceful termination
			return -1;
		} else if (WEXITSTATUS(*status) == 0) {
			LOGINFO("%s process ended with RC=%d\n", Child_Name.c_str(), WEXITSTATUS(*status)); // Success
		} else {
			gui_msg(Msg(msg::kError, "pid_error={1} process ended with ERROR: {2}")(Child_Name)(WEXITSTATUS(*status))); // Graceful exit, but there was an error
			return -1;
		}
	} else { // no PID returned
		if (errno == ECHILD)
			LOGERR("%s no child process exist\n", Child_Name.c_str());
		else {
			LOGERR("%s Unexpected error %d\n", Child_Name.c_str(), errno);
			return -1;
		}
	}
	return 0;
}

bool TWFunc::Path_Exists(string Path) {
	// Check to see if the Path exists
	struct stat st;
	if (stat(Path.c_str(), &st) != 0)
		return false;
	else
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
		LOGERR("Failed to open '%s' to try decrypt: %s\n", fn.c_str(), strerror(errno));
		oaes_free(&ctx);
		return -1;
	}
	read_len = fread(buffer, sizeof(uint8_t), 4096, f);
	if (read_len <= 0) {
		LOGERR("Read size during try decrypt failed: %s\n", strerror(errno));
		fclose(f);
		oaes_free(&ctx);
		return -1;
	}
	if (oaes_decrypt(ctx, buffer, read_len, NULL, &out_len) != OAES_RET_SUCCESS) {
		LOGERR("Error: Failed to retrieve required buffer size for trying decryption.\n");
		fclose(f);
		oaes_free(&ctx);
		return -1;
	}
	buffer_out = (uint8_t *) calloc(out_len, sizeof(char));
	if (buffer_out == NULL) {
		LOGERR("Failed to allocate output buffer for try decrypt.\n");
		fclose(f);
		oaes_free(&ctx);
		return -1;
	}
	if (oaes_decrypt(ctx, buffer, read_len, buffer_out, &out_len) != OAES_RET_SUCCESS) {
		LOGERR("Failed to decrypt file '%s'\n", fn.c_str());
		fclose(f);
		free(buffer_out);
		oaes_free(&ctx);
		return 0;
	}
	fclose(f);
	oaes_free(&ctx);
	if (out_len < 2) {
		LOGINFO("Successfully decrypted '%s' but read length too small.\n", fn.c_str());
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

unsigned long TWFunc::Get_File_Size(string Path) {
	struct stat st;

	if (stat(Path.c_str(), &st) != 0)
		return 0;
	return st.st_size;
}

std::string TWFunc::Remove_Trailing_Slashes(const std::string& path, bool leaveLast)
{
	std::string res;
	size_t last_idx = 0, idx = 0;

	while(last_idx != std::string::npos)
	{
		if(last_idx != 0)
			res += '/';

		idx = path.find_first_of('/', last_idx);
		if(idx == std::string::npos) {
			res += path.substr(last_idx, idx);
			break;
		}

		res += path.substr(last_idx, idx-last_idx);
		last_idx = path.find_first_not_of('/', idx);
	}

	if(leaveLast)
		res += '/';
	return res;
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

#ifndef BUILD_TWRPTAR_MAIN

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

	gui_msg("install_dumlock=Installing HTC Dumlock to system...");
	copy_file(TWHTCD_PATH "htcdumlocksys", "/system/bin/htcdumlock", 0755);
	if (!Path_Exists("/system/bin/flash_image")) {
		LOGINFO("Installing flash_image...\n");
		copy_file(TWHTCD_PATH "flash_imagesys", "/system/bin/flash_image", 0755);
		need_libs = 1;
	} else
		LOGINFO("flash_image is already installed, skipping...\n");
	if (!Path_Exists("/system/bin/dump_image")) {
		LOGINFO("Installing dump_image...\n");
		copy_file(TWHTCD_PATH "dump_imagesys", "/system/bin/dump_image", 0755);
		need_libs = 1;
	} else
		LOGINFO("dump_image is already installed, skipping...\n");
	if (need_libs) {
		LOGINFO("Installing libs needed for flash_image and dump_image...\n");
		copy_file(TWHTCD_PATH "libbmlutils.so", "/system/lib/libbmlutils.so", 0644);
		copy_file(TWHTCD_PATH "libflashutils.so", "/system/lib/libflashutils.so", 0644);
		copy_file(TWHTCD_PATH "libmmcutils.so", "/system/lib/libmmcutils.so", 0644);
		copy_file(TWHTCD_PATH "libmtdutils.so", "/system/lib/libmtdutils.so", 0644);
	}
	LOGINFO("Installing HTC Dumlock app...\n");
	mkdir("/data/app", 0777);
	unlink("/data/app/com.teamwin.htcdumlock*");
	copy_file(TWHTCD_PATH "HTCDumlock.apk", "/data/app/com.teamwin.htcdumlock.apk", 0777);
	sync();
	gui_msg("done=Done.");
}

void TWFunc::htc_dumlock_restore_original_boot(void) {
	if (!PartitionManager.Mount_By_Path("/sdcard", true))
		return;

	gui_msg("dumlock_restore=Restoring original boot...");
	Exec_Cmd("htcdumlock restore");
	gui_msg("done=Done.");
}

void TWFunc::htc_dumlock_reflash_recovery_to_boot(void) {
	if (!PartitionManager.Mount_By_Path("/sdcard", true))
		return;
	gui_msg("dumlock_reflash=Reflashing recovery to boot...");
	Exec_Cmd("htcdumlock recovery noreboot");
	gui_msg("done=Done.");
}

int TWFunc::Recursive_Mkdir(string Path) {
	std::vector<std::string> parts = Split_String(Path, "/", true);
	std::string cur_path;
	for (size_t i = 0; i < parts.size(); ++i) {
		cur_path += "/" + parts[i];
		if (!TWFunc::Path_Exists(cur_path)) {
			if (mkdir(cur_path.c_str(), 0777)) {
				gui_msg(Msg(msg::kError, "create_folder_strerr=Can not create '{1}' folder ({2}).")(cur_path)(strerror(errno)));
				return false;
			} else {
				tw_set_default_metadata(cur_path.c_str());
			}
		}
	}
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
		if (set_bootloader_message(&boot) != 0)
			LOGERR("Unable to set bootloader message.\n");
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
	Update_Log_File();

	switch (command) {
		case rb_current:
		case rb_system:
			Update_Intent_File("s");
			sync();
			check_and_run_script("/sbin/rebootsystem.sh", "reboot system");
#ifdef ANDROID_RB_PROPERTY
			return property_set(ANDROID_RB_PROPERTY, "reboot,");
#elif defined(ANDROID_RB_RESTART)
			return android_reboot(ANDROID_RB_RESTART, 0, 0);
#else
			return reboot(RB_AUTOBOOT);
#endif
		case rb_recovery:
			check_and_run_script("/sbin/rebootrecovery.sh", "reboot recovery");
#ifdef ANDROID_RB_PROPERTY
			return property_set(ANDROID_RB_PROPERTY, "reboot,recovery");
#else
			return __reboot(LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, LINUX_REBOOT_CMD_RESTART2, (void*) "recovery");
#endif
		case rb_bootloader:
			check_and_run_script("/sbin/rebootbootloader.sh", "reboot bootloader");
#ifdef ANDROID_RB_PROPERTY
			return property_set(ANDROID_RB_PROPERTY, "reboot,bootloader");
#else
			return __reboot(LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, LINUX_REBOOT_CMD_RESTART2, (void*) "bootloader");
#endif
		case rb_poweroff:
			check_and_run_script("/sbin/poweroff.sh", "power off");
#ifdef ANDROID_RB_PROPERTY
			return property_set(ANDROID_RB_PROPERTY, "shutdown,");
#elif defined(ANDROID_RB_POWEROFF)
			return android_reboot(ANDROID_RB_POWEROFF, 0, 0);
#else
			return reboot(RB_POWER_OFF);
#endif
		case rb_download:
			check_and_run_script("/sbin/rebootdownload.sh", "reboot download");
#ifdef ANDROID_RB_PROPERTY
			return property_set(ANDROID_RB_PROPERTY, "reboot,download");
#else
			return __reboot(LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, LINUX_REBOOT_CMD_RESTART2, (void*) "download");
#endif
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
		gui_msg(Msg("run_script=Running {1} script...")(display_name));
		chmod(script_file, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
		TWFunc::Exec_Cmd(script_file);
		gui_msg("done=Done.");
	}
}

int TWFunc::removeDir(const string path, bool skipParent) {
	DIR *d = opendir(path.c_str());
	int r = 0;
	string new_path;

	if (d == NULL) {
		gui_msg(Msg(msg::kError, "error_opening_strerr=Error opening: '{1}' ({2})")(path)(strerror(errno)));
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

int TWFunc::read_file(string fn, uint64_t& results) {
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

int32_t TWFunc::timespec_diff_ms(timespec& start, timespec& end)
{
	return ((end.tv_sec * 1000) + end.tv_nsec/1000000) -
			((start.tv_sec * 1000) + start.tv_nsec/1000000);
}

bool TWFunc::Install_SuperSU(void) {
	if (!PartitionManager.Mount_By_Path("/system", true))
		return false;

	check_and_run_script("/supersu/install-supersu.sh", "SuperSU");
	return true;
}

bool TWFunc::Try_Decrypting_Backup(string Restore_Path, string Password) {
	DIR* d;

	string Filename;
	Restore_Path += "/";
	d = opendir(Restore_Path.c_str());
	if (d == NULL) {
		gui_msg(Msg(msg::kError, "error_opening_strerr=Error opening: '{1}' ({2})")(Restore_Path)(strerror(errno)));
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

string TWFunc::Get_Current_Date() {
	string Current_Date;
	time_t seconds = time(0);
	struct tm *t = localtime(&seconds);
	char timestamp[255];
	sprintf(timestamp,"%04d-%02d-%02d--%02d-%02d-%02d",t->tm_year+1900,t->tm_mon+1,t->tm_mday,t->tm_hour,t->tm_min,t->tm_sec);
	Current_Date = timestamp;
	return Current_Date;
}

string TWFunc::System_Property_Get(string Prop_Name) {
	bool mount_state = PartitionManager.Is_Mounted_By_Path("/system");
	std::vector<string> buildprop;
	string propvalue;
	if (!PartitionManager.Mount_By_Path("/system", true))
		return propvalue;
	if (TWFunc::read_file("/system/build.prop", buildprop) != 0) {
		LOGINFO("Unable to open /system/build.prop for getting '%s'.\n", Prop_Name.c_str());
		DataManager::SetValue(TW_BACKUP_NAME, Get_Current_Date());
		if (!mount_state)
			PartitionManager.UnMount_By_Path("/system", false);
		return propvalue;
	}
	int line_count = buildprop.size();
	int index;
	size_t start_pos = 0, end_pos;
	string propname;
	for (index = 0; index < line_count; index++) {
		end_pos = buildprop.at(index).find("=", start_pos);
		propname = buildprop.at(index).substr(start_pos, end_pos);
		if (propname == Prop_Name) {
			propvalue = buildprop.at(index).substr(end_pos + 1, buildprop.at(index).size());
			if (!mount_state)
				PartitionManager.UnMount_By_Path("/system", false);
			return propvalue;
		}
	}
	if (!mount_state)
		PartitionManager.UnMount_By_Path("/system", false);
	return propvalue;
}

void TWFunc::Auto_Generate_Backup_Name() {
	string propvalue = System_Property_Get("ro.build.display.id");
	if (propvalue.empty()) {
		DataManager::SetValue(TW_BACKUP_NAME, Get_Current_Date());
		return;
	}
	string Backup_Name = Get_Current_Date();
	Backup_Name += "_" + propvalue;
	if (Backup_Name.size() > MAX_BACKUP_NAME_LEN)
		Backup_Name.resize(MAX_BACKUP_NAME_LEN);
	// Trailing spaces cause problems on some file systems, so remove them
	string space_check, space = " ";
	space_check = Backup_Name.substr(Backup_Name.size() - 1, 1);
	while (space_check == space) {
		Backup_Name.resize(Backup_Name.size() - 1);
		space_check = Backup_Name.substr(Backup_Name.size() - 1, 1);
	}
	replace(Backup_Name.begin(), Backup_Name.end(), ' ', '_');
	DataManager::SetValue(TW_BACKUP_NAME, Backup_Name);
	if (PartitionManager.Check_Backup_Name(false) != 0) {
		LOGINFO("Auto generated backup name '%s' contains invalid characters, using date instead.\n", Backup_Name.c_str());
		DataManager::SetValue(TW_BACKUP_NAME, Get_Current_Date());
	}
}

void TWFunc::Fixup_Time_On_Boot()
{
#ifdef QCOM_RTC_FIX

	LOGINFO("TWFunc::Fixup_Time: Pre-fix date and time: %s\n", TWFunc::Get_Current_Date().c_str());

	struct timeval tv;
	uint64_t offset = 0;
	std::string sepoch = "/sys/class/rtc/rtc0/since_epoch";

	if (TWFunc::read_file(sepoch, offset) == 0) {

		LOGINFO("TWFunc::Fixup_Time: Setting time offset from file %s\n", sepoch.c_str());

		tv.tv_sec = offset;
		tv.tv_usec = 0;
		settimeofday(&tv, NULL);

		gettimeofday(&tv, NULL);

		if (tv.tv_sec > 1405209403) { // Anything older then 12 Jul 2014 23:56:43 GMT will do nicely thank you ;)

			LOGINFO("TWFunc::Fixup_Time: Date and time corrected: %s\n", TWFunc::Get_Current_Date().c_str());
			return;

		}

	} else {

		LOGINFO("TWFunc::Fixup_Time: opening %s failed\n", sepoch.c_str());

	}

	LOGINFO("TWFunc::Fixup_Time: will attempt to use the ats files now.\n");

	// Devices with Qualcomm Snapdragon 800 do some shenanigans with RTC.
	// They never set it, it just ticks forward from 1970-01-01 00:00,
	// and then they have files /data/system/time/ats_* with 64bit offset
	// in miliseconds which, when added to the RTC, gives the correct time.
	// So, the time is: (offset_from_ats + value_from_RTC)
	// There are multiple ats files, they are for different systems? Bases?
	// Like, ats_1 is for modem and ats_2 is for TOD (time of day?).
	// Look at file time_genoff.h in CodeAurora, qcom-opensource/time-services

	static const char *paths[] = { "/data/system/time/", "/data/time/"  };

	FILE *f;
	DIR *d;
	offset = 0;
	struct dirent *dt;
	std::string ats_path;

	if(!PartitionManager.Mount_By_Path("/data", false))
		return;

	// Prefer ats_2, it seems to be the one we want according to logcat on hammerhead
	// - it is the one for ATS_TOD (time of day?).
	// However, I never saw a device where the offset differs between ats files.
	for(size_t i = 0; i < (sizeof(paths)/sizeof(paths[0])); ++i)
	{
		DIR *d = opendir(paths[i]);
		if(!d)
			continue;

		while((dt = readdir(d)))
		{
			if(dt->d_type != DT_REG || strncmp(dt->d_name, "ats_", 4) != 0)
				continue;

			if(ats_path.empty() || strcmp(dt->d_name, "ats_2") == 0)
				ats_path = std::string(paths[i]).append(dt->d_name);
		}

		closedir(d);
	}

	if(ats_path.empty())
	{
		LOGINFO("TWFunc::Fixup_Time: no ats files found, leaving untouched!\n");
		return;
	}

	f = fopen(ats_path.c_str(), "r");
	if(!f)
	{
		LOGINFO("TWFunc::Fixup_Time: failed to open file %s\n", ats_path.c_str());
		return;
	}

	if(fread(&offset, sizeof(offset), 1, f) != 1)
	{
		LOGINFO("TWFunc::Fixup_Time: failed load uint64 from file %s\n", ats_path.c_str());
		fclose(f);
		return;
	}
	fclose(f);

	LOGINFO("TWFunc::Fixup_Time: Setting time offset from file %s, offset %llu\n", ats_path.c_str(), offset);

	gettimeofday(&tv, NULL);

	tv.tv_sec += offset/1000;
	tv.tv_usec += (offset%1000)*1000;

	while(tv.tv_usec >= 1000000)
	{
		++tv.tv_sec;
		tv.tv_usec -= 1000000;
	}

	settimeofday(&tv, NULL);

	LOGINFO("TWFunc::Fixup_Time: Date and time corrected: %s\n", TWFunc::Get_Current_Date().c_str());

#endif
}

std::vector<std::string> TWFunc::Split_String(const std::string& str, const std::string& delimiter, bool removeEmpty)
{
	std::vector<std::string> res;
	size_t idx = 0, idx_last = 0;

	while(idx < str.size())
	{
		idx = str.find_first_of(delimiter, idx_last);
		if(idx == std::string::npos)
			idx = str.size();

		if(idx-idx_last != 0 || !removeEmpty)
			res.push_back(str.substr(idx_last, idx-idx_last));

		idx_last = idx + delimiter.size();
	}

	return res;
}

bool TWFunc::Create_Dir_Recursive(const std::string& path, mode_t mode, uid_t uid, gid_t gid)
{
	std::vector<std::string> parts = Split_String(path, "/");
	std::string cur_path;
	struct stat info;
	for(size_t i = 0; i < parts.size(); ++i)
	{
		cur_path += "/" + parts[i];
		if(stat(cur_path.c_str(), &info) < 0 || !S_ISDIR(info.st_mode))
		{
			if(mkdir(cur_path.c_str(), mode) < 0)
				return false;
			chown(cur_path.c_str(), uid, gid);
		}
	}
	return true;
}

int TWFunc::Set_Brightness(std::string brightness_value)
{
	int result = -1;
	std::string secondary_brightness_file;

	if (DataManager::GetIntValue("tw_has_brightnesss_file")) {
		LOGINFO("TWFunc::Set_Brightness: Setting brightness control to %s\n", brightness_value.c_str());
		result = TWFunc::write_file(DataManager::GetStrValue("tw_brightness_file"), brightness_value);
		DataManager::GetValue("tw_secondary_brightness_file", secondary_brightness_file);
		if (!secondary_brightness_file.empty()) {
			LOGINFO("TWFunc::Set_Brightness: Setting secondary brightness control to %s\n", brightness_value.c_str());
			TWFunc::write_file(secondary_brightness_file, brightness_value);
		}
	}
	return result;
}

bool TWFunc::Toggle_MTP(bool enable) {
#ifdef TW_HAS_MTP
	static int was_enabled = false;

	if (enable && was_enabled) {
		if (!PartitionManager.Enable_MTP())
			PartitionManager.Disable_MTP();
	} else {
		was_enabled = DataManager::GetIntValue("tw_mtp_enabled");
		PartitionManager.Disable_MTP();
		usleep(500);
	}
	return was_enabled;
#else
	return false;
#endif
}

void TWFunc::SetPerformanceMode(bool mode) {
	if (mode) {
		property_set("recovery.perf.mode", "1");
	} else {
		property_set("recovery.perf.mode", "0");
	}
	// Some time for events to catch up to init handlers
	usleep(500000);
}

std::string TWFunc::to_string(unsigned long value) {
	std::ostringstream os;
	os << value;
	return os.str();
}

void TWFunc::Disable_Stock_Recovery_Replace(void) {
	if (PartitionManager.Mount_By_Path("/system", false)) {
		// Disable flashing of stock recovery
		if (TWFunc::Path_Exists("/system/recovery-from-boot.p")) {
			rename("/system/recovery-from-boot.p", "/system/recovery-from-boot.bak");
			gui_msg("rename_stock=Renamed stock recovery file in /system to prevent the stock ROM from replacing TWRP.");
			sync();
		}
		PartitionManager.UnMount_By_Path("/system", false);
	}
}

unsigned long long TWFunc::IOCTL_Get_Block_Size(const char* block_device) {
	unsigned long block_device_size;
	int ret = 0;

	int fd = open(block_device, O_RDONLY);
	if (fd < 0) {
		LOGINFO("Find_Partition_Size: Failed to open '%s', (%s)\n", block_device, strerror(errno));
	} else {
		ret = ioctl(fd, BLKGETSIZE, &block_device_size);
		close(fd);
		if (ret) {
			LOGINFO("Find_Partition_Size: ioctl error: (%s)\n", strerror(errno));
		} else {
			return (unsigned long long)(block_device_size) * 512LLU;
		}
	}
	return 0;
}

#endif // ndef BUILD_TWRPTAR_MAIN
