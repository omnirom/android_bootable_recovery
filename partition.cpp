/*
	Copyright 2013 to 2016 TeamWin
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
#include <libgen.h>
#include <zlib.h>
#include <iostream>
#include <sstream>
#include <sys/param.h>
#include <fcntl.h>

#ifdef TW_INCLUDE_CRYPTO
	#include "cutils/properties.h"
#endif

#include "libblkid/include/blkid.h"
#include "variables.h"
#include "twcommon.h"
#include "partitions.hpp"
#include "data.hpp"
#include "twrp-functions.hpp"
#include "twrpDigest.hpp"
#include "twrpTar.hpp"
#include "twrpDU.hpp"
#include "infomanager.hpp"
#include "set_metadata.h"
#include "gui/gui.hpp"
#include "adbbu/libtwadbbu.hpp"
extern "C" {
	#include "mtdutils/mtdutils.h"
	#include "mtdutils/mounts.h"
#ifdef USE_EXT4
	#include "make_ext4fs.h"
#endif

#ifdef TW_INCLUDE_CRYPTO
	#include "crypto/lollipop/cryptfs.h"
	#include "gpt/gpt.h"
#else
	#define CRYPT_FOOTER_OFFSET 0x4000
#endif
}
#ifdef HAVE_SELINUX
#include "selinux/selinux.h"
#include <selinux/label.h>
#endif
#ifdef HAVE_CAPABILITIES
#include <sys/capability.h>
#include <sys/xattr.h>
#include <linux/xattr.h>
#endif
#include <sparse_format.h>
#include "progresstracking.hpp"

using namespace std;

extern struct selabel_handle *selinux_handle;
extern bool datamedia;

struct flag_list {
	const char *name;
	unsigned flag;
};

const struct flag_list mount_flags[] = {
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

enum TW_FSTAB_FLAGS {
	TWFLAG_DEFAULTS, // Retain position
	TWFLAG_ANDSEC,
	TWFLAG_BACKUP,
	TWFLAG_BACKUPNAME,
	TWFLAG_BLOCKSIZE,
	TWFLAG_CANBEWIPED,
	TWFLAG_CANENCRYPTBACKUP,
	TWFLAG_DISPLAY,
	TWFLAG_ENCRYPTABLE,
	TWFLAG_FLASHIMG,
	TWFLAG_FORCEENCRYPT,
	TWFLAG_FSFLAGS,
	TWFLAG_IGNOREBLKID,
	TWFLAG_LENGTH,
	TWFLAG_MOUNTTODECRYPT,
	TWFLAG_REMOVABLE,
	TWFLAG_RETAINLAYOUTVERSION,
	TWFLAG_SETTINGSSTORAGE,
	TWFLAG_STORAGE,
	TWFLAG_STORAGENAME,
	TWFLAG_SUBPARTITIONOF,
	TWFLAG_SYMLINK,
	TWFLAG_USERDATAENCRYPTBACKUP,
	TWFLAG_USERMRF,
	TWFLAG_WIPEDURINGFACTORYRESET,
	TWFLAG_WIPEINGUI,
};

/* Flags without a trailing '=' are considered dual format flags and can be
 * written as either 'flagname' or 'flagname=', where the character following
 * the '=' is Y,y,1 for true and false otherwise.
 */
const struct flag_list tw_flags[] = {
	{ "andsec",                 TWFLAG_ANDSEC },
	{ "backup",                 TWFLAG_BACKUP },
	{ "backupname=",            TWFLAG_BACKUPNAME },
	{ "blocksize=",             TWFLAG_BLOCKSIZE },
	{ "canbewiped",             TWFLAG_CANBEWIPED },
	{ "canencryptbackup",       TWFLAG_CANENCRYPTBACKUP },
	{ "defaults",               TWFLAG_DEFAULTS },
	{ "display=",               TWFLAG_DISPLAY },
	{ "encryptable=",           TWFLAG_ENCRYPTABLE },
	{ "flashimg",               TWFLAG_FLASHIMG },
	{ "forceencrypt=",          TWFLAG_FORCEENCRYPT },
	{ "fsflags=",               TWFLAG_FSFLAGS },
	{ "ignoreblkid",            TWFLAG_IGNOREBLKID },
	{ "length=",                TWFLAG_LENGTH },
	{ "mounttodecrypt",         TWFLAG_MOUNTTODECRYPT },
	{ "removable",              TWFLAG_REMOVABLE },
	{ "retainlayoutversion",    TWFLAG_RETAINLAYOUTVERSION },
	{ "settingsstorage",        TWFLAG_SETTINGSSTORAGE },
	{ "storage",                TWFLAG_STORAGE },
	{ "storagename=",           TWFLAG_STORAGENAME },
	{ "subpartitionof=",        TWFLAG_SUBPARTITIONOF },
	{ "symlink=",               TWFLAG_SYMLINK },
	{ "userdataencryptbackup",  TWFLAG_USERDATAENCRYPTBACKUP },
	{ "usermrf",                TWFLAG_USERMRF },
	{ "wipeduringfactoryreset", TWFLAG_WIPEDURINGFACTORYRESET },
	{ "wipeingui",              TWFLAG_WIPEINGUI },
	{ 0,                        0 },
};

TWPartition::TWPartition() {
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
	Mount_To_Decrypt = false;
	Decrypted_Block_Device = "";
	Display_Name = "";
	Backup_Display_Name = "";
	Storage_Name = "";
	Backup_Name = "";
	Backup_FileName = "";
	MTD_Name = "";
	Backup_Method = BM_NONE;
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
	Crypto_Key_Location = "footer";
	MTP_Storage_ID = 0;
	Can_Flash_Img = false;
	Mount_Read_Only = false;
	Is_Adopted_Storage = false;
	Adopted_GUID = "";
}

TWPartition::~TWPartition(void) {
	// Do nothing
}

bool TWPartition::Process_Fstab_Line(const char *fstab_line, bool Display_Error) {
	char full_line[MAX_FSTAB_LINE_LENGTH];
	char twflags[MAX_FSTAB_LINE_LENGTH] = "";
	char* ptr;
	int line_len = strlen(fstab_line), index = 0, item_index = 0;
	bool skip = false;

	strlcpy(full_line, fstab_line, sizeof(full_line));
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
					LOGERR("Invalid block device '%s' in fstab line '%s'", ptr, fstab_line);
				else
					LOGINFO("Invalid block device '%s' in fstab line '%s'", ptr, fstab_line);
				return false;
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
				strlcpy(twflags, ptr, sizeof(twflags));
			} else if (strlen(ptr) == 4 && (strncmp(ptr, "NULL", 4) == 0 || strncmp(ptr, "null", 4) == 0 || strncmp(ptr, "null", 4) == 0)) {
				// Do nothing
			} else {
				// Unhandled data
				LOGINFO("Unhandled fstab information '%s' in fstab line '%s'\n", ptr, fstab_line);
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
		return false;
	} else if (Is_File_System(Fstab_File_System)) {
		Find_Actual_Block_Device();
		Setup_File_System(Display_Error);
		if (Mount_Point == "/system") {
			Display_Name = "System";
			Backup_Display_Name = Display_Name;
			Storage_Name = Display_Name;
			Wipe_Available_in_GUI = true;
			Can_Be_Backed_Up = true;
			Mount_Read_Only = true;
		} else if (Mount_Point == "/data") {
			Display_Name = "Data";
			Backup_Display_Name = Display_Name;
			Storage_Name = Display_Name;
			Wipe_Available_in_GUI = true;
			Wipe_During_Factory_Reset = true;
			Can_Be_Backed_Up = true;
			Can_Encrypt_Backup = true;
			Use_Userdata_Encryption = true;
		} else if (Mount_Point == "/cache") {
			Display_Name = "Cache";
			Backup_Display_Name = Display_Name;
			Storage_Name = Display_Name;
			Wipe_Available_in_GUI = true;
			Wipe_During_Factory_Reset = true;
			Can_Be_Backed_Up = true;
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
		} else if (Mount_Point == "/vendor") {
			Display_Name = "Vendor";
			Backup_Display_Name = Display_Name;
			Storage_Name = Display_Name;
			Mount_Read_Only = true;
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
			Can_Flash_Img = true;
		} else if (Mount_Point == "/recovery") {
			Display_Name = "Recovery";
			Backup_Display_Name = Display_Name;
			Can_Flash_Img = true;
		} else if (Mount_Point == "/system_image") {
			Display_Name = "System Image";
			Backup_Display_Name = Display_Name;
			Can_Flash_Img = true;
			Can_Be_Backed_Up = true;
		} else if (Mount_Point == "/vendor_image") {
			Display_Name = "Vendor Image";
			Backup_Display_Name = Display_Name;
			Can_Flash_Img = true;
			Can_Be_Backed_Up = true;
		}
	}

	// Process TWRP fstab flags
	if (strlen(twflags) > 0) {
		string Prev_Display_Name = Display_Name;
		string Prev_Storage_Name = Storage_Name;
		string Prev_Backup_Display_Name = Backup_Display_Name;
		Display_Name = "";
		Storage_Name = "";
		Backup_Display_Name = "";

		Process_TW_Flags(twflags, Display_Error);

		bool has_display_name = !Display_Name.empty();
		bool has_storage_name = !Storage_Name.empty();
		bool has_backup_name = !Backup_Display_Name.empty();
		if (!has_display_name) Display_Name = Prev_Display_Name;
		if (!has_storage_name) Storage_Name = Prev_Storage_Name;
		if (!has_backup_name) Backup_Display_Name = Prev_Backup_Display_Name;

		if (has_display_name && !has_storage_name)
			Storage_Name = Display_Name;
		if (!has_display_name && has_storage_name)
			Display_Name = Storage_Name;
		if (has_display_name && !has_backup_name && Backup_Display_Name != "Android Secure")
			Backup_Display_Name = Display_Name;
		if (!has_display_name && has_backup_name)
			Display_Name = Backup_Display_Name;
	}
	return true;
}

void TWPartition::Partition_Post_Processing(bool Display_Error) {
	if (Mount_Point == "/data")
		Setup_Data_Partition(Display_Error);
	else if (Mount_Point == "/cache")
		Setup_Cache_Partition(Display_Error);
}

void TWPartition::Setup_Data_Partition(bool Display_Error) {
	if (Mount_Point != "/data")
		return;

	// Ensure /data is not mounted as tmpfs for qcom hardware decrypt
	UnMount(false);

#ifdef TW_INCLUDE_CRYPTO
	if (datamedia)
		Setup_Data_Media();
	Can_Be_Encrypted = true;
	char crypto_blkdev[255];
	property_get("ro.crypto.fs_crypto_blkdev", crypto_blkdev, "error");
	if (strcmp(crypto_blkdev, "error") != 0) {
		DataManager::SetValue(TW_IS_DECRYPTED, 1);
		Is_Encrypted = true;
		Is_Decrypted = true;
		Decrypted_Block_Device = crypto_blkdev;
		LOGINFO("Data already decrypted, new block device: '%s'\n", crypto_blkdev);
	} else if (!Mount(false)) {
		if (Is_Present) {
			set_partition_data(Actual_Block_Device.c_str(), Crypto_Key_Location.c_str(), Fstab_File_System.c_str());
			if (cryptfs_check_footer() == 0) {
				Is_Encrypted = true;
				Is_Decrypted = false;
				Can_Be_Mounted = false;
				Current_File_System = "emmc";
				Setup_Image(Display_Error);
				DataManager::SetValue(TW_IS_ENCRYPTED, 1);
				DataManager::SetValue(TW_CRYPTO_PWTYPE, cryptfs_get_password_type());
				DataManager::SetValue(TW_CRYPTO_PASSWORD, "");
				DataManager::SetValue("tw_crypto_display", "");
			} else {
				gui_err("mount_data_footer=Could not mount /data and unable to find crypto footer.");
			}
		} else {
			LOGERR("Primary block device '%s' for mount point '%s' is not present!\n", Primary_Block_Device.c_str(), Mount_Point.c_str());
		}
	} else {
		// Filesystem is not encrypted and the mount succeeded, so return to
		// the original unmounted state
		UnMount(false);
	}
	if (datamedia && (!Is_Encrypted || (Is_Encrypted && Is_Decrypted))) {
		Setup_Data_Media();
		Recreate_Media_Folder();
	}
#else
	if (datamedia) {
		Setup_Data_Media();
		Recreate_Media_Folder();
	}
#endif
}

void TWPartition::Setup_Cache_Partition(bool Display_Error __unused) {
	if (Mount_Point != "/cache")
		return;

	if (!Mount(true))
		return;

	if (!TWFunc::Path_Exists("/cache/recovery/.")) {
		LOGINFO("Recreating /cache/recovery folder\n");
		if (mkdir("/cache/recovery", S_IRWXU | S_IRWXG | S_IWGRP | S_IXGRP) != 0)
			LOGERR("Could not create /cache/recovery\n");
	}
}

void TWPartition::Process_FS_Flags(const char *str) {
	char *options = strdup(str);
	char *ptr, *savep;

	Mount_Options = "";

	// Avoid issues with potentially nested strtok by using strtok_r
	ptr = strtok_r(options, ",", &savep);
	while (ptr) {
		const struct flag_list* mount_flag = mount_flags;

		for (; mount_flag->name; mount_flag++) {
			// mount_flags are never postfixed by '=',
			// so only match identical strings (including length)
			if (strcmp(ptr, mount_flag->name) == 0) {
				Mount_Flags |= mount_flag->flag;
				break;
			}
		}

		if (mount_flag->flag == MS_RDONLY)
			Mount_Read_Only = true;

		if (mount_flag->name != 0) {
			if (!Mount_Options.empty())
				Mount_Options += ",";
			Mount_Options += mount_flag->name;
		} else {
			LOGINFO("Unhandled mount flag: '%s'\n", ptr);
		}

		ptr = strtok_r(NULL, ",", &savep);
	}
	free(options);
}

void TWPartition::Apply_TW_Flag(const unsigned flag, const char* str, const bool val) {
	switch (flag) {
		case TWFLAG_ANDSEC:
			Has_Android_Secure = val;
			break;
		case TWFLAG_BACKUP:
			Can_Be_Backed_Up = val;
			break;
		case TWFLAG_BACKUPNAME:
			Backup_Display_Name = str;
			break;
		case TWFLAG_BLOCKSIZE:
			Format_Block_Size = (unsigned long)(atol(str));
			break;
		case TWFLAG_CANBEWIPED:
			Can_Be_Wiped = val;
			break;
		case TWFLAG_CANENCRYPTBACKUP:
			Can_Encrypt_Backup = val;
			break;
		case TWFLAG_DEFAULTS:
			// Do nothing
			break;
		case TWFLAG_DISPLAY:
			Display_Name = str;
			break;
		case TWFLAG_ENCRYPTABLE:
		case TWFLAG_FORCEENCRYPT:
			Crypto_Key_Location = str;
			break;
		case TWFLAG_FLASHIMG:
			Can_Flash_Img = val;
			break;
		case TWFLAG_FSFLAGS:
			Process_FS_Flags(str);
			break;
		case TWFLAG_IGNOREBLKID:
			Ignore_Blkid = val;
			break;
		case TWFLAG_LENGTH:
			Length = atoi(str);
			break;
		case TWFLAG_MOUNTTODECRYPT:
			Mount_To_Decrypt = val;
			break;
		case TWFLAG_REMOVABLE:
			Removable = val;
			break;
		case TWFLAG_RETAINLAYOUTVERSION:
			Retain_Layout_Version = val;
			break;
		case TWFLAG_SETTINGSSTORAGE:
			Is_Settings_Storage = val;
			if (Is_Settings_Storage)
				Is_Storage = true;
			break;
		case TWFLAG_STORAGE:
			Is_Storage = val;
			break;
		case TWFLAG_STORAGENAME:
			Storage_Name = str;
			break;
		case TWFLAG_SUBPARTITIONOF:
			Is_SubPartition = true;
			SubPartition_Of = str;
			break;
		case TWFLAG_SYMLINK:
			Symlink_Path = str;
			break;
		case TWFLAG_USERDATAENCRYPTBACKUP:
			Use_Userdata_Encryption = val;
			if (Use_Userdata_Encryption)
				Can_Encrypt_Backup = true;
			break;
		case TWFLAG_USERMRF:
			Use_Rm_Rf = val;
			break;
		case TWFLAG_WIPEDURINGFACTORYRESET:
			Wipe_During_Factory_Reset = val;
			if (Wipe_During_Factory_Reset) {
				Can_Be_Wiped = true;
				Wipe_Available_in_GUI = true;
			}
			break;
		case TWFLAG_WIPEINGUI:
			Wipe_Available_in_GUI = val;
			if (Wipe_Available_in_GUI)
				Can_Be_Wiped = true;
			break;
		default:
			// Should not get here
			LOGINFO("Flag identified for processing, but later unmatched: %i\n", flag);
			break;
	}
}

void TWPartition::Process_TW_Flags(char *flags, bool Display_Error) {
	char separator[2] = {'\n', 0};
	char *ptr, *savep;

	// Semicolons within double-quotes are not forbidden, so replace
	// only the semicolons intended as separators with '\n' for strtok
	for (unsigned i = 0, skip = 0; i < strlen(flags); i++) {
		if (flags[i] == '\"')
			skip = !skip;
		if (!skip && flags[i] == ';')
			flags[i] = separator[0];
	}

	// Avoid issues with potentially nested strtok by using strtok_r
	ptr = strtok_r(flags, separator, &savep);
	while (ptr) {
		int ptr_len = strlen(ptr);
		const struct flag_list* tw_flag = tw_flags;

		for (; tw_flag->name; tw_flag++) {
			int flag_len = strlen(tw_flag->name);

			if (strncmp(ptr, tw_flag->name, flag_len) == 0) {
				bool flag_val = false;

				if (ptr_len > flag_len && (tw_flag->name)[flag_len-1] != '='
						&& ptr[flag_len] != '=') {
					// Handle flags with same starting string
					// (e.g. backup and backupname)
					continue;
				} else if (ptr_len > flag_len && ptr[flag_len] == '=') {
					// Handle flags with dual format: Part 1
					// (e.g. backup and backup=y. backup=y handled here)
					ptr += flag_len + 1;
					TWFunc::Strip_Quotes(ptr);
					// Skip flags with empty argument
					// (e.g. backup=)
					if (strlen(ptr) == 0) {
						LOGINFO("Flag missing argument or should not include '=': %s=\n", tw_flag->name);
						break;
					}
					flag_val = strchr("yY1", *ptr) != NULL;
				} else if (ptr_len == flag_len
						&& (tw_flag->name)[flag_len-1] == '=') {
					// Skip flags missing argument after =
					// (e.g. backupname=)
					LOGINFO("Flag missing argument: %s\n", tw_flag->name);
					break;
				} else if (ptr_len > flag_len
						&& (tw_flag->name)[flag_len-1] == '=') {
					// Handle arguments to flags
					// (e.g. backupname="My Stuff")
					ptr += flag_len;
					TWFunc::Strip_Quotes(ptr);
					// Skip flags with empty argument
					// (e.g. backupname="")
					if (strlen(ptr) == 0) {
						LOGINFO("Flag missing argument: %s\n", tw_flag->name);
						break;
					}
				} else if (ptr_len == flag_len) {
					// Handle flags with dual format: Part 2
					// (e.g. backup and backup=y. backup handled here)
					flag_val = true;
				} else {
					LOGINFO("Flag matched, but could not be processed: %s\n", ptr);
					break;
				}

				Apply_TW_Flag(tw_flag->flag, ptr, flag_val);
				break;
			}
		}
		if (tw_flag->name == 0) {
			if (Display_Error)
				LOGERR("Unhandled flag: '%s'\n", ptr);
			else
				LOGINFO("Unhandled flag: '%s'\n", ptr);
		}
		ptr = strtok_r(NULL, separator, &savep);
	}
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
				gui_msg(Msg(msg::kError, "create_folder_strerr=Can not create '{1}' folder ({2}).")(Path)(strerror(errno)));
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
	Backup_Method = BM_FILES;
}

void TWPartition::Setup_Image(bool Display_Error) {
	Display_Name = Mount_Point.substr(1, Mount_Point.size() - 1);
	Backup_Name = Display_Name;
	if (Current_File_System == "emmc")
		Backup_Method = BM_DD;
	else if (Current_File_System == "mtd" || Current_File_System == "bml")
		Backup_Method = BM_FLASH_UTILS;
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
	Mount_Storage_Retry(true);
}

void TWPartition::Setup_Data_Media() {
	LOGINFO("Setting up '%s' as data/media emulated storage.\n", Mount_Point.c_str());
	if (Storage_Name.empty() || Storage_Name == "Data")
		Storage_Name = "Internal Storage";
	Has_Data_Media = true;
	Is_Storage = true;
	Storage_Path = Mount_Point + "/media";
	Symlink_Path = Storage_Path;
	if (Mount_Point == "/data") {
		Is_Settings_Storage = true;
		if (strcmp(EXPAND(TW_EXTERNAL_STORAGE_PATH), "/sdcard") == 0) {
			Make_Dir("/emmc", false);
			Symlink_Mount_Point = "/emmc";
		} else {
			Make_Dir("/sdcard", false);
			Symlink_Mount_Point = "/sdcard";
		}
		if (Mount(false) && TWFunc::Path_Exists(Mount_Point + "/media/0")) {
			Storage_Path = Mount_Point + "/media/0";
			Symlink_Path = Storage_Path;
			DataManager::SetValue(TW_INTERNAL_PATH, Mount_Point + "/media/0");
			UnMount(true);
		}
		DataManager::SetValue("tw_has_internal", 1);
		DataManager::SetValue("tw_has_data_media", 1);
		du.add_absolute_dir(Mount_Point + "/misc/vold");
		du.add_absolute_dir(Mount_Point + "/.layout_version");
		du.add_absolute_dir(Mount_Point + "/system/storage.xml");
	} else {
		if (Mount(true) && TWFunc::Path_Exists(Mount_Point + "/media/0")) {
			Storage_Path = Mount_Point + "/media/0";
			Symlink_Path = Storage_Path;
			UnMount(true);
		}
	}
	du.add_absolute_dir(Mount_Point + "/media");
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

bool TWPartition::Mount_Storage_Retry(bool Display_Error) {
	// On some devices, storage doesn't want to mount right away, retry and sleep
	if (!Mount(Display_Error)) {
		int retry_count = 5;
		while (retry_count > 0 && !Mount(false)) {
			usleep(500000);
			retry_count--;
		}
		return Mount(Display_Error);
	}
	return true;
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

unsigned long long TWPartition::IOCTL_Get_Block_Size() {
	Find_Actual_Block_Device();

	return TWFunc::IOCTL_Get_Block_Size(Actual_Block_Device.c_str());
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

			sscanf(line, "%s %lx %*x %*u %s", label, &size, device);

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

	unsigned long long ioctl_size = IOCTL_Get_Block_Size();
	if (ioctl_size) {
		Size = ioctl_size;
		return true;
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

bool TWPartition::Is_File_System_Writable(void) {
	if (!Is_File_System(Current_File_System) || !Is_Mounted())
		return false;

	string test_path = Mount_Point + "/.";
	return (access(test_path.c_str(), W_OK) == 0);
}

bool TWPartition::Mount(bool Display_Error) {
	int exfat_mounted = 0;
	unsigned long flags = Mount_Flags;

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

	if (Current_File_System == "ntfs" && (TWFunc::Path_Exists("/sbin/ntfs-3g") || TWFunc::Path_Exists("/sbin/mount.ntfs"))) {
		string cmd;
		string Ntfsmount_Binary = "";

		if (TWFunc::Path_Exists("/sbin/ntfs-3g"))
			Ntfsmount_Binary = "ntfs-3g";
		else if (TWFunc::Path_Exists("/sbin/mount.ntfs"))
			Ntfsmount_Binary = "mount.ntfs";

		if (Mount_Read_Only)
			cmd = "/sbin/" + Ntfsmount_Binary + " -o ro " + Actual_Block_Device + " " + Mount_Point;
		else
			cmd = "/sbin/" + Ntfsmount_Binary + " " + Actual_Block_Device + " " + Mount_Point;
		LOGINFO("cmd: '%s'\n", cmd.c_str());

		if (TWFunc::Exec_Cmd(cmd) == 0) {
			return true;
		} else {
			LOGINFO("ntfs-3g failed to mount, trying regular mount method.\n");
		}
	}

	if (Mount_Read_Only)
		flags |= MS_RDONLY;

	if (Fstab_File_System == "yaffs2") {
		// mount an MTD partition as a YAFFS2 filesystem.
		flags = MS_NOATIME | MS_NODEV | MS_NODIRATIME;
		if (Mount_Read_Only)
			flags |= MS_RDONLY;
		if (mount(Actual_Block_Device.c_str(), Mount_Point.c_str(), Fstab_File_System.c_str(), flags, NULL) < 0) {
			if (mount(Actual_Block_Device.c_str(), Mount_Point.c_str(), Fstab_File_System.c_str(), flags | MS_RDONLY, NULL) < 0) {
				if (Display_Error)
					gui_msg(Msg(msg::kError, "fail_mount=Failed to mount '{1}' ({2})")(Mount_Point)(strerror(errno)));
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
					gui_msg(Msg(msg::kError, "fail_mount=Failed to mount '{1}' ({2})")(Mount_Point)(strerror(errno)));
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
	}

	string mount_fs = Current_File_System;
	if (Current_File_System == "exfat" && TWFunc::Path_Exists("/sys/module/texfat"))
		mount_fs = "texfat";

	if (!exfat_mounted &&
		mount(Actual_Block_Device.c_str(), Mount_Point.c_str(), mount_fs.c_str(), flags, Mount_Options.c_str()) != 0 &&
		mount(Actual_Block_Device.c_str(), Mount_Point.c_str(), mount_fs.c_str(), flags, NULL) != 0) {
#ifdef TW_NO_EXFAT_FUSE
		if (Current_File_System == "exfat") {
			LOGINFO("Mounting exfat failed, trying vfat...\n");
			if (mount(Actual_Block_Device.c_str(), Mount_Point.c_str(), "vfat", 0, NULL) != 0) {
				if (Display_Error)
					gui_msg(Msg(msg::kError, "fail_mount=Failed to mount '{1}' ({2})")(Mount_Point)(strerror(errno)));
				else
					LOGINFO("Unable to mount '%s'\n", Mount_Point.c_str());
				LOGINFO("Actual block device: '%s', current file system: '%s', flags: 0x%8x, options: '%s'\n", Actual_Block_Device.c_str(), Current_File_System.c_str(), flags, Mount_Options.c_str());
				return false;
			}
		} else {
#endif
			if (!Removable && Display_Error)
				gui_msg(Msg(msg::kError, "fail_mount=Failed to mount '{1}' ({2})")(Mount_Point)(strerror(errno)));
			else
				LOGINFO("Unable to mount '%s'\n", Mount_Point.c_str());
			LOGINFO("Actual block device: '%s', current file system: '%s'\n", Actual_Block_Device.c_str(), Current_File_System.c_str());
			return false;
#ifdef TW_NO_EXFAT_FUSE
		}
#endif
	}

	if (Removable)
		Update_Size(Display_Error);

	if (!Symlink_Mount_Point.empty() && TWFunc::Path_Exists(Symlink_Path)) {
		string Command = "mount -o bind '" + Symlink_Path + "' '" + Symlink_Mount_Point + "'";
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

		if (Is_Storage)
			PartitionManager.Remove_MTP_Storage(MTP_Storage_ID);

		if (!Symlink_Mount_Point.empty())
			umount(Symlink_Mount_Point.c_str());

		umount(Mount_Point.c_str());
		if (Is_Mounted()) {
			if (Display_Error)
				gui_msg(Msg(msg::kError, "fail_unmount=Failed to unmount '{1}' ({2})")(Mount_Point)(strerror(errno)));
			else
				LOGINFO("Unable to unmount '%s'\n", Mount_Point.c_str());
			return false;
		} else {
			return true;
		}
	} else {
		return true;
	}
}

bool TWPartition::ReMount(bool Display_Error) {
	if (UnMount(Display_Error))
		return Mount(Display_Error);
	return false;
}

bool TWPartition::ReMount_RW(bool Display_Error) {
	// No need to remount if already mounted rw
	if (Is_File_System_Writable())
		return true;

	bool ro = Mount_Read_Only;
	int flags = Mount_Flags;

	Mount_Read_Only = false;
	Mount_Flags &= ~MS_RDONLY;

	bool ret = ReMount(Display_Error);

	Mount_Read_Only = ro;
	Mount_Flags = flags;

	return ret;
}

bool TWPartition::Wipe(string New_File_System) {
	bool wiped = false, update_crypt = false, recreate_media = true;
	int check;
	string Layout_Filename = Mount_Point + "/.layout_version";

	if (!Can_Be_Wiped) {
		gui_msg(Msg(msg::kError, "cannot_wipe=Partition {1} cannot be wiped.")(Display_Name));
		return false;
	}

	if (Mount_Point == "/cache")
		Log_Offset = 0;

	if (Retain_Layout_Version && Mount(false) && TWFunc::Path_Exists(Layout_Filename))
		TWFunc::copy_file(Layout_Filename, "/.layout_version", 0600);
	else
		unlink("/.layout_version");

	if (Has_Data_Media && Current_File_System == New_File_System) {
		wiped = Wipe_Data_Without_Wiping_Media();
		recreate_media = false;
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
		else if (New_File_System == "ntfs")
			wiped = Wipe_NTFS();
		else {
			LOGERR("Unable to wipe '%s' -- unknown file system '%s'\n", Mount_Point.c_str(), New_File_System.c_str());
			unlink("/.layout_version");
			return false;
		}
		update_crypt = wiped;
	}

	if (wiped) {
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

		if (Has_Data_Media && recreate_media) {
			Recreate_Media_Folder();
		}
		if (Is_Storage && Mount(false))
			PartitionManager.Add_MTP_Storage(MTP_Storage_ID);
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

	gui_msg(Msg("wiping=Wiping {1}")(Backup_Display_Name));
	TWFunc::removeDir(Mount_Point + "/.android_secure/", true);
	return true;
}

bool TWPartition::Can_Repair() {
	if (Mount_Read_Only)
		return false;
	if (Current_File_System == "vfat" && TWFunc::Path_Exists("/sbin/fsck.fat"))
		return true;
	else if ((Current_File_System == "ext2" || Current_File_System == "ext3" || Current_File_System == "ext4") && TWFunc::Path_Exists("/sbin/e2fsck"))
		return true;
	else if (Current_File_System == "exfat" && TWFunc::Path_Exists("/sbin/fsck.exfat"))
		return true;
	else if (Current_File_System == "f2fs" && TWFunc::Path_Exists("/sbin/fsck.f2fs"))
		return true;
	else if (Current_File_System == "ntfs" && (TWFunc::Path_Exists("/sbin/ntfsfix") || TWFunc::Path_Exists("/sbin/fsck.ntfs")))
		return true;
	return false;
}

bool TWPartition::Repair() {
	string command;

	if (Current_File_System == "vfat") {
		if (!TWFunc::Path_Exists("/sbin/fsck.fat")) {
			gui_msg(Msg(msg::kError, "repair_not_exist={1} does not exist! Cannot repair!")("fsck.fat"));
			return false;
		}
		if (!UnMount(true))
			return false;
		gui_msg(Msg("repairing_using=Repairing {1} using {2}...")(Display_Name)("fsck.fat"));
		Find_Actual_Block_Device();
		command = "/sbin/fsck.fat -y " + Actual_Block_Device;
		LOGINFO("Repair command: %s\n", command.c_str());
		if (TWFunc::Exec_Cmd(command) == 0) {
			gui_msg("done=Done.");
			return true;
		} else {
			gui_msg(Msg(msg::kError, "unable_repair=Unable to repair {1}.")(Display_Name));
			return false;
		}
	}
	if (Current_File_System == "ext2" || Current_File_System == "ext3" || Current_File_System == "ext4") {
		if (!TWFunc::Path_Exists("/sbin/e2fsck")) {
			gui_msg(Msg(msg::kError, "repair_not_exist={1} does not exist! Cannot repair!")("e2fsck"));
			return false;
		}
		if (!UnMount(true))
			return false;
		gui_msg(Msg("repairing_using=Repairing {1} using {2}...")(Display_Name)("e2fsck"));
		Find_Actual_Block_Device();
		command = "/sbin/e2fsck -fp " + Actual_Block_Device;
		LOGINFO("Repair command: %s\n", command.c_str());
		if (TWFunc::Exec_Cmd(command) == 0) {
			gui_msg("done=Done.");
			return true;
		} else {
			gui_msg(Msg(msg::kError, "unable_repair=Unable to repair {1}.")(Display_Name));
			return false;
		}
	}
	if (Current_File_System == "exfat") {
		if (!TWFunc::Path_Exists("/sbin/fsck.exfat")) {
			gui_msg(Msg(msg::kError, "repair_not_exist={1} does not exist! Cannot repair!")("fsck.exfat"));
			return false;
		}
		if (!UnMount(true))
			return false;
		gui_msg(Msg("repairing_using=Repairing {1} using {2}...")(Display_Name)("fsck.exfat"));
		Find_Actual_Block_Device();
		command = "/sbin/fsck.exfat " + Actual_Block_Device;
		LOGINFO("Repair command: %s\n", command.c_str());
		if (TWFunc::Exec_Cmd(command) == 0) {
			gui_msg("done=Done.");
			return true;
		} else {
			gui_msg(Msg(msg::kError, "unable_repair=Unable to repair {1}.")(Display_Name));
			return false;
		}
	}
	if (Current_File_System == "f2fs") {
		if (!TWFunc::Path_Exists("/sbin/fsck.f2fs")) {
			gui_msg(Msg(msg::kError, "repair_not_exist={1} does not exist! Cannot repair!")("fsck.f2fs"));
			return false;
		}
		if (!UnMount(true))
			return false;
		gui_msg(Msg("repairing_using=Repairing {1} using {2}...")(Display_Name)("fsck.f2fs"));
		Find_Actual_Block_Device();
		command = "/sbin/fsck.f2fs " + Actual_Block_Device;
		LOGINFO("Repair command: %s\n", command.c_str());
		if (TWFunc::Exec_Cmd(command) == 0) {
			gui_msg("done=Done.");
			return true;
		} else {
			gui_msg(Msg(msg::kError, "unable_repair=Unable to repair {1}.")(Display_Name));
			return false;
		}
	}
	if (Current_File_System == "ntfs") {
		string Ntfsfix_Binary;
		if (TWFunc::Path_Exists("/sbin/ntfsfix"))
			Ntfsfix_Binary = "ntfsfix";
		else if (TWFunc::Path_Exists("/sbin/fsck.ntfs"))
			Ntfsfix_Binary = "fsck.ntfs";
		else {
			gui_msg(Msg(msg::kError, "repair_not_exist={1} does not exist! Cannot repair!")("ntfsfix"));
			return false;
		}
		if (!UnMount(true))
			return false;
		gui_msg(Msg("repairing_using=Repairing {1} using {2}...")(Display_Name)(Ntfsfix_Binary));
		Find_Actual_Block_Device();
		command = "/sbin/" + Ntfsfix_Binary + " " + Actual_Block_Device;
		LOGINFO("Repair command: %s\n", command.c_str());
		if (TWFunc::Exec_Cmd(command) == 0) {
			gui_msg("done=Done.");
			return true;
		} else {
			gui_msg(Msg(msg::kError, "unable_repair=Unable to repair {1}.")(Display_Name));
			return false;
		}
	}
	return false;
}

bool TWPartition::Can_Resize() {
	if (Mount_Read_Only)
		return false;
	if ((Current_File_System == "ext2" || Current_File_System == "ext3" || Current_File_System == "ext4") && TWFunc::Path_Exists("/sbin/resize2fs"))
		return true;
	return false;
}

bool TWPartition::Resize() {
	string command;

	if (Current_File_System == "ext2" || Current_File_System == "ext3" || Current_File_System == "ext4") {
		if (!Can_Repair()) {
			LOGINFO("Cannot resize %s because %s cannot be repaired before resizing.\n", Display_Name.c_str(), Display_Name.c_str());
			gui_msg(Msg(msg::kError, "cannot_resize=Cannot resize {1}.")(Display_Name));
			return false;
		}
		if (!TWFunc::Path_Exists("/sbin/resize2fs")) {
			LOGINFO("resize2fs does not exist! Cannot resize!\n");
			gui_msg(Msg(msg::kError, "cannot_resize=Cannot resize {1}.")(Display_Name));
			return false;
		}
		// Repair will unmount so no need to do it twice
		gui_msg(Msg("repair_resize=Repairing {1} before resizing.")( Display_Name));
		if (!Repair())
			return false;
		gui_msg(Msg("resizing=Resizing {1} using {2}...")(Display_Name)("resize2fs"));
		Find_Actual_Block_Device();
		command = "/sbin/resize2fs " + Actual_Block_Device;
		if (Length != 0) {
			unsigned long long Actual_Size = IOCTL_Get_Block_Size();
			if (Actual_Size == 0)
				return false;

			unsigned long long Block_Count;
			if (Length < 0) {
				// Reduce overall size by this length
				Block_Count = (Actual_Size / 1024LLU) - ((unsigned long long)(Length * -1) / 1024LLU);
			} else {
				// This is the size, not a size reduction
				Block_Count = ((unsigned long long)(Length) / 1024LLU);
			}
			char temp[256];
			sprintf(temp, "%llu", Block_Count);
			command += " ";
			command += temp;
			command += "K";
		}
		LOGINFO("Resize command: %s\n", command.c_str());
		if (TWFunc::Exec_Cmd(command) == 0) {
			Update_Size(true);
			gui_msg("done=Done.");
			return true;
		} else {
			Update_Size(true);
			gui_msg(Msg(msg::kError, "unable_resize=Unable to resize {1}.")(Display_Name));
			return false;
		}
	}
	return false;
}

bool TWPartition::Backup(PartitionSettings *part_settings, pid_t *tar_fork_pid) {
	if (Backup_Method == BM_FILES)
		return Backup_Tar(part_settings, tar_fork_pid);
	else if (Backup_Method == BM_DD)
		return Backup_Image(part_settings);
	else if (Backup_Method == BM_FLASH_UTILS)
		return Backup_Dump_Image(part_settings);
	LOGERR("Unknown backup method for '%s'\n", Mount_Point.c_str());
	return false;
}

bool TWPartition::Check_MD5(string restore_folder) {
	string Full_Filename, md5file;
	char split_filename[512];
	int index = 0;
	twrpDigest md5sum;

	sync();

	memset(split_filename, 0, sizeof(split_filename));
	Full_Filename = restore_folder + "/" + Backup_FileName;
	if (!TWFunc::Path_Exists(Full_Filename)) {
		// This is a split archive, we presume
		sprintf(split_filename, "%s%03i", Full_Filename.c_str(), index);
		LOGINFO("split_filename: %s\n", split_filename);
		md5file = split_filename;
		md5file += ".md5";
		if (!TWFunc::Path_Exists(md5file)) {
			gui_msg(Msg(msg::kError, "no_md5_found=No md5 file found for '{1}'. Please unselect Enable MD5 verification to restore.")(split_filename));
			return false;
		}
		md5sum.setfn(split_filename);
		while (index < 1000) {
			if (TWFunc::Path_Exists(split_filename) && md5sum.verify_md5digest() != 0) {
				gui_msg(Msg(msg::kError, "md5_fail_match=MD5 failed to match on '{1}'.")(split_filename));
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
			gui_msg(Msg(msg::kError, "no_md5_found=No md5 file found for '{1}'. Please unselect Enable MD5 verification to restore.")(split_filename));
			return false;
		}
		md5sum.setfn(Full_Filename);
		if (md5sum.verify_md5digest() != 0) {
			gui_msg(Msg(msg::kError, "md5_fail_match=MD5 failed to match on '{1}'.")(split_filename));
			return false;
		} else
			return true;
	}
	return false;
}

bool TWPartition::Restore(PartitionSettings *part_settings) {
	TWFunc::GUI_Operation_Text(TW_RESTORE_TEXT, Display_Name, gui_parse_text("{@restoring_hdr}"));
	LOGINFO("Restore filename is: %s/%s\n", part_settings->Restore_Name.c_str(), part_settings->Backup_FileName.c_str());

	string Restore_File_System = Get_Restore_File_System(part_settings);

	if (Is_File_System(Restore_File_System))
		return Restore_Tar(part_settings);
	else if (Is_Image(Restore_File_System))
		return Restore_Image(part_settings);

	LOGERR("Unknown restore method for '%s'\n", Mount_Point.c_str());
	return false;
}

string TWPartition::Get_Restore_File_System(PartitionSettings *part_settings) {
	size_t first_period, second_period;
	string Restore_File_System;

	// Parse backup filename to extract the file system before wiping
	first_period = part_settings->Backup_FileName.find(".");
	if (first_period == string::npos) {
		LOGERR("Unable to find file system (first period).\n");
		return string();
	}
	Restore_File_System = part_settings->Backup_FileName.substr(first_period + 1, part_settings->Backup_FileName.size() - first_period - 1);
	second_period = Restore_File_System.find(".");
	if (second_period == string::npos) {
		LOGERR("Unable to find file system (second period).\n");
		return string();
	}
	Restore_File_System.resize(second_period);
	return Restore_File_System;
}

string TWPartition::Backup_Method_By_Name() {
	if (Backup_Method == BM_NONE)
		return "none";
	else if (Backup_Method == BM_FILES)
		return "files";
	else if (Backup_Method == BM_DD)
		return "dd";
	else if (Backup_Method == BM_FLASH_UTILS)
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
#ifdef TW_INCLUDE_CRYPTO
	if (Is_Decrypted) {
		if (!UnMount(true))
			return false;
		if (delete_crypto_blk_dev((char*)("userdata")) != 0) {
			LOGERR("Error deleting crypto block device, continuing anyway.\n");
		}
	}
#endif
	Is_Decrypted = false;
	Is_Encrypted = false;
	Find_Actual_Block_Device();
	if (Crypto_Key_Location == "footer") {
		int newlen, fd;
		if (Length != 0) {
			newlen = Length;
			if (newlen < 0)
				newlen = newlen * -1;
		} else {
			newlen = CRYPT_FOOTER_OFFSET;
		}
		if ((fd = open(Actual_Block_Device.c_str(), O_RDWR)) < 0) {
			gui_print_color("warning", "Unable to open '%s' to wipe crypto key\n", Actual_Block_Device.c_str());
		} else {
			unsigned int block_count;
			if ((ioctl(fd, BLKGETSIZE, &block_count)) == -1) {
				gui_print_color("warning", "Unable to get block size for wiping crypto footer.\n");
			} else {
				off64_t offset = ((off64_t)block_count * 512) - newlen;
				if (lseek64(fd, offset, SEEK_SET) == -1) {
					gui_print_color("warning", "Unable to lseek64 for wiping crypto footer.\n");
				} else {
					void* buffer = malloc(newlen);
					if (!buffer) {
						gui_print_color("warning", "Failed to malloc for wiping crypto footer.\n");
					} else {
						memset(buffer, 0, newlen);
						int ret = write(fd, buffer, newlen);
						if (ret != newlen) {
							gui_print_color("warning", "Failed to wipe crypto footer.\n");
						} else {
							LOGINFO("Successfully wiped crypto footer.\n");
						}
						free(buffer);
					}
				}
			}
			close(fd);
		}
	} else {
		if (TWFunc::IOCTL_Get_Block_Size(Crypto_Key_Location.c_str()) >= 16384LLU) {
			string Command = "dd of='" + Crypto_Key_Location + "' if=/dev/zero bs=16384 count=1";
			TWFunc::Exec_Cmd(Command);
		} else {
			LOGINFO("Crypto key location reports size < 16K so not wiping crypto footer.\n");
		}
	}
	if (Wipe(Fstab_File_System)) {
		Has_Data_Media = Save_Data_Media;
		if (Has_Data_Media && !Symlink_Mount_Point.empty()) {
			Recreate_Media_Folder();
			if (Mount(false))
				PartitionManager.Add_MTP_Storage(MTP_Storage_ID);
		}
		DataManager::SetValue(TW_IS_ENCRYPTED, 0);
#ifndef TW_OEM_BUILD
		gui_msg("format_data_msg=You may need to reboot recovery to be able to use /data again.");
#endif
		return true;
	} else {
		Has_Data_Media = Save_Data_Media;
		gui_err("format_data_err=Unable to format to remove encryption.");
		if (Has_Data_Media && Mount(false))
			PartitionManager.Add_MTP_Storage(MTP_Storage_ID);
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

		gui_msg(Msg("formatting_using=Formatting {1} using {2}...")(Display_Name)("mke2fs"));
		Find_Actual_Block_Device();
		command = "mke2fs -t " + File_System + " -m 0 " + Actual_Block_Device;
		LOGINFO("mke2fs command: %s\n", command.c_str());
		if (TWFunc::Exec_Cmd(command) == 0) {
			Current_File_System = File_System;
			Recreate_AndSec_Folder();
			gui_msg("done=Done.");
			return true;
		} else {
			gui_msg(Msg(msg::kError, "unable_to_wipe=Unable to wipe {1}.")(Display_Name));
			return false;
		}
	} else
		return Wipe_RMRF();

	return false;
}

bool TWPartition::Wipe_EXT4() {
	Find_Actual_Block_Device();
	if (!Is_Present) {
		LOGINFO("Block device not present, cannot wipe %s.\n", Display_Name.c_str());
		gui_msg(Msg(msg::kError, "unable_to_wipe=Unable to wipe {1}.")(Display_Name));
		return false;
	}
	if (!UnMount(true))
		return false;

#if defined(HAVE_SELINUX) && defined(USE_EXT4)
	int ret;
	char *secontext = NULL;

	gui_msg(Msg("formatting_using=Formatting {1} using {2}...")(Display_Name)("make_ext4fs"));

	if (!selinux_handle || selabel_lookup(selinux_handle, &secontext, Mount_Point.c_str(), S_IFDIR) < 0) {
		LOGINFO("Cannot lookup security context for '%s'\n", Mount_Point.c_str());
		ret = make_ext4fs(Actual_Block_Device.c_str(), Length, Mount_Point.c_str(), NULL);
	} else {
		ret = make_ext4fs(Actual_Block_Device.c_str(), Length, Mount_Point.c_str(), selinux_handle);
	}
	if (ret != 0) {
		gui_msg(Msg(msg::kError, "unable_to_wipe=Unable to wipe {1}.")(Display_Name));
		return false;
	} else {
		string sedir = Mount_Point + "/lost+found";
		PartitionManager.Mount_By_Path(sedir.c_str(), true);
		rmdir(sedir.c_str());
		mkdir(sedir.c_str(), S_IRWXU | S_IRWXG | S_IWGRP | S_IXGRP);
		return true;
	}
#else
	if (TWFunc::Path_Exists("/sbin/make_ext4fs")) {
		string Command;

		gui_msg(Msg("formatting_using=Formatting {1} using {2}...")(Display_Name)("make_ext4fs"));
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
			gui_msg("done=Done.");
			return true;
		} else {
			gui_msg(Msg(msg::kError, "unable_to_wipe=Unable to wipe {1}.")(Display_Name));
			return false;
		}
	} else
		return Wipe_EXT23("ext4");
#endif
	return false;
}

bool TWPartition::Wipe_FAT() {
	string command;

	if (TWFunc::Path_Exists("/sbin/mkfs.fat")) {
		if (!UnMount(true))
			return false;

		gui_msg(Msg("formatting_using=Formatting {1} using {2}...")(Display_Name)("mkfs.fat"));
		Find_Actual_Block_Device();
		command = "mkfs.fat " + Actual_Block_Device;
		if (TWFunc::Exec_Cmd(command) == 0) {
			Current_File_System = "vfat";
			Recreate_AndSec_Folder();
			gui_msg("done=Done.");
			return true;
		} else {
			gui_msg(Msg(msg::kError, "unable_to_wipe=Unable to wipe {1}.")(Display_Name));
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

		gui_msg(Msg("formatting_using=Formatting {1} using {2}...")(Display_Name)("mkexfatfs"));
		Find_Actual_Block_Device();
		command = "mkexfatfs " + Actual_Block_Device;
		if (TWFunc::Exec_Cmd(command) == 0) {
			Recreate_AndSec_Folder();
			gui_msg("done=Done.");
			return true;
		} else {
			gui_msg(Msg(msg::kError, "unable_to_wipe=Unable to wipe {1}.")(Display_Name));
			return false;
		}
		return true;
	}
	return false;
}

bool TWPartition::Wipe_MTD() {
	if (!UnMount(true))
		return false;

	gui_msg(Msg("formatting_using=Formatting {1} using {2}...")(Display_Name)("MTD"));

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
	gui_msg("done=Done.");
	return true;
}

bool TWPartition::Wipe_RMRF() {
	if (!Mount(true))
		return false;
	// This is the only wipe that leaves the partition mounted, so we
	// must manually remove the partition from MTP if it is a storage
	// partition.
	if (Is_Storage)
		PartitionManager.Remove_MTP_Storage(MTP_Storage_ID);

	gui_msg(Msg("remove_all=Removing all files under '{1}'")(Mount_Point));
	TWFunc::removeDir(Mount_Point, true);
	Recreate_AndSec_Folder();
	return true;
}

bool TWPartition::Wipe_F2FS() {
	string command;

	if (TWFunc::Path_Exists("/sbin/mkfs.f2fs")) {
		if (!UnMount(true))
			return false;

		gui_msg(Msg("formatting_using=Formatting {1} using {2}...")(Display_Name)("mkfs.f2fs"));
		Find_Actual_Block_Device();
		command = "mkfs.f2fs -t 0";
		if (!Is_Decrypted && Length != 0) {
			// Only use length if we're not decrypted
			char len[32];
			int mod_length = Length;
			if (Length < 0)
				mod_length *= -1;
			sprintf(len, "%i", mod_length);
			command += " -r ";
			command += len;
		}
		command += " " + Actual_Block_Device;
		if (TWFunc::Exec_Cmd(command) == 0) {
			Recreate_AndSec_Folder();
			gui_msg("done=Done.");
			return true;
		} else {
			gui_msg(Msg(msg::kError, "unable_to_wipe=Unable to wipe {1}.")(Display_Name));
			return false;
		}
		return true;
	} else {
		LOGINFO("mkfs.f2fs binary not found, using rm -rf to wipe.\n");
		return Wipe_RMRF();
	}
	return false;
}

bool TWPartition::Wipe_NTFS() {
	string command;
	string Ntfsmake_Binary;

	if (TWFunc::Path_Exists("/sbin/mkntfs"))
		Ntfsmake_Binary = "mkntfs";
	else if (TWFunc::Path_Exists("/sbin/mkfs.ntfs"))
		Ntfsmake_Binary = "mkfs.ntfs";
	else
		return false;

	if (!UnMount(true))
		return false;

	gui_msg(Msg("formatting_using=Formatting {1} using {2}...")(Display_Name)(Ntfsmake_Binary));
	Find_Actual_Block_Device();
	command = "/sbin/" + Ntfsmake_Binary + " " + Actual_Block_Device;
	if (TWFunc::Exec_Cmd(command) == 0) {
		Recreate_AndSec_Folder();
		gui_msg("done=Done.");
		return true;
	} else {
		gui_msg(Msg(msg::kError, "unable_to_wipe=Unable to wipe {1}.")(Display_Name));
		return false;
	}
	return false;
}

bool TWPartition::Wipe_Data_Without_Wiping_Media() {
#ifdef TW_OEM_BUILD
	// In an OEM Build we want to do a full format
	return Wipe_Encryption();
#else
	bool ret = false;

	if (!Mount(true))
		return false;

	gui_msg("wiping_data=Wiping data without wiping /data/media ...");
	ret = Wipe_Data_Without_Wiping_Media_Func(Mount_Point + "/");
	if (ret)
		gui_msg("done=Done.");
	return ret;
#endif // ifdef TW_OEM_BUILD
}

bool TWPartition::Wipe_Data_Without_Wiping_Media_Func(const string& parent __unused) {
	string dir;

	DIR* d;
	d = opendir(parent.c_str());
	if (d != NULL) {
		struct dirent* de;
		while ((de = readdir(d)) != NULL) {
			if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)	 continue;

			dir = parent;
			dir.append(de->d_name);
			if (du.check_skip_dirs(dir)) {
				LOGINFO("skipped '%s'\n", dir.c_str());
				continue;
			}
			if (de->d_type == DT_DIR) {
				dir.append("/");
				if (!Wipe_Data_Without_Wiping_Media_Func(dir)) {
					closedir(d);
					return false;
				}
				rmdir(dir.c_str());
			} else if (de->d_type == DT_REG || de->d_type == DT_LNK || de->d_type == DT_FIFO || de->d_type == DT_SOCK) {
				if (!unlink(dir.c_str()))
					LOGINFO("Unable to unlink '%s'\n", dir.c_str());
			}
		}
		closedir(d);

		return true;
	}
	gui_msg(Msg(msg::kError, "error_opening_strerr=Error opening: '{1}' ({2})")(Mount_Point)(strerror(errno)));
	return false;
}

bool TWPartition::Backup_Tar(PartitionSettings *part_settings, pid_t *tar_fork_pid) {
	string Full_FileName;
	twrpTar tar;

	if (!Mount(true))
		return false;

	TWFunc::GUI_Operation_Text(TW_BACKUP_TEXT, Backup_Display_Name, "Backing Up");
	gui_msg(Msg("backing_up=Backing up {1}...")(Backup_Display_Name));

	DataManager::GetValue(TW_USE_COMPRESSION_VAR, tar.use_compression);

#ifndef TW_EXCLUDE_ENCRYPTED_BACKUPS
	if (Can_Encrypt_Backup) {
		DataManager::GetValue("tw_encrypt_backup", tar.use_encryption);
		if (tar.use_encryption) {
			if (Use_Userdata_Encryption)
				tar.userdata_encryption = tar.use_encryption;
			string Password;
			DataManager::GetValue("tw_backup_password", Password);
			tar.setpassword(Password);
		} else {
			tar.use_encryption = 0;
		}
	}
#endif

	Backup_FileName = Backup_Name + "." + Current_File_System + ".win";
	Full_FileName = part_settings->Full_Backup_Path + Backup_FileName;
	tar.has_data_media = Has_Data_Media;
	tar.part_settings = part_settings;
	tar.setdir(Backup_Path);
	tar.setfn(Full_FileName);
	tar.setsize(Backup_Size);
	tar.partition_name = Backup_Name;
	tar.backup_folder = part_settings->Full_Backup_Path;
	if (tar.createTarFork(tar_fork_pid) != 0)
		return false;
	return true;
}

bool TWPartition::Backup_Image(PartitionSettings *part_settings) {
	string Full_FileName, adb_file_name;
	int adb_control_bu_fd, compressed;

	TWFunc::GUI_Operation_Text(TW_BACKUP_TEXT, Display_Name, gui_parse_text("{@backing}"));
	gui_msg(Msg("backing_up=Backing up {1}...")(Backup_Display_Name));

	Backup_FileName = Backup_Name + "." + Current_File_System + ".win";

	if (part_settings->adbbackup) {
		Full_FileName = TW_ADB_BACKUP;
		adb_file_name  = part_settings->Full_Backup_Path + "/" + Backup_FileName;
	}
	else
		Full_FileName = part_settings->Full_Backup_Path + "/" + Backup_FileName;

	part_settings->total_restore_size = Backup_Size;

	if (part_settings->adbbackup) {
		if (!twadbbu::Write_TWIMG(adb_file_name, Backup_Size))
			return false;
	}

	if (!Raw_Read_Write(part_settings))
		return false;

	if (part_settings->adbbackup) {
		if (!twadbbu::Write_TWEOF())
			return false;
	}
	return true;
}

bool TWPartition::Raw_Read_Write(PartitionSettings *part_settings) {
	unsigned long long RW_Block_Size, Remain = Backup_Size;
	int src_fd = -1, dest_fd = -1;
	ssize_t bs;
	bool ret = false;
	void* buffer = NULL;
	unsigned long long backedup_size = 0;
	string srcfn, destfn;

	if (part_settings->PM_Method == PM_BACKUP) {
		srcfn = Actual_Block_Device;
		if (part_settings->adbbackup)
			destfn = TW_ADB_BACKUP;
		else
			destfn = part_settings->Full_Backup_Path + part_settings->Backup_FileName;
	}
	else {
		destfn = Actual_Block_Device;
		if (part_settings->adbbackup) {
			srcfn = TW_ADB_RESTORE;
		} else {
			srcfn = part_settings->Restore_Name + "/" + part_settings->Backup_FileName;
			Remain = TWFunc::Get_File_Size(srcfn);
		}
	}

	src_fd = open(srcfn.c_str(), O_RDONLY | O_LARGEFILE);
	if (src_fd < 0) {
		gui_msg(Msg(msg::kError, "error_opening_strerr=Error opening: '{1}' ({2})")(srcfn.c_str())(strerror(errno)));
		return false;
	}

	dest_fd = open(destfn.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_LARGEFILE, S_IRUSR | S_IWUSR);
	if (dest_fd < 0) {
		gui_msg(Msg(msg::kError, "error_opening_strerr=Error opening: '{1}' ({2})")(destfn.c_str())(strerror(errno)));
		goto exit;
	}
	
	LOGINFO("Reading '%s', writing '%s'\n", srcfn.c_str(), destfn.c_str());

	if (part_settings->adbbackup) {
		RW_Block_Size = MAX_ADB_READ;
		bs = MAX_ADB_READ;
	}
	else {
		RW_Block_Size = 1048576LLU; // 1MB
		bs = (ssize_t)(RW_Block_Size);
	}

	buffer = malloc((size_t)bs);
	if (!buffer) {
		LOGINFO("Raw_Read_Write failed to malloc\n");
		goto exit;
	}

	if (part_settings->progress)
		part_settings->progress->SetPartitionSize(part_settings->total_restore_size);

	while (Remain > 0) {
		if (Remain < RW_Block_Size)
			bs = (ssize_t)(Remain);
		if (read(src_fd,  buffer, bs) != bs) {
			LOGINFO("Error reading source fd (%s)\n", strerror(errno));
			goto exit;
		}
		if (write(dest_fd, buffer, bs) != bs) {
			LOGINFO("Error writing destination fd (%s)\n", strerror(errno));
			goto exit;
		}
		backedup_size += (unsigned long long)(bs);
		Remain = Remain - (unsigned long long)(bs);
		if (part_settings->progress)
			part_settings->progress->UpdateSize(backedup_size);
		if (PartitionManager.Check_Backup_Cancel() != 0)
			goto exit;
	}
	if (part_settings->progress)
		part_settings->progress->UpdateDisplayDetails(true);
	fsync(dest_fd);
	ret = true;
exit:
	if (src_fd >= 0)
		close(src_fd);
	if (dest_fd >= 0)
		close(dest_fd);
	if (buffer)
		free(buffer);
	return ret;
}

bool TWPartition::Backup_Dump_Image(PartitionSettings *part_settings) {
	string Full_FileName, Command;
	int use_compression, adb_control_bu_fd;
	unsigned long long compressed;

	TWFunc::GUI_Operation_Text(TW_BACKUP_TEXT, Display_Name, gui_parse_text("{@backing}"));
	gui_msg(Msg("backing_up=Backing up {1}...")(Backup_Display_Name));

	if (part_settings->progress)
		part_settings->progress->SetPartitionSize(Backup_Size);

	Backup_FileName = Backup_Name + "." + Current_File_System + ".win";
	Full_FileName = part_settings->Full_Backup_Path + "/" + Backup_FileName;

	Command = "dump_image " + MTD_Name + " '" + Full_FileName + "'";

	LOGINFO("Backup command: '%s'\n", Command.c_str());
	TWFunc::Exec_Cmd(Command);
	tw_set_default_metadata(Full_FileName.c_str());
	if (TWFunc::Get_File_Size(Full_FileName) == 0) {
		// Actual size may not match backup size due to bad blocks on MTD devices so just check for 0 bytes
		gui_msg(Msg(msg::kError, "backup_size=Backup file size for '{1}' is 0 bytes.")(Full_FileName));
		return false;
	}
	if (part_settings->progress)
		part_settings->progress->UpdateSize(Backup_Size);

	return true;
}

unsigned long long TWPartition::Get_Restore_Size(PartitionSettings *part_settings) {
	if (!part_settings->adbbackup) {
		InfoManager restore_info(part_settings->Restore_Name + "/" + Backup_Name + ".info");
		if (restore_info.LoadValues() == 0) {
			if (restore_info.GetValue("backup_size", Restore_Size) == 0) {
				LOGINFO("Read info file, restore size is %llu\n", Restore_Size);
				return Restore_Size;
			}
		}
	}

	string Full_FileName, Restore_File_System = Get_Restore_File_System(part_settings);

	Full_FileName = part_settings->Restore_Name + "/" + Backup_FileName;
	if (Is_Image(Restore_File_System)) {
		Restore_Size = TWFunc::Get_File_Size(Full_FileName);
		return Restore_Size;
	}

	twrpTar tar;
	tar.setdir(Backup_Path);
	tar.setfn(Full_FileName);
	tar.backup_name = Full_FileName;
#ifndef TW_EXCLUDE_ENCRYPTED_BACKUPS
	string Password;
	DataManager::GetValue("tw_restore_password", Password);
	if (!Password.empty())
		tar.setpassword(Password);
#endif
	tar.partition_name = Backup_Name;
	tar.backup_folder = part_settings->Restore_Name;
	tar.part_settings = part_settings;
	Restore_Size = tar.get_size();
	return Restore_Size;
}

bool TWPartition::Restore_Tar(PartitionSettings *part_settings) {
	string Full_FileName;
	bool ret = false;
	string Restore_File_System = Get_Restore_File_System(part_settings);

	if (Has_Android_Secure) {
		if (!Wipe_AndSec())
			return false;
	} else {
		gui_msg(Msg("wiping=Wiping {1}")(Backup_Display_Name));
		if (Has_Data_Media && Mount_Point == "/data" && Restore_File_System != Current_File_System) {
			gui_msg(Msg(msg::kWarning, "datamedia_fs_restore=WARNING: This /data backup was made with {1} file system! The backup may not boot unless you change back to {1}.")(Restore_File_System));
			if (!Wipe_Data_Without_Wiping_Media())
				return false;
		} else {
			if (!Wipe(Restore_File_System))
				return false;
		}
	}
	TWFunc::GUI_Operation_Text(TW_RESTORE_TEXT, Backup_Display_Name, gui_parse_text("{@restoring_hdr}"));
	gui_msg(Msg("restoring=Restoring {1}...")(Backup_Display_Name));

	// Remount as read/write as needed so we can restore the backup
	if (!ReMount_RW(true))
		return false;

	Full_FileName = part_settings->Restore_Name + "/" + part_settings->Backup_FileName;
	twrpTar tar;
	tar.part_settings = part_settings;
	tar.setdir(Backup_Path);
	tar.setfn(Full_FileName);
	tar.backup_name = Backup_Name;
#ifndef TW_EXCLUDE_ENCRYPTED_BACKUPS
	string Password;
	DataManager::GetValue("tw_restore_password", Password);
	if (!Password.empty())
		tar.setpassword(Password);
#endif
	part_settings->progress->SetPartitionSize(Get_Restore_Size(part_settings));
	if (tar.extractTarFork() != 0)
		ret = false;
	else
		ret = true;
#ifdef HAVE_CAPABILITIES
	// Restore capabilities to the run-as binary
	if (Mount_Point == "/system" && Mount(true) && TWFunc::Path_Exists("/system/bin/run-as")) {
		struct vfs_cap_data cap_data;
		uint64_t capabilities = (1 << CAP_SETUID) | (1 << CAP_SETGID);

		memset(&cap_data, 0, sizeof(cap_data));
		cap_data.magic_etc = VFS_CAP_REVISION | VFS_CAP_FLAGS_EFFECTIVE;
		cap_data.data[0].permitted = (uint32_t) (capabilities & 0xffffffff);
		cap_data.data[0].inheritable = 0;
		cap_data.data[1].permitted = (uint32_t) (capabilities >> 32);
		cap_data.data[1].inheritable = 0;
		if (setxattr("/system/bin/run-as", XATTR_NAME_CAPS, &cap_data, sizeof(cap_data), 0) < 0) {
			LOGINFO("Failed to reset capabilities of /system/bin/run-as binary.\n");
		} else {
			LOGINFO("Reset capabilities of /system/bin/run-as binary successful.\n");
		}
	}
#endif
	if (Mount_Read_Only || Mount_Flags & MS_RDONLY)
		// Remount as read only when restoration is complete
		ReMount(true);

	return ret;
}

bool TWPartition::Restore_Image(PartitionSettings *part_settings) {
	string Full_FileName;
	string Restore_File_System = Get_Restore_File_System(part_settings);

	TWFunc::GUI_Operation_Text(TW_RESTORE_TEXT, Backup_Display_Name, gui_parse_text("{@restoring_hdr}"));
	gui_msg(Msg("restoring=Restoring {1}...")(Backup_Display_Name));

	if (part_settings->adbbackup)
		Full_FileName = TW_ADB_RESTORE;
	else
		Full_FileName = part_settings->Full_Backup_Path + part_settings->Backup_FileName;

	if (Restore_File_System == "emmc") {
		if (!part_settings->adbbackup)
			part_settings->total_restore_size = (uint64_t)(TWFunc::Get_File_Size(Full_FileName));
		if (!Raw_Read_Write(part_settings))
			return false;
	} else if (Restore_File_System == "mtd" || Restore_File_System == "bml") {
		if (!Flash_Image_FI(Full_FileName, part_settings->progress))
			return false;
	}

	if (part_settings->adbbackup) {
		if (!twadbbu::Write_TWEOF())
			return false;
	}
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
			Used = du.Get_Folder_Size(Mount_Point);
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
	if (Is_Decrypted && !Decrypted_Block_Device.empty()) {
		Actual_Block_Device = Decrypted_Block_Device;
		if (TWFunc::Path_Exists(Decrypted_Block_Device))
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
	string Media_Path = Mount_Point + "/media";

	if (!Mount(true)) {
		gui_msg(Msg(msg::kError, "recreate_folder_err=Unable to recreate {1} folder.")(Media_Path));
	} else if (!TWFunc::Path_Exists(Media_Path)) {
		PartitionManager.Mount_By_Path(Symlink_Mount_Point, true);
		LOGINFO("Recreating %s folder.\n", Media_Path.c_str());
		mkdir(Media_Path.c_str(), 0770);
		string Internal_path = DataManager::GetStrValue("tw_internal_path");
		if (!Internal_path.empty()) {
			LOGINFO("Recreating %s folder.\n", Internal_path.c_str());
			mkdir(Internal_path.c_str(), 0770);
		}
#ifdef TW_INTERNAL_STORAGE_PATH
		mkdir(EXPAND(TW_INTERNAL_STORAGE_PATH), 0770);
#endif
#ifdef HAVE_SELINUX
		// Afterwards, we will try to set the
		// default metadata that we were hopefully able to get during
		// early boot.
		tw_set_default_metadata(Media_Path.c_str());
		if (!Internal_path.empty())
			tw_set_default_metadata(Internal_path.c_str());
#endif
		// Toggle mount to ensure that "internal sdcard" gets mounted
		PartitionManager.UnMount_By_Path(Symlink_Mount_Point, true);
		PartitionManager.Mount_By_Path(Symlink_Mount_Point, true);
	}
}

void TWPartition::Recreate_AndSec_Folder(void) {
	if (!Has_Android_Secure)
		return;
	LOGINFO("Creating %s: %s\n", Backup_Display_Name.c_str(), Symlink_Path.c_str());
	if (!Mount(true)) {
		gui_msg(Msg(msg::kError, "recreate_folder_err=Unable to recreate {1} folder.")(Backup_Name));
	} else if (!TWFunc::Path_Exists(Symlink_Path)) {
		LOGINFO("Recreating %s folder.\n", Backup_Name.c_str());
		PartitionManager.Mount_By_Path(Symlink_Mount_Point, true);
		mkdir(Symlink_Path.c_str(), S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
		PartitionManager.UnMount_By_Path(Symlink_Mount_Point, true);
	}
}

uint64_t TWPartition::Get_Max_FileSize() {
	uint64_t maxFileSize = 0;
	const uint64_t constGB = (uint64_t) 1024 * 1024 * 1024;
	const uint64_t constTB = (uint64_t) constGB * 1024;
	const uint64_t constPB = (uint64_t) constTB * 1024;
	const uint64_t constEB = (uint64_t) constPB * 1024;
	if (Current_File_System == "ext4")
		maxFileSize = 16 * constTB; //16 TB
	else if (Current_File_System == "vfat")
		maxFileSize = 4 * constGB; //4 GB
	else if (Current_File_System == "ntfs")
		maxFileSize = 256 * constTB; //256 TB
	else if (Current_File_System == "exfat")
		maxFileSize = 16 * constPB; //16 PB
	else if (Current_File_System == "ext3")
		maxFileSize = 2 * constTB; //2 TB
	else if (Current_File_System == "f2fs")
		maxFileSize = 3.94 * constTB; //3.94 TB
	else
		maxFileSize = 100000000L;
	return maxFileSize - 1;
}

bool TWPartition::Flash_Image(PartitionSettings *part_settings) {
	string Restore_File_System, full_filename;

	full_filename = part_settings->Restore_Name + "/" + part_settings->Backup_FileName;

	LOGINFO("Image filename is: %s\n", part_settings->Backup_FileName.c_str());

	if (Backup_Method == BM_FILES) {
		LOGERR("Cannot flash images to file systems\n");
		return false;
	} else if (!Can_Flash_Img) {
		LOGERR("Cannot flash images to partitions %s\n", Display_Name.c_str());
		return false;
	} else {
		if (!Find_Partition_Size()) {
			LOGERR("Unable to find partition size for '%s'\n", Mount_Point.c_str());
			return false;
		}
		unsigned long long image_size = TWFunc::Get_File_Size(full_filename);
		if (image_size > Size) {
			LOGINFO("Size (%llu bytes) of image '%s' is larger than target device '%s' (%llu bytes)\n",
				image_size, part_settings->Backup_FileName.c_str(), Actual_Block_Device.c_str(), Size);
			gui_err("img_size_err=Size of image is larger than target device");
			return false;
		}
		if (Backup_Method == BM_DD) {
			if (!part_settings->adbbackup) {
				if (Is_Sparse_Image(full_filename)) {
					return Flash_Sparse_Image(full_filename);
				}
			}
			unsigned long long file_size = (unsigned long long)(TWFunc::Get_File_Size(full_filename));
			return Raw_Read_Write(part_settings);
		} else if (Backup_Method == BM_FLASH_UTILS) {
			return Flash_Image_FI(full_filename, NULL);
		}
	}

	LOGERR("Unknown flash method for '%s'\n", Mount_Point.c_str());
	return false;
}

bool TWPartition::Is_Sparse_Image(const string& Filename) {
	uint32_t magic = 0;
	int fd = open(Filename.c_str(), O_RDONLY);
	if (fd < 0) {
		gui_msg(Msg(msg::kError, "error_opening_strerr=Error opening: '{1}' ({2})")(Filename)(strerror(errno)));
		return false;
	}

	if (read(fd, &magic, sizeof(magic)) != sizeof(magic)) {
		gui_msg(Msg(msg::kError, "error_opening_strerr=Error opening: '{1}' ({2})")(Filename)(strerror(errno)));
		close(fd);
		return false;
	}
	close(fd);
	if (magic == SPARSE_HEADER_MAGIC)
		return true;
	return false;
}

bool TWPartition::Flash_Sparse_Image(const string& Filename) {
	string Command;

	gui_msg(Msg("flashing=Flashing {1}...")(Display_Name));

	Command = "simg2img '" + Filename + "' '" + Actual_Block_Device + "'";
	LOGINFO("Flash command: '%s'\n", Command.c_str());
	TWFunc::Exec_Cmd(Command);
	return true;
}

bool TWPartition::Flash_Image_FI(const string& Filename, ProgressTracking *progress) {
	string Command;
	unsigned long long file_size;

	gui_msg(Msg("flashing=Flashing {1}...")(Display_Name));
	if (progress) {
		file_size = (unsigned long long)(TWFunc::Get_File_Size(Filename));
		progress->SetPartitionSize(file_size);
	}
	// Sometimes flash image doesn't like to flash due to the first 2KB matching, so we erase first to ensure that it flashes
	Command = "erase_image " + MTD_Name;
	LOGINFO("Erase command: '%s'\n", Command.c_str());
	TWFunc::Exec_Cmd(Command);
	Command = "flash_image " + MTD_Name + " '" + Filename + "'";
	LOGINFO("Flash command: '%s'\n", Command.c_str());
	TWFunc::Exec_Cmd(Command);
	if (progress)
		progress->UpdateSize(file_size);
	return true;
}

void TWPartition::Change_Mount_Read_Only(bool new_value) {
	Mount_Read_Only = new_value;
}

bool TWPartition::Is_Read_Only() {
	return Mount_Read_Only;
}

int TWPartition::Check_Lifetime_Writes() {
	bool original_read_only = Mount_Read_Only;
	int ret = 1;

	Mount_Read_Only = true;
	if (Mount(false)) {
		Find_Actual_Block_Device();
		string block = basename(Actual_Block_Device.c_str());
		string file = "/sys/fs/" + Current_File_System + "/" + block + "/lifetime_write_kbytes";
		string result;
		if (TWFunc::Path_Exists(file)) {
			if (TWFunc::read_file(file, result) != 0) {
				LOGINFO("Check_Lifetime_Writes of '%s' failed to read_file\n", file.c_str());
			} else {
				LOGINFO("Check_Lifetime_Writes result: '%s'\n", result.c_str());
				if (result == "0") {
					ret = 0;
				}
			}
		} else {
			LOGINFO("Check_Lifetime_Writes file does not exist '%s'\n", file.c_str());
		}
		UnMount(true);
	} else {
		LOGINFO("Check_Lifetime_Writes failed to mount '%s'\n", Mount_Point.c_str());
	}
	Mount_Read_Only = original_read_only;
	return ret;
}

int TWPartition::Decrypt_Adopted() {
#ifdef TW_INCLUDE_CRYPTO
	int ret = 1;
	Is_Adopted_Storage = false;
	string Adopted_Key_File = "";

	if (!Removable)
		return ret;

	int fd = open(Alternate_Block_Device.c_str(), O_RDONLY);
	if (fd < 0) {
		LOGINFO("failed to open '%s'\n", Alternate_Block_Device.c_str());
		return ret;
	}
	char type_guid[80];
	char part_guid[80];

	if (gpt_disk_get_partition_info(fd, 2, type_guid, part_guid) == 0) {
		LOGINFO("type: '%s'\n", type_guid);
		LOGINFO("part: '%s'\n", part_guid);
		Adopted_GUID = part_guid;
		LOGINFO("Adopted_GUID '%s'\n", Adopted_GUID.c_str());
		if (strcmp(type_guid, TWGptAndroidExpand) == 0) {
			LOGINFO("android_expand found\n");
			Adopted_Key_File = "/data/misc/vold/expand_";
			Adopted_Key_File += part_guid;
			Adopted_Key_File += ".key";
			if (TWFunc::Path_Exists(Adopted_Key_File)) {
				Is_Adopted_Storage = true;
				/* Until we find a use case for this, I think it is safe
				 * to disable USB Mass Storage whenever adopted storage
				 * is present.
				 */
				LOGINFO("Detected adopted storage, disabling USB mass storage mode\n");
				DataManager::SetValue("tw_has_usb_storage", 0);
			}
		}
	}

	if (Is_Adopted_Storage) {
		string Adopted_Block_Device = Alternate_Block_Device + "p2";
		if (!TWFunc::Path_Exists(Adopted_Block_Device)) {
			Adopted_Block_Device = Alternate_Block_Device + "2";
			if (!TWFunc::Path_Exists(Adopted_Block_Device)) {
				LOGINFO("Adopted block device does not exist\n");
				goto exit;
			}
		}
		LOGINFO("key file is '%s', block device '%s'\n", Adopted_Key_File.c_str(), Adopted_Block_Device.c_str());
		char crypto_blkdev[MAXPATHLEN];
		std::string thekey;
		int fdkey = open(Adopted_Key_File.c_str(), O_RDONLY);
		if (fdkey < 0) {
			LOGINFO("failed to open key file\n");
			goto exit;
		}
		char buf[512];
		ssize_t n;
		while ((n = read(fdkey, &buf[0], sizeof(buf))) > 0) {
			thekey.append(buf, n);
		}
		close(fdkey);
		unsigned char* key = (unsigned char*) thekey.data();
		cryptfs_revert_ext_volume(part_guid);

		ret = cryptfs_setup_ext_volume(part_guid, Adopted_Block_Device.c_str(), key, thekey.size(), crypto_blkdev);
		if (ret == 0) {
			LOGINFO("adopted storage new block device: '%s'\n", crypto_blkdev);
			Decrypted_Block_Device = crypto_blkdev;
			Is_Decrypted = true;
			Is_Encrypted = true;
			Find_Actual_Block_Device();
			if (!Mount_Storage_Retry(false)) {
				LOGERR("Failed to mount decrypted adopted storage device\n");
				Is_Decrypted = false;
				Is_Encrypted = false;
				cryptfs_revert_ext_volume(part_guid);
				ret = 1;
			} else {
				UnMount(false);
				Has_Android_Secure = false;
				Symlink_Path = "";
				Symlink_Mount_Point = "";
				Backup_Name = Mount_Point.substr(1);
				Backup_Path = Mount_Point;
				TWPartition* sdext = PartitionManager.Find_Partition_By_Path("/sd-ext");
				if (sdext && sdext->Actual_Block_Device == Adopted_Block_Device) {
					LOGINFO("Removing /sd-ext from partition list due to adopted storage\n");
					PartitionManager.Remove_Partition_By_Path("/sd-ext");
				}
				Setup_Data_Media();
				Recreate_Media_Folder();
				Wipe_Available_in_GUI = true;
				Wipe_During_Factory_Reset = true;
				Can_Be_Backed_Up = true;
				Can_Encrypt_Backup = true;
				Use_Userdata_Encryption = true;
				Is_Storage = true;
				Storage_Name = "Adopted Storage";
				Is_SubPartition = true;
				SubPartition_Of = "/data";
				PartitionManager.Add_MTP_Storage(MTP_Storage_ID);
				DataManager::SetValue("tw_has_adopted_storage", 1);
			}
		} else {
			LOGERR("Failed to setup adopted storage decryption\n");
		}
	}
exit:
	return ret;
#else
	LOGINFO("Decrypt_Adopted: no crypto support\n");
	return 1;
#endif
}

void TWPartition::Revert_Adopted() {
#ifdef TW_INCLUDE_CRYPTO
	if (!Adopted_GUID.empty()) {
		PartitionManager.Remove_MTP_Storage(Mount_Point);
		UnMount(false);
		cryptfs_revert_ext_volume(Adopted_GUID.c_str());
		Is_Adopted_Storage = false;
		Is_Encrypted = false;
		Is_Decrypted = false;
		Decrypted_Block_Device = "";
		Find_Actual_Block_Device();
		Wipe_During_Factory_Reset = false;
		Can_Be_Backed_Up = false;
		Can_Encrypt_Backup = false;
		Use_Userdata_Encryption = false;
		Is_SubPartition = false;
		SubPartition_Of = "";
		Has_Data_Media = false;
		Storage_Path = Mount_Point;
		if (!Symlink_Mount_Point.empty()) {
			TWPartition* Dat = PartitionManager.Find_Partition_By_Path("/data");
			if (Dat) {
				Dat->UnMount(false);
				Dat->Symlink_Mount_Point = Symlink_Mount_Point;
			}
			Symlink_Mount_Point = "";
		}
	}
#else
	LOGINFO("Revert_Adopted: no crypto support\n");
#endif
}
