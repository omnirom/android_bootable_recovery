/*
	Copyright 2013 TeamWin
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
#include <sys/stat.h>
#include <sys/vfs.h>
#include <sys/mount.h>
#include <unistd.h>
#include <dirent.h>
#include <iostream>
#include <sstream>

#ifdef TW_INCLUDE_CRYPTO
	#include "cutils/properties.h"
#endif

#include "libblkid/blkid.h"
#include "variables.h"
#include "twcommon.h"
#include "partitions.hpp"
#include "data.hpp"
#include "twrp-functions.hpp"
#include "twrpDigest.hpp"
#include "twrpTar.hpp"
#include "twrpDU.hpp"
#include "fixPermissions.hpp"
extern "C" {
	#include "mtdutils/mtdutils.h"
	#include "mtdutils/mounts.h"
#ifdef TW_INCLUDE_CRYPTO_SAMSUNG
	#include "crypto/libcrypt_samsung/include/libcrypt_samsung.h"
#endif
#ifdef USE_EXT4
	#include "make_ext4fs.h"
#endif
}
#ifdef HAVE_SELINUX
#include "selinux/selinux.h"
#endif

using namespace std;

extern struct selabel_handle *selinux_handle;

struct flag_list {
	const char *name;
	unsigned flag;
};

static struct flag_list mount_flags[] = {
	{ "noatime",    MS_NOATIME },
	{ "noexec",     MS_NOEXEC },
	{ "nosuid",     MS_NOSUID },
	{ "nodev",      MS_NODEV },
	{ "nodiratime", MS_NODIRATIME },
	{ "ro",         MS_RDONLY },
	{ "rw",         0 },
	{ "remount",    MS_REMOUNT },
	{ "bind",       MS_BIND },
	{ "rec",        MS_REC },
#ifdef MS_UNBINDABLE
	{ "unbindable", MS_UNBINDABLE },
#endif
#ifdef MS_PRIVATE
	{ "private",    MS_PRIVATE },
#endif
#ifdef MS_SLAVE
	{ "slave",      MS_SLAVE },
#endif
#ifdef MS_SHARED
	{ "shared",     MS_SHARED },
#endif
	{ "sync",       MS_SYNCHRONOUS },
	{ "defaults",   0 },
	{ 0,            0 },
};

TWPartition::TWPartition(void) {
	Can_Be_Mounted = false;
	Can_Be_Wiped = false;
	Can_Be_Backed_Up = false;
	Use_Rm_Rf = false;
	Wipe_During_Factory_Reset = false;
	Wipe_Available_in_GUI = false;
	Is_SubPartition = false;
	Has_SubPartition = false;
	SubPartition_Of = "";
	Symlink_Path = "";
	Symlink_Mount_Point = "";
	Mount_Point = "";
	Backup_Path = "";
	Actual_Block_Device = "";
	Primary_Block_Device = "";
	Alternate_Block_Device = "";
	Removable = false;
	Is_Present = false;
	Length = 0;
	Size = 0;
	Used = 0;
	Free = 0;
	Backup_Size = 0;
	Can_Be_Encrypted = false;
	Is_Encrypted = false;
	Is_Decrypted = false;
	Decrypted_Block_Device = "";
	Display_Name = "";
	Backup_Display_Name = "";
	Storage_Name = "";
	Backup_Name = "";
	Backup_FileName = "";
	MTD_Name = "";
	Backup_Method = NONE;
	Can_Encrypt_Backup = false;
	Use_Userdata_Encryption = false;
	Has_Data_Media = false;
	Has_Android_Secure = false;
	Is_Storage = false;
	Is_Settings_Storage = false;
	Storage_Path = "";
	Current_File_System = "";
	Fstab_File_System = "";
	Mount_Flags = 0;
	Mount_Options = "";
	Format_Block_Size = 0;
	Ignore_Blkid = false;
	Retain_Layout_Version = false;
#ifdef TW_INCLUDE_CRYPTO_SAMSUNG
	EcryptFS_Password = "";
#endif
}

TWPartition::~TWPartition(void) {
	// Do nothing
}

bool TWPartition::Process_Fstab_Line(string Line, bool Display_Error) {
	char full_line[MAX_FSTAB_LINE_LENGTH], item[MAX_FSTAB_LINE_LENGTH];
	int line_len = Line.size(), index = 0, item_index = 0;
	char* ptr;
	string Flags;
	strncpy(full_line, Line.c_str(), line_len);
	bool skip = false;

	for (index = 0; index < line_len; index++) {
		if (full_line[index] == 34)
			skip = !skip;
		if (!skip && full_line[index] <= 32)
			full_line[index] = '\0';
	}
	Mount_Point = full_line;
	LOGINFO("Processing '%s'\n", Mount_Point.c_str());
	Backup_Path = Mount_Point;
	Storage_Path = Mount_Point;
	Display_Name = full_line + 1;
	Backup_Display_Name = Display_Name;
	Storage_Name = Display_Name;
	index = Mount_Point.size();
	while (index < line_len) {
		while (index < line_len && full_line[index] == '\0')
			index++;
		if (index >= line_len)
			continue;
		ptr = full_line + index;
		if (item_index == 0) {
			// File System
			Fstab_File_System = ptr;
			Current_File_System = ptr;
			item_index++;
		} else if (item_index == 1) {
			// Primary Block Device
			if (Fstab_File_System == "mtd" || Fstab_File_System == "yaffs2") {
				MTD_Name = ptr;
				Find_MTD_Block_Device(MTD_Name);
			} else if (Fstab_File_System == "bml") {
				if (Mount_Point == "/boot")
					MTD_Name = "boot";
				else if (Mount_Point == "/recovery")
					MTD_Name = "recovery";
				Primary_Block_Device = ptr;
				if (*ptr != '/')
					LOGERR("Until we get better BML support, you will have to find and provide the full block device path to the BML devices e.g. /dev/block/bml9 instead of the partition name\n");
			} else if (*ptr != '/') {
				if (Display_Error)
					LOGERR("Invalid block device on '%s', '%s', %i\n", Line.c_str(), ptr, index);
				else
					LOGINFO("Invalid block device on '%s', '%s', %i\n", Line.c_str(), ptr, index);
				return 0;
			} else {
				Primary_Block_Device = ptr;
				Find_Real_Block_Device(Primary_Block_Device, Display_Error);
			}
			item_index++;
		} else if (item_index > 1) {
			if (*ptr == '/') {
				// Alternate Block Device
				Alternate_Block_Device = ptr;
				Find_Real_Block_Device(Alternate_Block_Device, Display_Error);
			} else if (strlen(ptr) > 7 && strncmp(ptr, "length=", 7) == 0) {
				// Partition length
				ptr += 7;
				Length = atoi(ptr);
			} else if (strlen(ptr) > 6 && strncmp(ptr, "flags=", 6) == 0) {
				// Custom flags, save for later so that new values aren't overwritten by defaults
				ptr += 6;
				Flags = ptr;
				Process_Flags(Flags, Display_Error);
			} else if (strlen(ptr) == 4 && (strncmp(ptr, "NULL", 4) == 0 || strncmp(ptr, "null", 4) == 0 || strncmp(ptr, "null", 4) == 0)) {
				// Do nothing
			} else {
				// Unhandled data
				LOGINFO("Unhandled fstab information: '%s', %i, line: '%s'\n", ptr, index, Line.c_str());
			}
		}
		while (index < line_len && full_line[index] != '\0')
			index++;
	}

	if (!Is_File_System(Fstab_File_System) && !Is_Image(Fstab_File_System)) {
		if (Display_Error)
			LOGERR("Unknown File System: '%s'\n", Fstab_File_System.c_str());
		else
			LOGINFO("Unknown File System: '%s'\n", Fstab_File_System.c_str());
		return 0;
	} else if (Is_File_System(Fstab_File_System)) {
		Find_Actual_Block_Device();
		Setup_File_System(Display_Error);
		if (Mount_Point == "/system") {
			Display_Name = "System";
			Backup_Display_Name = Display_Name;
			Storage_Name = Display_Name;
			Wipe_Available_in_GUI = true;
			Can_Be_Backed_Up = true;
		} else if (Mount_Point == "/data") {
			Display_Name = "Data";
			Backup_Display_Name = Display_Name;
			Storage_Name = Display_Name;
			Wipe_Available_in_GUI = true;
			Wipe_During_Factory_Reset = true;
			Can_Be_Backed_Up = true;
			Can_Encrypt_Backup = true;
			Use_Userdata_Encryption = true;
#ifdef RECOVERY_SDCARD_ON_DATA
			Storage_Name = "Internal Storage";
			Has_Data_Media = true;
			Is_Storage = true;
			Is_Settings_Storage = true;
			Storage_Path = "/data/media";
			Symlink_Path = Storage_Path;
			if (strcmp(EXPAND(TW_EXTERNAL_STORAGE_PATH), "/sdcard") == 0) {
				Make_Dir("/emmc", Display_Error);
				Symlink_Mount_Point = "/emmc";
			} else {
				Make_Dir("/sdcard", Display_Error);
				Symlink_Mount_Point = "/sdcard";
			}
			if (Mount(false) && TWFunc::Path_Exists("/data/media/0")) {
				Storage_Path = "/data/media/0";
				Symlink_Path = Storage_Path;
				DataManager::SetValue(TW_INTERNAL_PATH, "/data/media/0");
				UnMount(true);
			}
#endif
#ifdef TW_INCLUDE_CRYPTO
			Can_Be_Encrypted = true;
			char crypto_blkdev[255];
			property_get("ro.crypto.fs_crypto_blkdev", crypto_blkdev, "error");
			if (strcmp(crypto_blkdev, "error") != 0) {
				DataManager::SetValue(TW_DATA_BLK_DEVICE, Primary_Block_Device);
				DataManager::SetValue(TW_IS_DECRYPTED, 1);
				Is_Encrypted = true;
				Is_Decrypted = true;
				Decrypted_Block_Device = crypto_blkdev;
				LOGINFO("Data already decrypted, new block device: '%s'\n", crypto_blkdev);
			} else if (!Mount(false)) {
				Is_Encrypted = true;
				Is_Decrypted = false;
				Can_Be_Mounted = false;
				Current_File_System = "emmc";
				Setup_Image(Display_Error);
				DataManager::SetValue(TW_IS_ENCRYPTED, 1);
				DataManager::SetValue(TW_CRYPTO_PASSWORD, "");
				DataManager::SetValue("tw_crypto_display", "");
			} else {
				// Filesystem is not encrypted and the mount
				// succeeded, so get it back to the original
				// unmounted state
				UnMount(false);
			}
	#ifdef RECOVERY_SDCARD_ON_DATA
			if (!Is_Encrypted || (Is_Encrypted && Is_Decrypted))
				Recreate_Media_Folder();
	#endif
#else
	#ifdef RECOVERY_SDCARD_ON_DATA
			Recreate_Media_Folder();
	#endif
#endif
		} else if (Mount_Point == "/cache") {
			Display_Name = "Cache";
			Backup_Display_Name = Display_Name;
			Storage_Name = Display_Name;
			Wipe_Available_in_GUI = true;
			Wipe_During_Factory_Reset = true;
			Can_Be_Backed_Up = true;
			if (Mount(false) && !TWFunc::Path_Exists("/cache/recovery/.")) {
				LOGINFO("Recreating /cache/recovery folder.\n");
				if (mkdir("/cache/recovery", S_IRWXU | S_IRWXG | S_IWGRP | S_IXGRP) != 0) 
					return -1;
			}
		} else if (Mount_Point == "/datadata") {
			Wipe_During_Factory_Reset = true;
			Display_Name = "DataData";
			Backup_Display_Name = Display_Name;
			Storage_Name = Display_Name;
			Is_SubPartition = true;
			SubPartition_Of = "/data";
			DataManager::SetValue(TW_HAS_DATADATA, 1);
			Can_Be_Backed_Up = true;
			Can_Encrypt_Backup = true;
			Use_Userdata_Encryption = false; // This whole partition should be encrypted
		} else if (Mount_Point == "/sd-ext") {
			Wipe_During_Factory_Reset = true;
			Display_Name = "SD-Ext";
			Backup_Display_Name = Display_Name;
			Storage_Name = Display_Name;
			Wipe_Available_in_GUI = true;
			Removable = true;
			Can_Be_Backed_Up = true;
			Can_Encrypt_Backup = true;
			Use_Userdata_Encryption = true;
		} else if (Mount_Point == "/boot") {
			Display_Name = "Boot";
			Backup_Display_Name = Display_Name;
			DataManager::SetValue("tw_boot_is_mountable", 1);
			Can_Be_Backed_Up = true;
		}
#ifdef TW_EXTERNAL_STORAGE_PATH
		if (Mount_Point == EXPAND(TW_EXTERNAL_STORAGE_PATH)) {
			Is_Storage = true;
			Storage_Path = EXPAND(TW_EXTERNAL_STORAGE_PATH);
			Removable = true;
			Wipe_Available_in_GUI = true;
#else
		if (Mount_Point == "/sdcard" || Mount_Point == "/external_sd" || Mount_Point == "/external_sdcard") {
			Is_Storage = true;
			Removable = true;
			Wipe_Available_in_GUI = true;
#endif
		}
#ifdef TW_INTERNAL_STORAGE_PATH
		if (Mount_Point == EXPAND(TW_INTERNAL_STORAGE_PATH)) {
			Is_Storage = true;
			Is_Settings_Storage = true;
			Storage_Path = EXPAND(TW_INTERNAL_STORAGE_PATH);
			Wipe_Available_in_GUI = true;
		}
#else
		if (Mount_Point == "/emmc" || Mount_Point == "/internal_sd" || Mount_Point == "/internal_sdcard") {
			Is_Storage = true;
			Is_Settings_Storage = true;
			Wipe_Available_in_GUI = true;
		}
#endif
	} else if (Is_Image(Fstab_File_System)) {
		Find_Actual_Block_Device();
		Setup_Image(Display_Error);
		if (Mount_Point == "/boot") {
			Display_Name = "Boot";
			Backup_Display_Name = Display_Name;
			Can_Be_Backed_Up = true;
		} else if (Mount_Point == "/recovery") {
			Display_Name = "Recovery";
			Backup_Display_Name = Display_Name;
		}
	}

	// Process any custom flags
	if (Flags.size() > 0)
		Process_Flags(Flags, Display_Error);
	return true;
}

bool TWPartition::Process_FS_Flags(string& Options, int Flags) {
	int i;
	char *p;
	char *savep;
	char fs_options[250];

	strlcpy(fs_options, Options.c_str(), sizeof(fs_options));
	Options = "";

	p = strtok_r(fs_options, ",", &savep);
	while (p) {
		/* Look for the flag "p" in the flag list "fl"
		* If not found, the loop exits with fl[i].name being null.
		*/
		for (i = 0; mount_flags[i].name; i++) {
			if (strncmp(p, mount_flags[i].name, strlen(mount_flags[i].name)) == 0) {
				Flags |= mount_flags[i].flag;
				break;
			}
		}

		if (!mount_flags[i].name) {
			if (Options.size() > 0)
				Options += ",";
			Options += p;
		}
		p = strtok_r(NULL, ",", &savep);
	}

	return true;
}

bool TWPartition::Process_Flags(string Flags, bool Display_Error) {
	char flags[MAX_FSTAB_LINE_LENGTH];
	int flags_len, index = 0, ptr_len;
	char* ptr;
	bool skip = false, has_display_name = false, has_storage_name = false, has_backup_name = false;

	strcpy(flags, Flags.c_str());
	flags_len = Flags.size();
	for (index = 0; index < flags_len; index++) {
		if (flags[index] == 34)
			skip = !skip;
		if (!skip && flags[index] == ';')
			flags[index] = '\0';
	}

	index = 0;
	while (index < flags_len) {
		while (index < flags_len && flags[index] == '\0')
			index++;
		if (index >= flags_len)
			continue;
		ptr = flags + index;
		ptr_len = strlen(ptr);
		if (strcmp(ptr, "removable") == 0) {
			Removable = true;
		} else if (strncmp(ptr, "storage", 7) == 0) {
			if (ptr_len == 7) {
				LOGINFO("ptr_len is 7, storage set to true\n");
				Is_Storage = true;
			} else if (ptr_len == 9) {
				ptr += 9;
				if (*ptr == '1' || *ptr == 'y' || *ptr == 'Y') {
					LOGINFO("storage set to true\n");
					Is_Storage = true;
				} else {
					LOGINFO("storage set to false\n");
					Is_Storage = false;
				}
			}
		} else if (strcmp(ptr, "settingsstorage") == 0) {
			Is_Storage = true;
		} else if (strcmp(ptr, "canbewiped") == 0) {
			Can_Be_Wiped = true;
		} else if (strcmp(ptr, "usermrf") == 0) {
			Use_Rm_Rf = true;
		} else if (ptr_len > 7 && strncmp(ptr, "backup=", 7) == 0) {
			ptr += 7;
			if (*ptr == '1' || *ptr == 'y' || *ptr == 'Y')
				Can_Be_Backed_Up = true;
			else
				Can_Be_Backed_Up = false;
		} else if (strcmp(ptr, "wipeingui") == 0) {
			Can_Be_Wiped = true;
			Wipe_Available_in_GUI = true;
		} else if (strcmp(ptr, "wipeduringfactoryreset") == 0) {
			Can_Be_Wiped = true;
			Wipe_Available_in_GUI = true;
			Wipe_During_Factory_Reset = true;
		} else if (ptr_len > 15 && strncmp(ptr, "subpartitionof=", 15) == 0) {
			ptr += 15;
			Is_SubPartition = true;
			SubPartition_Of = ptr;
		} else if (strcmp(ptr, "ignoreblkid") == 0) {
			Ignore_Blkid = true;
		} else if (strcmp(ptr, "retainlayoutversion") == 0) {
			Retain_Layout_Version = true;
		} else if (ptr_len > 8 && strncmp(ptr, "symlink=", 8) == 0) {
			ptr += 8;
			Symlink_Path = ptr;
		} else if (ptr_len > 8 && strncmp(ptr, "display=", 8) == 0) {
			has_display_name = true;
			ptr += 8;
			if (*ptr == '\"') ptr++;
			Display_Name = ptr;
			if (Display_Name.substr(Display_Name.size() - 1, 1) == "\"") {
				Display_Name.resize(Display_Name.size() - 1);
			}
		} else if (ptr_len > 11 && strncmp(ptr, "storagename=", 11) == 0) {
			has_storage_name = true;
			ptr += 11;
			if (*ptr == '\"') ptr++;
			Storage_Name = ptr;
			if (Storage_Name.substr(Storage_Name.size() - 1, 1) == "\"") {
				Storage_Name.resize(Storage_Name.size() - 1);
			}
		} else if (ptr_len > 11 && strncmp(ptr, "backupname=", 10) == 0) {
			has_backup_name = true;
			ptr += 10;
			if (*ptr == '\"') ptr++;
			Backup_Display_Name = ptr;
			if (Backup_Display_Name.substr(Backup_Display_Name.size() - 1, 1) == "\"") {
				Backup_Display_Name.resize(Backup_Display_Name.size() - 1);
			}
		} else if (ptr_len > 10 && strncmp(ptr, "blocksize=", 10) == 0) {
			ptr += 10;
			Format_Block_Size = atoi(ptr);
		} else if (ptr_len > 7 && strncmp(ptr, "length=", 7) == 0) {
			ptr += 7;
			Length = atoi(ptr);
		} else if (ptr_len > 17 && strncmp(ptr, "canencryptbackup=", 17) == 0) {
			ptr += 17;
			if (*ptr == '1' || *ptr == 'y' || *ptr == 'Y')
				Can_Encrypt_Backup = true;
			else
				Can_Encrypt_Backup = false;
		} else if (ptr_len > 21 && strncmp(ptr, "userdataencryptbackup=", 21) == 0) {
			ptr += 21;
			if (*ptr == '1' || *ptr == 'y' || *ptr == 'Y') {
				Can_Encrypt_Backup = true;
				Use_Userdata_Encryption = true;
			} else {
				Use_Userdata_Encryption = false;
			}
		} else if (ptr_len > 8 && strncmp(ptr, "fsflags=", 8) == 0) {
			ptr += 8;
			if (*ptr == '\"') ptr++;

			Mount_Options = ptr;
			if (Mount_Options.substr(Mount_Options.size() - 1, 1) == "\"") {
				Mount_Options.resize(Mount_Options.size() - 1);
			}
			Process_FS_Flags(Mount_Options, Mount_Flags);
		} else {
			if (Display_Error)
				LOGERR("Unhandled flag: '%s'\n", ptr);
			else
				LOGINFO("Unhandled flag: '%s'\n", ptr);
		}
		while (index < flags_len && flags[index] != '\0')
			index++;
	}
	if (has_display_name && !has_storage_name)
		Storage_Name = Display_Name;
	if (!has_display_name && has_storage_name)
		Display_Name = Storage_Name;
	if (has_display_name && !has_backup_name && Backup_Display_Name != "Android Secure")
		Backup_Display_Name = Display_Name;
	if (!has_display_name && has_backup_name)
		Display_Name = Backup_Display_Name;
	return true;
}

bool TWPartition::Is_File_System(string File_System) {
	if (File_System == "ext2" ||
		File_System == "ext3" ||
		File_System == "ext4" ||
		File_System == "vfat" ||
		File_System == "ntfs" ||
		File_System == "yaffs2" ||
		File_System == "exfat" ||
		File_System == "f2fs" ||
		File_System == "auto")
		return true;
	else
		return false;
}

bool TWPartition::Is_Image(string File_System) {
	if (File_System == "emmc" || File_System == "mtd" || File_System == "bml")
		return true;
	else
		return false;
}

bool TWPartition::Make_Dir(string Path, bool Display_Error) {
	if (!TWFunc::Path_Exists(Path)) {
		if (mkdir(Path.c_str(), 0777) == -1) {
			if (Display_Error)
				LOGERR("Can not create '%s' folder.\n", Path.c_str());
			else
				LOGINFO("Can not create '%s' folder.\n", Path.c_str());
			return false;
		} else {
			LOGINFO("Created '%s' folder.\n", Path.c_str());
			return true;
		}
	}
	return true;
}

void TWPartition::Setup_File_System(bool Display_Error) {
	struct statfs st;

	Can_Be_Mounted = true;
	Can_Be_Wiped = true;

	// Make the mount point folder if it doesn't exist
	Make_Dir(Mount_Point, Display_Error);
	Display_Name = Mount_Point.substr(1, Mount_Point.size() - 1);
	Backup_Name = Display_Name;
	Backup_Method = FILES;
}

void TWPartition::Setup_Image(bool Display_Error) {
	Display_Name = Mount_Point.substr(1, Mount_Point.size() - 1);
	Backup_Name = Display_Name;
	if (Current_File_System == "emmc")
		Backup_Method = DD;
	else if (Current_File_System == "mtd" || Current_File_System == "bml")
		Backup_Method = FLASH_UTILS;
	else
		LOGINFO("Unhandled file system '%s' on image '%s'\n", Current_File_System.c_str(), Display_Name.c_str());
	if (Find_Partition_Size()) {
		Used = Size;
		Backup_Size = Size;
	} else {
		if (Display_Error)
			LOGERR("Unable to find partition size for '%s'\n", Mount_Point.c_str());
		else
			LOGINFO("Unable to find partition size for '%s'\n", Mount_Point.c_str());
	}
}

void TWPartition::Setup_AndSec(void) {
	Backup_Display_Name = "Android Secure";
	Backup_Name = "and-sec";
	Can_Be_Backed_Up = true;
	Has_Android_Secure = true;
	Symlink_Path = Mount_Point + "/.android_secure";
	Symlink_Mount_Point = "/and-sec";
	Backup_Path = Symlink_Mount_Point;
	Make_Dir("/and-sec", true);
	Recreate_AndSec_Folder();
	Mount_Storage_Retry();
}

void TWPartition::Find_Real_Block_Device(string& Block, bool Display_Error) {
	char device[512], realDevice[512];

	strcpy(device, Block.c_str());
	memset(realDevice, 0, sizeof(realDevice));
	while (readlink(device, realDevice, sizeof(realDevice)) > 0)
	{
		strcpy(device, realDevice);
		memset(realDevice, 0, sizeof(realDevice));
	}

	if (device[0] != '/') {
		if (Display_Error)
			LOGERR("Invalid symlink path '%s' found on block device '%s'\n", device, Block.c_str());
		else
			LOGINFO("Invalid symlink path '%s' found on block device '%s'\n", device, Block.c_str());
		return;
	} else {
		Block = device;
		return;
	}
}

void TWPartition::Mount_Storage_Retry(void) {
	// On some devices, storage doesn't want to mount right away, retry and sleep
	if (!Mount(true)) {
		int retry_count = 5;
		while (retry_count > 0 && !Mount(false)) {
			usleep(500000);
			retry_count--;
		}
		Mount(true);
	}
}

bool TWPartition::Find_MTD_Block_Device(string MTD_Name) {
	FILE *fp = NULL;
	char line[255];

	fp = fopen("/proc/mtd", "rt");
	if (fp == NULL) {
		LOGERR("Device does not support /proc/mtd\n");
		return false;
	}

	while (fgets(line, sizeof(line), fp) != NULL)
	{
		char device[32], label[32];
		unsigned long size = 0;
		char* fstype = NULL;
		int deviceId;

		sscanf(line, "%s %lx %*s %*c%s", device, &size, label);

		// Skip header and blank lines
		if ((strcmp(device, "dev:") == 0) || (strlen(line) < 8))
			continue;

		// Strip off the trailing " from the label
		label[strlen(label)-1] = '\0';

		if (strcmp(label, MTD_Name.c_str()) == 0) {
			// We found our device
			// Strip off the trailing : from the device
			device[strlen(device)-1] = '\0';
			if (sscanf(device,"mtd%d", &deviceId) == 1) {
				sprintf(device, "/dev/block/mtdblock%d", deviceId);
				Primary_Block_Device = device;
				fclose(fp);
				return true;
			}
		}
	}
	fclose(fp);

	return false;
}

bool TWPartition::Get_Size_Via_statfs(bool Display_Error) {
	struct statfs st;
	string Local_Path = Mount_Point + "/.";

	if (!Mount(Display_Error))
		return false;

	if (statfs(Local_Path.c_str(), &st) != 0) {
		if (!Removable) {
			if (Display_Error)
				LOGERR("Unable to statfs '%s'\n", Local_Path.c_str());
			else
				LOGINFO("Unable to statfs '%s'\n", Local_Path.c_str());
		}
		return false;
	}
	Size = (st.f_blocks * st.f_bsize);
	Used = ((st.f_blocks - st.f_bfree) * st.f_bsize);
	Free = (st.f_bfree * st.f_bsize);
	Backup_Size = Used;
	return true;
}

bool TWPartition::Get_Size_Via_df(bool Display_Error) {
	FILE* fp;
	char command[255], line[512];
	int include_block = 1;
	unsigned int min_len;

	if (!Mount(Display_Error))
		return false;

	min_len = Actual_Block_Device.size() + 2;
	sprintf(command, "df %s > /tmp/dfoutput.txt", Mount_Point.c_str());
	TWFunc::Exec_Cmd(command);
	fp = fopen("/tmp/dfoutput.txt", "rt");
	if (fp == NULL) {
		LOGINFO("Unable to open /tmp/dfoutput.txt.\n");
		return false;
	}

	while (fgets(line, sizeof(line), fp) != NULL)
	{
		unsigned long blocks, used, available;
		char device[64];
		char tmpString[64];

		if (strncmp(line, "Filesystem", 10) == 0)
			continue;
		if (strlen(line) < min_len) {
			include_block = 0;
			continue;
		}
		if (include_block) {
			sscanf(line, "%s %lu %lu %lu", device, &blocks, &used, &available);
		} else {
			// The device block string is so long that the df information is on the next line
			int space_count = 0;
			sprintf(tmpString, "/dev/block/%s", Actual_Block_Device.c_str());
			while (tmpString[space_count] == 32)
				space_count++;
			sscanf(line + space_count, "%lu %lu %lu", &blocks, &used, &available);
		}

		// Adjust block size to byte size
		Size = blocks * 1024ULL;
		Used = used * 1024ULL;
		Free = available * 1024ULL;
		Backup_Size = Used;
	}
	fclose(fp);
	return true;
}

bool TWPartition::Find_Partition_Size(void) {
	FILE* fp;
	char line[512];
	string tmpdevice;

	fp = fopen("/proc/dumchar_info", "rt");
	if (fp != NULL) {
		while (fgets(line, sizeof(line), fp) != NULL)
		{
			char label[32], device[32];
			unsigned long size = 0;

			sscanf(line, "%s %lx %*lx %*lu %s", label, &size, device);

			// Skip header, annotation	and blank lines
			if ((strncmp(device, "/dev/", 5) != 0) || (strlen(line) < 8))
				continue;

			tmpdevice = "/dev/";
			tmpdevice += label;
			if (tmpdevice == Primary_Block_Device || tmpdevice == Alternate_Block_Device) {
				Size = size;
				fclose(fp);
				return true;
			}
		}
	}

	// In this case, we'll first get the partitions we care about (with labels)
	fp = fopen("/proc/partitions", "rt");
	if (fp == NULL)
		return false;

	while (fgets(line, sizeof(line), fp) != NULL)
	{
		unsigned long major, minor, blocks;
		char device[512];
		char tmpString[64];

		if (strlen(line) < 7 || line[0] == 'm')	 continue;
		sscanf(line + 1, "%lu %lu %lu %s", &major, &minor, &blocks, device);

		tmpdevice = "/dev/block/";
		tmpdevice += device;
		if (tmpdevice == Primary_Block_Device || tmpdevice == Alternate_Block_Device) {
			// Adjust block size to byte size
			Size = blocks * 1024ULL;
			fclose(fp);
			return true;
		}
	}
	fclose(fp);
	return false;
}

bool TWPartition::Is_Mounted(void) {
	if (!Can_Be_Mounted)
		return false;

	struct stat st1, st2;
	string test_path;

	// Check to see if the mount point directory exists
	test_path = Mount_Point + "/.";
	if (stat(test_path.c_str(), &st1) != 0)  return false;

	// Check to see if the directory above the mount point exists
	test_path = Mount_Point + "/../.";
	if (stat(test_path.c_str(), &st2) != 0)  return false;

	// Compare the device IDs -- if they match then we're (probably) using tmpfs instead of an actual device
	int ret = (st1.st_dev != st2.st_dev) ? true : false;

	return ret;
}

bool TWPartition::Mount(bool Display_Error) {
	int exfat_mounted = 0;

	if (Is_Mounted()) {
		return true;
	} else if (!Can_Be_Mounted) {
		return false;
	}

	Find_Actual_Block_Device();

	// Check the current file system before mounting
	Check_FS_Type();
	if (Current_File_System == "exfat" && TWFunc::Path_Exists("/sbin/exfat-fuse")) {
		string cmd = "/sbin/exfat-fuse -o big_writes,max_read=131072,max_write=131072 " + Actual_Block_Device + " " + Mount_Point;
		LOGINFO("cmd: %s\n", cmd.c_str());
		string result;
		if (TWFunc::Exec_Cmd(cmd, result) != 0) {
			LOGINFO("exfat-fuse failed to mount with result '%s', trying vfat\n", result.c_str());
			Current_File_System = "vfat";
		} else {
#ifdef TW_NO_EXFAT_FUSE
			UnMount(false);
			// We'll let the kernel handle it but using exfat-fuse to detect if the file system is actually exfat
			// Some kernels let us mount vfat as exfat which doesn't work out too well
#else
			exfat_mounted = 1;
#endif
		}
	}
	if (Fstab_File_System == "yaffs2") {
		// mount an MTD partition as a YAFFS2 filesystem.
		const unsigned long flags = MS_NOATIME | MS_NODEV | MS_NODIRATIME;
		if (mount(Actual_Block_Device.c_str(), Mount_Point.c_str(), Fstab_File_System.c_str(), flags, NULL) < 0) {
			if (mount(Actual_Block_Device.c_str(), Mount_Point.c_str(), Fstab_File_System.c_str(), flags | MS_RDONLY, NULL) < 0) {
				if (Display_Error)
					LOGERR("Failed to mount '%s' (MTD)\n", Mount_Point.c_str());
				else
					LOGINFO("Failed to mount '%s' (MTD)\n", Mount_Point.c_str());
				return false;
			} else {
				LOGINFO("Mounted '%s' (MTD) as RO\n", Mount_Point.c_str());
				return true;
			}
		} else {
			struct stat st;
			string test_path = Mount_Point;
			if (stat(test_path.c_str(), &st) < 0) {
				if (Display_Error)
					LOGERR("Failed to mount '%s' (MTD)\n", Mount_Point.c_str());
				else
					LOGINFO("Failed to mount '%s' (MTD)\n", Mount_Point.c_str());
				return false;
			}
			mode_t new_mode = st.st_mode | S_IXUSR | S_IXGRP | S_IXOTH;
			if (new_mode != st.st_mode) {
				LOGINFO("Fixing execute permissions for %s\n", Mount_Point.c_str());
				if (chmod(Mount_Point.c_str(), new_mode) < 0) {
					if (Display_Error)
						LOGERR("Couldn't fix permissions for %s: %s\n", Mount_Point.c_str(), strerror(errno));
					else
						LOGINFO("Couldn't fix permissions for %s: %s\n", Mount_Point.c_str(), strerror(errno));
					return false;
				}
			}
			return true;
		}
	} else if (!exfat_mounted && mount(Actual_Block_Device.c_str(), Mount_Point.c_str(), Current_File_System.c_str(), Mount_Flags, Mount_Options.c_str()) != 0 && mount(Actual_Block_Device.c_str(), Mount_Point.c_str(), Current_File_System.c_str(), Mount_Flags, NULL) != 0) {
#ifdef TW_NO_EXFAT_FUSE
		if (Current_File_System == "exfat") {
			LOGINFO("Mounting exfat failed, trying vfat...\n");
			if (mount(Actual_Block_Device.c_str(), Mount_Point.c_str(), "vfat", 0, NULL) != 0) {
				if (Display_Error)
					LOGERR("Unable to mount '%s'\n", Mount_Point.c_str());
				else
					LOGINFO("Unable to mount '%s'\n", Mount_Point.c_str());
				LOGINFO("Actual block device: '%s', current file system: '%s', flags: 0x%8x, options: '%s'\n", Actual_Block_Device.c_str(), Current_File_System.c_str(), Mount_Flags, Mount_Options.c_str());
				return false;
			}
		} else {
#endif
			if (Display_Error)
				LOGERR("Unable to mount '%s'\n", Mount_Point.c_str());
			else
				LOGINFO("Unable to mount '%s'\n", Mount_Point.c_str());
			LOGINFO("Actual block device: '%s', current file system: '%s'\n", Actual_Block_Device.c_str(), Current_File_System.c_str());
			return false;
#ifdef TW_NO_EXFAT_FUSE
		}
#endif
	}
#ifdef TW_INCLUDE_CRYPTO_SAMSUNG
	string MetaEcfsFile = EXPAND(TW_EXTERNAL_STORAGE_PATH);
	MetaEcfsFile += "/.MetaEcfsFile";
	if (EcryptFS_Password.size() > 0 && PartitionManager.Mount_By_Path("/data", false) && TWFunc::Path_Exists(MetaEcfsFile)) {
		if (mount_ecryptfs_drive(EcryptFS_Password.c_str(), Mount_Point.c_str(), Mount_Point.c_str(), 0) != 0) {
			if (Display_Error)
				LOGERR("Unable to mount ecryptfs for '%s'\n", Mount_Point.c_str());
			else
				LOGINFO("Unable to mount ecryptfs for '%s'\n", Mount_Point.c_str());
		} else {
			LOGINFO("Successfully mounted ecryptfs for '%s'\n", Mount_Point.c_str());
			Is_Decrypted = true;
		}
	} else if (Mount_Point == EXPAND(TW_EXTERNAL_STORAGE_PATH)) {
		if (Is_Decrypted)
			LOGINFO("Mounting external storage, '%s' is not encrypted\n", Mount_Point.c_str());
		Is_Decrypted = false;
	}
#endif
	if (Removable)
		Update_Size(Display_Error);

	if (!Symlink_Mount_Point.empty()) {
		string Command = "mount '" + Symlink_Path + "' '" + Symlink_Mount_Point + "'";
		TWFunc::Exec_Cmd(Command);
	}
	return true;
}

bool TWPartition::UnMount(bool Display_Error) {
	if (Is_Mounted()) {
		int never_unmount_system;

		DataManager::GetValue(TW_DONT_UNMOUNT_SYSTEM, never_unmount_system);
		if (never_unmount_system == 1 && Mount_Point == "/system")
			return true; // Never unmount system if you're not supposed to unmount it

#ifdef TW_INCLUDE_CRYPTO_SAMSUNG
		if (EcryptFS_Password.size() > 0) {
			if (unmount_ecryptfs_drive(Mount_Point.c_str()) != 0) {
				if (Display_Error)
					LOGERR("Unable to unmount ecryptfs for '%s'\n", Mount_Point.c_str());
				else
					LOGINFO("Unable to unmount ecryptfs for '%s'\n", Mount_Point.c_str());
			} else {
				LOGINFO("Successfully unmounted ecryptfs for '%s'\n", Mount_Point.c_str());
			}
		}
#endif

		if (!Symlink_Mount_Point.empty())
			umount(Symlink_Mount_Point.c_str());

		umount(Mount_Point.c_str());
		if (Is_Mounted()) {
			if (Display_Error)
				LOGERR("Unable to unmount '%s'\n", Mount_Point.c_str());
			else
				LOGINFO("Unable to unmount '%s'\n", Mount_Point.c_str());
			return false;
		} else
			return true;
	} else {
		return true;
	}
}

bool TWPartition::Wipe(string New_File_System) {
	bool wiped = false, update_crypt = false;
	int check;
	string Layout_Filename = Mount_Point + "/.layout_version";

	if (!Can_Be_Wiped) {
		LOGERR("Partition '%s' cannot be wiped.\n", Mount_Point.c_str());
		return false;
	}

	if (Mount_Point == "/cache")
		Log_Offset = 0;

#ifdef TW_INCLUDE_CRYPTO_SAMSUNG
	if (Mount_Point == "/data" && Mount(false)) {
		if (TWFunc::Path_Exists("/data/system/edk_p_sd"))
			TWFunc::copy_file("/data/system/edk_p_sd", "/tmp/edk_p_sd", 0600);
	}
#endif

	if (Retain_Layout_Version && Mount(false) && TWFunc::Path_Exists(Layout_Filename))
		TWFunc::copy_file(Layout_Filename, "/.layout_version", 0600);
	else
		unlink("/.layout_version");

	if (Has_Data_Media) {
		wiped = Wipe_Data_Without_Wiping_Media();
	} else {

		DataManager::GetValue(TW_RM_RF_VAR, check);

		if (check || Use_Rm_Rf)
			wiped = Wipe_RMRF();
		else if (New_File_System == "ext4")
			wiped = Wipe_EXT4();
		else if (New_File_System == "ext2" || New_File_System == "ext3")
			wiped = Wipe_EXT23(New_File_System);
		else if (New_File_System == "vfat")
			wiped = Wipe_FAT();
		else if (New_File_System == "exfat")
			wiped = Wipe_EXFAT();
		else if (New_File_System == "yaffs2")
			wiped = Wipe_MTD();
		else if (New_File_System == "f2fs")
			wiped = Wipe_F2FS();
		else {
			LOGERR("Unable to wipe '%s' -- unknown file system '%s'\n", Mount_Point.c_str(), New_File_System.c_str());
			unlink("/.layout_version");
			return false;
		}
		update_crypt = wiped;
	}

	if (wiped) {
#ifdef TW_INCLUDE_CRYPTO_SAMSUNG
		if (Mount_Point == "/data" && Mount(false)) {
			if (TWFunc::Path_Exists("/tmp/edk_p_sd")) {
				Make_Dir("/data/system", true);
				TWFunc::copy_file("/tmp/edk_p_sd", "/data/system/edk_p_sd", 0600);
			}
		}
#endif

		if (Mount_Point == "/cache")
			DataManager::Output_Version();

		if (TWFunc::Path_Exists("/.layout_version") && Mount(false))
			TWFunc::copy_file("/.layout_version", Layout_Filename, 0600);

		if (update_crypt) {
			Setup_File_System(false);
			if (Is_Encrypted && !Is_Decrypted) {
				// just wiped an encrypted partition back to its unencrypted state
				Is_Encrypted = false;
				Is_Decrypted = false;
				Decrypted_Block_Device = "";
				if (Mount_Point == "/data") {
					DataManager::SetValue(TW_IS_ENCRYPTED, 0);
					DataManager::SetValue(TW_IS_DECRYPTED, 0);
				}
			}
		}
	}
	return wiped;
}

bool TWPartition::Wipe() {
	if (Is_File_System(Current_File_System))
		return Wipe(Current_File_System);
	else
		return Wipe(Fstab_File_System);
}

bool TWPartition::Wipe_AndSec(void) {
	if (!Has_Android_Secure)
		return false;

	if (!Mount(true))
		return false;

	gui_print("Wiping %s\n", Backup_Display_Name.c_str());
	TWFunc::removeDir(Mount_Point + "/.android_secure/", true);
	return true;
}

bool TWPartition::Backup(string backup_folder) {
	if (Backup_Method == FILES)
		return Backup_Tar(backup_folder);
	else if (Backup_Method == DD)
		return Backup_DD(backup_folder);
	else if (Backup_Method == FLASH_UTILS)
		return Backup_Dump_Image(backup_folder);
	LOGERR("Unknown backup method for '%s'\n", Mount_Point.c_str());
	return false;
}

bool TWPartition::Check_MD5(string restore_folder) {
	string Full_Filename, md5file;
	char split_filename[512];
	int index = 0;
	twrpDigest md5sum;

	memset(split_filename, 0, sizeof(split_filename));
	Full_Filename = restore_folder + "/" + Backup_FileName;
	if (!TWFunc::Path_Exists(Full_Filename)) {
		// This is a split archive, we presume
		sprintf(split_filename, "%s%03i", Full_Filename.c_str(), index);
		LOGINFO("split_filename: %s\n", split_filename);
		md5file = split_filename;
		md5file += ".md5";
		if (!TWFunc::Path_Exists(md5file)) {
			LOGERR("No md5 file found for '%s'.\n", split_filename);
			LOGERR("Please unselect Enable MD5 verification to restore.\n");
			return false;
		}
		md5sum.setfn(split_filename);
		while (index < 1000) {
			if (TWFunc::Path_Exists(split_filename) && md5sum.verify_md5digest() != 0) {
				LOGERR("MD5 failed to match on '%s'.\n", split_filename);
				return false;
			}
			index++;
			sprintf(split_filename, "%s%03i", Full_Filename.c_str(), index);
			md5sum.setfn(split_filename);
		}
		return true;
	} else {
		// Single file archive
		md5file = Full_Filename + ".md5";
		if (!TWFunc::Path_Exists(md5file)) {
			LOGERR("No md5 file found for '%s'.\n", Full_Filename.c_str());
			LOGERR("Please unselect Enable MD5 verification to restore.\n");
			return false;
		}
		md5sum.setfn(Full_Filename);
		if (md5sum.verify_md5digest() != 0) {
			LOGERR("MD5 failed to match on '%s'.\n", Full_Filename.c_str());
			return false;
		} else
			return true;
	}
	return false;
}

bool TWPartition::Restore(string restore_folder) {
	size_t first_period, second_period;
	string Restore_File_System;

	TWFunc::GUI_Operation_Text(TW_RESTORE_TEXT, Display_Name, "Restoring");
	LOGINFO("Restore filename is: %s\n", Backup_FileName.c_str());

	// Parse backup filename to extract the file system before wiping
	first_period = Backup_FileName.find(".");
	if (first_period == string::npos) {
		LOGERR("Unable to find file system (first period).\n");
		return false;
	}
	Restore_File_System = Backup_FileName.substr(first_period + 1, Backup_FileName.size() - first_period - 1);
	second_period = Restore_File_System.find(".");
	if (second_period == string::npos) {
		LOGERR("Unable to find file system (second period).\n");
		return false;
	}
	Restore_File_System.resize(second_period);
	LOGINFO("Restore file system is: '%s'.\n", Restore_File_System.c_str());

	if (Is_File_System(Restore_File_System))
		return Restore_Tar(restore_folder, Restore_File_System);
	else if (Is_Image(Restore_File_System)) {
		if (Restore_File_System == "emmc")
			return Restore_DD(restore_folder);
		else if (Restore_File_System == "mtd" || Restore_File_System == "bml")
			return Restore_Flash_Image(restore_folder);
	}

	LOGERR("Unknown restore method for '%s'\n", Mount_Point.c_str());
	return false;
}

string TWPartition::Backup_Method_By_Name() {
	if (Backup_Method == NONE)
		return "none";
	else if (Backup_Method == FILES)
		return "files";
	else if (Backup_Method == DD)
		return "dd";
	else if (Backup_Method == FLASH_UTILS)
		return "flash_utils";
	else
		return "undefined";
	return "ERROR!";
}

bool TWPartition::Decrypt(string Password) {
	LOGINFO("STUB TWPartition::Decrypt, password: '%s'\n", Password.c_str());
	// Is this needed?
	return 1;
}

bool TWPartition::Wipe_Encryption() {
	bool Save_Data_Media = Has_Data_Media;

	if (!UnMount(true))
		return false;

	Has_Data_Media = false;
	Decrypted_Block_Device = "";
	Is_Decrypted = false;
	Is_Encrypted = false;
	if (Wipe(Fstab_File_System)) {
		Has_Data_Media = Save_Data_Media;
		if (Has_Data_Media && !Symlink_Mount_Point.empty()) {
			Recreate_Media_Folder();
		}
		gui_print("You may need to reboot recovery to be able to use /data again.\n");
		return true;
	} else {
		Has_Data_Media = Save_Data_Media;
		LOGERR("Unable to format to remove encryption.\n");
		return false;
	}
	return false;
}

void TWPartition::Check_FS_Type() {
	const char* type;
	blkid_probe pr;

	if (Fstab_File_System == "yaffs2" || Fstab_File_System == "mtd" || Fstab_File_System == "bml" || Ignore_Blkid)
		return; // Running blkid on some mtd devices causes a massive crash or needs to be skipped

	Find_Actual_Block_Device();
	if (!Is_Present)
		return;

	pr = blkid_new_probe_from_filename(Actual_Block_Device.c_str());
	if (blkid_do_fullprobe(pr)) {
		blkid_free_probe(pr);
		LOGINFO("Can't probe device %s\n", Actual_Block_Device.c_str());
		return;
	}

	if (blkid_probe_lookup_value(pr, "TYPE", &type, NULL) < 0) { 
		blkid_free_probe(pr);
		LOGINFO("can't find filesystem on device %s\n", Actual_Block_Device.c_str());
		return;
	}

	Current_File_System = type;
	blkid_free_probe(pr);
}

bool TWPartition::Wipe_EXT23(string File_System) {
	if (!UnMount(true))
		return false;

	if (TWFunc::Path_Exists("/sbin/mke2fs")) {
		string command;

		gui_print("Formatting %s using mke2fs...\n", Display_Name.c_str());
		Find_Actual_Block_Device();
		command = "mke2fs -t " + File_System + " -m 0 " + Actual_Block_Device;
		LOGINFO("mke2fs command: %s\n", command.c_str());
		if (TWFunc::Exec_Cmd(command) == 0) {
			Current_File_System = File_System;
			Recreate_AndSec_Folder();
			gui_print("Done.\n");
			return true;
		} else {
			LOGERR("Unable to wipe '%s'.\n", Mount_Point.c_str());
			return false;
		}
	} else
		return Wipe_RMRF();

	return false;
}

bool TWPartition::Wipe_EXT4() {
	if (!UnMount(true))
		return false;

#if defined(HAVE_SELINUX) && defined(USE_EXT4)
	gui_print("Formatting %s using make_ext4fs function.\n", Display_Name.c_str());
	if (make_ext4fs(Actual_Block_Device.c_str(), Length, Mount_Point.c_str(), selinux_handle) != 0) {
		LOGERR("Unable to wipe '%s' using function call.\n", Mount_Point.c_str());
		return false;
	} else {
		#ifdef HAVE_SELINUX
		string sedir = Mount_Point + "/lost+found";
		PartitionManager.Mount_By_Path(sedir.c_str(), true);
		rmdir(sedir.c_str());
		mkdir(sedir.c_str(), S_IRWXU | S_IRWXG | S_IWGRP | S_IXGRP);
		#endif
		return true;
	}
#else
	if (TWFunc::Path_Exists("/sbin/make_ext4fs")) {
		string Command;

		gui_print("Formatting %s using make_ext4fs...\n", Display_Name.c_str());
		Find_Actual_Block_Device();
		Command = "make_ext4fs";
		if (!Is_Decrypted && Length != 0) {
			// Only use length if we're not decrypted
			char len[32];
			sprintf(len, "%i", Length);
			Command += " -l ";
			Command += len;
		}
		if (TWFunc::Path_Exists("/file_contexts")) {
			Command += " -S /file_contexts";
		}
		Command += " -a " + Mount_Point + " " + Actual_Block_Device;
		LOGINFO("make_ext4fs command: %s\n", Command.c_str());
		if (TWFunc::Exec_Cmd(Command) == 0) {
			Current_File_System = "ext4";
			Recreate_AndSec_Folder();
			gui_print("Done.\n");
			return true;
		} else {
			LOGERR("Unable to wipe '%s'.\n", Mount_Point.c_str());
			return false;
		}
	} else
		return Wipe_EXT23("ext4");
#endif
	return false;
}

bool TWPartition::Wipe_FAT() {
	string command;

	if (TWFunc::Path_Exists("/sbin/mkdosfs")) {
		if (!UnMount(true))
			return false;

		gui_print("Formatting %s using mkdosfs...\n", Display_Name.c_str());
		Find_Actual_Block_Device();
		command = "mkdosfs " + Actual_Block_Device;
		if (TWFunc::Exec_Cmd(command) == 0) {
			Current_File_System = "vfat";
			Recreate_AndSec_Folder();
			gui_print("Done.\n");
			return true;
		} else {
			LOGERR("Unable to wipe '%s'.\n", Mount_Point.c_str());
			return false;
		}
		return true;
	}
	else
		return Wipe_RMRF();

	return false;
}

bool TWPartition::Wipe_EXFAT() {
	string command;

	if (TWFunc::Path_Exists("/sbin/mkexfatfs")) {
		if (!UnMount(true))
			return false;

		gui_print("Formatting %s using mkexfatfs...\n", Display_Name.c_str());
		Find_Actual_Block_Device();
		command = "mkexfatfs " + Actual_Block_Device;
		if (TWFunc::Exec_Cmd(command) == 0) {
			Recreate_AndSec_Folder();
			gui_print("Done.\n");
			return true;
		} else {
			LOGERR("Unable to wipe '%s'.\n", Mount_Point.c_str());
			return false;
		}
		return true;
	}
	return false;
}

bool TWPartition::Wipe_MTD() {
	if (!UnMount(true))
		return false;

	gui_print("MTD Formatting \"%s\"\n", MTD_Name.c_str());

	mtd_scan_partitions();
	const MtdPartition* mtd = mtd_find_partition_by_name(MTD_Name.c_str());
	if (mtd == NULL) {
		LOGERR("No mtd partition named '%s'", MTD_Name.c_str());
		return false;
	}

	MtdWriteContext* ctx = mtd_write_partition(mtd);
	if (ctx == NULL) {
		LOGERR("Can't write '%s', failed to format.", MTD_Name.c_str());
		return false;
	}
	if (mtd_erase_blocks(ctx, -1) == -1) {
		mtd_write_close(ctx);
		LOGERR("Failed to format '%s'", MTD_Name.c_str());
		return false;
	}
	if (mtd_write_close(ctx) != 0) {
		LOGERR("Failed to close '%s'", MTD_Name.c_str());
		return false;
	}
	Current_File_System = "yaffs2";
	Recreate_AndSec_Folder();
	gui_print("Done.\n");
	return true;
}

bool TWPartition::Wipe_RMRF() {
	if (!Mount(true))
		return false;

	gui_print("Removing all files under '%s'\n", Mount_Point.c_str());
	TWFunc::removeDir(Mount_Point, true);
	Recreate_AndSec_Folder();
	return true;
}

bool TWPartition::Wipe_F2FS() {
	string command;

	if (TWFunc::Path_Exists("/sbin/mkfs.f2fs")) {
		if (!UnMount(true))
			return false;

		gui_print("Formatting %s using mkfs.f2fs...\n", Display_Name.c_str());
		Find_Actual_Block_Device();
		command = "mkfs.f2fs " + Actual_Block_Device;
		if (TWFunc::Exec_Cmd(command) == 0) {
			Recreate_AndSec_Folder();
			gui_print("Done.\n");
			return true;
		} else {
			LOGERR("Unable to wipe '%s'.\n", Mount_Point.c_str());
			return false;
		}
		return true;
	} else {
		gui_print("mkfs.f2fs binary not found, using rm -rf to wipe.\n");
		return Wipe_RMRF();
	}
	return false;
}

bool TWPartition::Wipe_Data_Without_Wiping_Media() {
	string dir;
	#ifdef HAVE_SELINUX
	fixPermissions perms;
	#endif

	// This handles wiping data on devices with "sdcard" in /data/media
	if (!Mount(true))
		return false;

	gui_print("Wiping data without wiping /data/media ...\n");

	DIR* d;
	d = opendir("/data");
	if (d != NULL) {
		struct dirent* de;
		while ((de = readdir(d)) != NULL) {
			if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)	 continue;
			// The media folder is the "internal sdcard"
			// The .layout_version file is responsible for determining whether 4.2 decides up upgrade
			// the media folder for multi-user.
			//TODO: convert this to use twrpDU.cpp
			if (strcmp(de->d_name, "media") == 0 || strcmp(de->d_name, ".layout_version") == 0)   continue;

			dir = "/data/";
			dir.append(de->d_name);
			if (de->d_type == DT_DIR) {
				TWFunc::removeDir(dir, false);
			} else if (de->d_type == DT_REG || de->d_type == DT_LNK || de->d_type == DT_FIFO || de->d_type == DT_SOCK) {
				if (!unlink(dir.c_str()))
					LOGINFO("Unable to unlink '%s'\n", dir.c_str());
			}
		}
		closedir(d);

		#ifdef HAVE_SELINUX
		perms.fixDataInternalContexts();
		#endif

		gui_print("Done.\n");
		return true;
	}
	gui_print("Dirent failed to open /data, error!\n");
	return false;
}

bool TWPartition::Backup_Tar(string backup_folder) {
	char back_name[255], split_index[5];
	string Full_FileName, Split_FileName, Tar_Args, Command;
	int use_compression, use_encryption = 0, index, backup_count;
	struct stat st;
	unsigned long long total_bsize = 0, file_size;
	twrpTar tar;
	vector <string> files;

	if (!Mount(true))
		return false;

	TWFunc::GUI_Operation_Text(TW_BACKUP_TEXT, Backup_Display_Name, "Backing Up");
	gui_print("Backing up %s...\n", Backup_Display_Name.c_str());

	DataManager::GetValue(TW_USE_COMPRESSION_VAR, use_compression);
	tar.use_compression = use_compression;
	//exclude Google Music Cache
	vector<string> excludedirs = du.get_absolute_dirs();
	for (int i = 0; i < excludedirs.size(); ++i) {
		tar.setexcl(excludedirs.at(i));
	}
#ifndef TW_EXCLUDE_ENCRYPTED_BACKUPS
	DataManager::GetValue("tw_encrypt_backup", use_encryption);
	if (use_encryption && Can_Encrypt_Backup) {
		tar.use_encryption = use_encryption;
		if (Use_Userdata_Encryption)
			tar.userdata_encryption = use_encryption;
	} else {
		use_encryption = false;
	}
#endif

	sprintf(back_name, "%s.%s.win", Backup_Name.c_str(), Current_File_System.c_str());
	Backup_FileName = back_name;
	Full_FileName = backup_folder + "/" + Backup_FileName;
	tar.has_data_media = Has_Data_Media;
	Full_FileName = backup_folder + "/" + Backup_FileName;
	tar.setdir(Backup_Path);
	tar.setfn(Full_FileName);
	tar.setsize(Backup_Size);
	if (tar.createTarFork() != 0)
		return false;
	return true;
}

bool TWPartition::Backup_DD(string backup_folder) {
	char back_name[255], backup_size[32];
	string Full_FileName, Command, DD_BS;
	int use_compression;

	sprintf(backup_size, "%llu", Backup_Size);
	DD_BS = backup_size;

	TWFunc::GUI_Operation_Text(TW_BACKUP_TEXT, Display_Name, "Backing Up");
	gui_print("Backing up %s...\n", Display_Name.c_str());

	sprintf(back_name, "%s.%s.win", Backup_Name.c_str(), Current_File_System.c_str());
	Backup_FileName = back_name;

	Full_FileName = backup_folder + "/" + Backup_FileName;

	Command = "dd if=" + Actual_Block_Device + " of='" + Full_FileName + "'" + " bs=" + DD_BS + "c count=1";
	LOGINFO("Backup command: '%s'\n", Command.c_str());
	TWFunc::Exec_Cmd(Command);
	if (TWFunc::Get_File_Size(Full_FileName) == 0) {
		LOGERR("Backup file size for '%s' is 0 bytes.\n", Full_FileName.c_str());
		return false;
	}
	return true;
}

bool TWPartition::Backup_Dump_Image(string backup_folder) {
	char back_name[255];
	string Full_FileName, Command;
	int use_compression;

	TWFunc::GUI_Operation_Text(TW_BACKUP_TEXT, Display_Name, "Backing Up");
	gui_print("Backing up %s...\n", Display_Name.c_str());

	sprintf(back_name, "%s.%s.win", Backup_Name.c_str(), Current_File_System.c_str());
	Backup_FileName = back_name;

	Full_FileName = backup_folder + "/" + Backup_FileName;

	Command = "dump_image " + MTD_Name + " '" + Full_FileName + "'";
	LOGINFO("Backup command: '%s'\n", Command.c_str());
	TWFunc::Exec_Cmd(Command);
	if (TWFunc::Get_File_Size(Full_FileName) == 0) {
		// Actual size may not match backup size due to bad blocks on MTD devices so just check for 0 bytes
		LOGERR("Backup file size for '%s' is 0 bytes.\n", Full_FileName.c_str());
		return false;
	}
	return true;
}

bool TWPartition::Restore_Tar(string restore_folder, string Restore_File_System) {
	string Full_FileName, Command;
	int index = 0;
	char split_index[5];

	if (Has_Android_Secure) {
		if (!Wipe_AndSec())
			return false;
	} else {
		gui_print("Wiping %s...\n", Display_Name.c_str());
		if (!Wipe(Restore_File_System))
			return false;
	}
	TWFunc::GUI_Operation_Text(TW_RESTORE_TEXT, Backup_Display_Name, "Restoring");
	gui_print("Restoring %s...\n", Backup_Display_Name.c_str());

	if (!Mount(true))
		return false;

	Full_FileName = restore_folder + "/" + Backup_FileName;
	/*if (!TWFunc::Path_Exists(Full_FileName)) {
		if (!TWFunc::Path_Exists(Full_FileName)) {
			// Backup is multiple archives
			LOGINFO("Backup is multiple archives.\n");
			sprintf(split_index, "%03i", index);
			Full_FileName = restore_folder + "/" + Backup_FileName + split_index;
			while (TWFunc::Path_Exists(Full_FileName)) {
				index++;
				gui_print("Restoring archive %i...\n", index);
				LOGINFO("Restoring '%s'...\n", Full_FileName.c_str());
				twrpTar tar;
				tar.setdir("/");
				tar.setfn(Full_FileName);
				if (tar.extractTarFork() != 0)
					return false;
				sprintf(split_index, "%03i", index);
				Full_FileName = restore_folder + "/" + Backup_FileName + split_index;
			}
			if (index == 0) {
				LOGERR("Error locating restore file: '%s'\n", Full_FileName.c_str());
				return false;
			}
		}
	} else {*/
		twrpTar tar;
		tar.setdir(Backup_Path);
		tar.setfn(Full_FileName);
		tar.backup_name = Backup_Name;
		if (tar.extractTarFork() != 0)
			return false;
	//}
	return true;
}

bool TWPartition::Restore_DD(string restore_folder) {
	string Full_FileName, Command;

	TWFunc::GUI_Operation_Text(TW_RESTORE_TEXT, Display_Name, "Restoring");
	Full_FileName = restore_folder + "/" + Backup_FileName;

	if (!Find_Partition_Size()) {
		LOGERR("Unable to find partition size for '%s'\n", Mount_Point.c_str());
		return false;
	}
	unsigned long long backup_size = TWFunc::Get_File_Size(Full_FileName);
	if (backup_size > Size) {
		LOGERR("Size (%iMB) of backup '%s' is larger than target device '%s' (%iMB)\n",
			(int)(backup_size / 1048576LLU), Full_FileName.c_str(),
			Actual_Block_Device.c_str(), (int)(Size / 1048576LLU));
		return false;
	}

	gui_print("Restoring %s...\n", Display_Name.c_str());
	Command = "dd bs=4096 if='" + Full_FileName + "' of=" + Actual_Block_Device;
	LOGINFO("Restore command: '%s'\n", Command.c_str());
	TWFunc::Exec_Cmd(Command);
	return true;
}

bool TWPartition::Restore_Flash_Image(string restore_folder) {
	string Full_FileName, Command;

	gui_print("Restoring %s...\n", Display_Name.c_str());
	Full_FileName = restore_folder + "/" + Backup_FileName;
	// Sometimes flash image doesn't like to flash due to the first 2KB matching, so we erase first to ensure that it flashes
	Command = "erase_image " + MTD_Name;
	LOGINFO("Erase command: '%s'\n", Command.c_str());
	TWFunc::Exec_Cmd(Command);
	Command = "flash_image " + MTD_Name + " '" + Full_FileName + "'";
	LOGINFO("Restore command: '%s'\n", Command.c_str());
	TWFunc::Exec_Cmd(Command);
	return true;
}

bool TWPartition::Update_Size(bool Display_Error) {
	bool ret = false, Was_Already_Mounted = false;

	if (!Can_Be_Mounted && !Is_Encrypted)
		return false;

	Was_Already_Mounted = Is_Mounted();
	if (Removable || Is_Encrypted) {
		if (!Mount(false))
			return true;
	} else if (!Mount(Display_Error))
		return false;

	ret = Get_Size_Via_statfs(Display_Error);
	if (!ret || Size == 0) {
		if (!Get_Size_Via_df(Display_Error)) {
			if (!Was_Already_Mounted)
				UnMount(false);
			return false;
		}
	}

	if (Has_Data_Media) {
		if (Mount(Display_Error)) {
			unsigned long long data_media_used, actual_data;
			du.add_relative_dir("media");
			Used = du.Get_Folder_Size("/data");
			du.clear_relative_dir("media");
			Backup_Size = Used;
			int bak = (int)(Used / 1048576LLU);
			int fre = (int)(Free / 1048576LLU);
			LOGINFO("Data backup size is %iMB, free: %iMB.\n", bak, fre);
		} else {
			if (!Was_Already_Mounted)
				UnMount(false);
			return false;
		}
	} else if (Has_Android_Secure) {
		if (Mount(Display_Error))
			Backup_Size = du.Get_Folder_Size(Backup_Path);
		else {
			if (!Was_Already_Mounted)
				UnMount(false);
			return false;
		}
	}
	if (!Was_Already_Mounted)
		UnMount(false);
	return true;
}

void TWPartition::Find_Actual_Block_Device(void) {
	if (Is_Decrypted) {
		Actual_Block_Device = Decrypted_Block_Device;
		if (TWFunc::Path_Exists(Primary_Block_Device))
			Is_Present = true;
	} else if (TWFunc::Path_Exists(Primary_Block_Device)) {
		Is_Present = true;
		Actual_Block_Device = Primary_Block_Device;
		return;
	}
	if (Is_Decrypted) {
	} else if (!Alternate_Block_Device.empty() && TWFunc::Path_Exists(Alternate_Block_Device)) {
		Actual_Block_Device = Alternate_Block_Device;
		Is_Present = true;
	} else {
		Is_Present = false;
	}
}

void TWPartition::Recreate_Media_Folder(void) {
	string Command;

	#ifdef HAVE_SELINUX
	fixPermissions perms;
	#endif

	if (!Mount(true)) {
		LOGERR("Unable to recreate /data/media folder.\n");
	} else if (!TWFunc::Path_Exists("/data/media")) {
		PartitionManager.Mount_By_Path(Symlink_Mount_Point, true);
		LOGINFO("Recreating /data/media folder.\n");
		mkdir("/data/media", S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH); 
		#ifdef HAVE_SELINUX
		perms.fixDataInternalContexts();
		#endif
		PartitionManager.UnMount_By_Path(Symlink_Mount_Point, true);
	}
}

void TWPartition::Recreate_AndSec_Folder(void) {
	if (!Has_Android_Secure)
		return;
	LOGINFO("Creating %s: %s\n", Backup_Display_Name.c_str(), Symlink_Path.c_str());
	if (!Mount(true)) {
		LOGERR("Unable to recreate %s folder.\n", Backup_Name.c_str());
	} else if (!TWFunc::Path_Exists(Symlink_Path)) {
		LOGINFO("Recreating %s folder.\n", Backup_Name.c_str());
		PartitionManager.Mount_By_Path(Symlink_Mount_Point, true);
		mkdir(Symlink_Path.c_str(), S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH); 
		PartitionManager.UnMount_By_Path(Symlink_Mount_Point, true);
	}
}
