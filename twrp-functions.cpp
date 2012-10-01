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
#include <sys/reboot.h>

#include "twrp-functions.hpp"
#include "partitions.hpp"
#include "common.h"
#include "data.hpp"
#include "bootloader.h"
#include "variables.h"

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

static const char *COMMAND_FILE = "/cache/recovery/command";
static const char *INTENT_FILE = "/cache/recovery/intent";
static const char *LOG_FILE = "/cache/recovery/log";
static const char *LAST_LOG_FILE = "/cache/recovery/last_log";
static const char *LAST_INSTALL_FILE = "/cache/recovery/last_install";
static const char *CACHE_ROOT = "/cache";
static const char *SDCARD_ROOT = "/sdcard";
static const char *TEMPORARY_LOG_FILE = "/tmp/recovery.log";
static const char *TEMPORARY_INSTALL_FILE = "/tmp/last_install";

// close a file, log an error if the error indicator is set
void TWFunc::check_and_fclose(FILE *fp, const char *name) {
    fflush(fp);
    if (ferror(fp)) LOGE("Error in %s\n(%s)\n", name, strerror(errno));
    fclose(fp);
}

void TWFunc::copy_log_file(const char* source, const char* destination, int append) {
    FILE *log = fopen_path(destination, append ? "a" : "w");
    if (log == NULL) {
        LOGE("Can't open %s\n", destination);
    } else {
        FILE *tmplog = fopen(source, "r");
        if (tmplog != NULL) {
            if (append) {
                fseek(tmplog, tmplog_offset, SEEK_SET);  // Since last write
            }
            char buf[4096];
            while (fgets(buf, sizeof(buf), tmplog)) fputs(buf, log);
            if (append) {
                tmplog_offset = ftell(tmplog);
            }
            check_and_fclose(tmplog, source);
        }
        check_and_fclose(log, destination);
    }
}

// clear the recovery command and prepare to boot a (hopefully working) system,
// copy our log file to cache as well (for the system to read), and
// record any intent we were asked to communicate back to the system.
// this function is idempotent: call it as many times as you like.
void TWFunc::twfinish_recovery(const char *send_intent) {
    // By this point, we're ready to return to the main system...
    if (send_intent != NULL) {
        FILE *fp = fopen_path(INTENT_FILE, "w");
        if (fp == NULL) {
            LOGE("Can't open %s\n", INTENT_FILE);
        } else {
            fputs(send_intent, fp);
            check_and_fclose(fp, INTENT_FILE);
        }
    }

    // Copy logs to cache so the system can find out what happened.
    copy_log_file(TEMPORARY_LOG_FILE, LOG_FILE, true);
    copy_log_file(TEMPORARY_LOG_FILE, LAST_LOG_FILE, false);
    copy_log_file(TEMPORARY_INSTALL_FILE, LAST_INSTALL_FILE, false);
    chmod(LOG_FILE, 0600);
    chown(LOG_FILE, 1000, 1000);   // system user
    chmod(LAST_LOG_FILE, 0640);
    chmod(LAST_INSTALL_FILE, 0644);

    // Reset to normal system boot so recovery won't cycle indefinitely.
    struct bootloader_message boot;
    memset(&boot, 0, sizeof(boot));
    set_bootloader_message(&boot);

    // Remove the command file, so recovery won't repeat indefinitely.
    if (system("mount /cache") != 0 ||
        (unlink(COMMAND_FILE) && errno != ENOENT)) {
        LOGW("Can't unlink %s\n", COMMAND_FILE);
    }

    system("umount /cache");
    sync();  // For good measure.
}

// reboot: Reboot the system. Return -1 on error, no return on success
int TWFunc::tw_reboot(RebootCommand command)
{
	// Always force a sync before we reboot
    sync();

    switch (command)
    {
    case rb_current:
    case rb_system:
        twfinish_recovery("s");
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
        return reboot(RB_POWER_OFF);
    case rb_download:
		check_and_run_script("/sbin/rebootdownload.sh", "reboot download");
		return __reboot(LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, LINUX_REBOOT_CMD_RESTART2, (void*) "download");
	return 1;
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
		ui_print("Running %s script...\n", display_name);
		char command[255];
		strcpy(command, "chmod 755 ");
		strcat(command, script_file);
		system(command);
		system(script_file);
		ui_print("\nFinished running %s script.\n", display_name);
	}
}
