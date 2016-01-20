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
#include <libgen.h>
#include <iostream>
#include <sstream>
#include <sys/param.h>

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

using namespace std;

extern struct selabel_handle *selinux_handle;
extern bool datamedia;

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
			Mount_Read_Only = true;
		} else if (Mount_Point == "/data") {
			UnMount(false); // added in case /data is mounted as tmpfs for qcom hardware decrypt
			Display_Name = "Data";
			Backup_Display_Name = Display_Name;
			Storage_Name = Display_Name;
			Wipe_Available_in_GUI = true;
			Wipe_During_Factory_Reset = true;
			Can_Be_Backed_Up = true;
			Can_Encrypt_Backup = true;
			Use_Userdata_Encryption = true;
			if (datamedia)
				Setup_Data_Media();
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
				// Filesystem is not encrypted and the mount
				// succeeded, so get it back to the original
				// unmounted state
				UnMount(false);
			}
			if (datamedia && (!Is_Encrypted || (Is_Encrypted && Is_Decrypted)))
				Recreate_Media_Folder();
#else
			if (datamedia)
				Recreate_Media_Folder();
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

	// Process any custom flags
	if (Flags.size() > 0)
		Process_Flags(Flags, Display_Error);
	return true;
}

bool TWPartition::Process_FS_Flags(string& Options, int& Flags) {
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
			Is_Settings_Storage = true;
		} else if (strcmp(ptr, "andsec") == 0) {
			Has_Android_Secure = true;
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
			Format_Block_Size = (unsigned long)(atol(ptr));
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
		} else if ((ptr_len > 12 && strncmp(ptr, "encryptable=", 12) == 0) || (ptr_len > 13 && strncmp(ptr, "forceencrypt=", 13) == 0)) {
			ptr += 12;
			if (*ptr == '=') ptr++;
			if (*ptr == '\"') ptr++;

			Crypto_Key_Location = ptr;
			if (Crypto_Key_Location.substr(Crypto_Key_Location.size() - 1, 1) == "\"") {
				Crypto_Key_Location.resize(Crypto_Key_Location.size() - 1);
			}
		} else if (ptr_len > 8 && strncmp(ptr, "mounttodecrypt", 14) == 0) {
			Mount_To_Decrypt = true;
		} else if (strncmp(ptr, "flashimg", 8) == 0) {
			if (ptr_len == 8) {
				Can_Flash_Img = true;
			} else if (ptr_len == 10) {
				ptr += 9;
				if (*ptr == '1' || *ptr == 'y' || *ptr == 'Y') {
					Can_Flash_Img = true;
				} else {
					Can_Flash_Img = false;
				}
			}
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

bool TWPartition::Backup(string backup_folder, const unsigned long long *overall_size, const unsigned long long *other_backups_size, pid_t &tar_fork_pid) {
	if (Backup_Method == FILES) {
		return Backup_Tar(backup_folder, overall_size, other_backups_size, tar_fork_pid);
	}
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

bool TWPartition::Restore(string restore_folder, const unsigned long long *total_restore_size, unsigned long long *already_restored_size) {
	string Restore_File_System;

	TWFunc::GUI_Operation_Text(TW_RESTORE_TEXT, Display_Name, gui_parse_text("{@restoring_hdr}"));
	LOGINFO("Restore filename is: %s\n", Backup_FileName.c_str());

	Restore_File_System = Get_Restore_File_System(restore_folder);

	if (Is_File_System(Restore_File_System))
		return Restore_Tar(restore_folder, Restore_File_System, total_restore_size, already_restored_size);
	else if (Is_Image(Restore_File_System)) {
		return Restore_Image(restore_folder, total_restore_size, already_restored_size, Restore_File_System);
	}

	LOGERR("Unknown restore method for '%s'\n", Mount_Point.c_str());
	return false;
}

string TWPartition::Get_Restore_File_System(string restore_folder) {
	size_t first_period, second_period;
	string Restore_File_System;

	// Parse backup filename to extract the file system before wiping
	first_period = Backup_FileName.find(".");
	if (first_period == string::npos) {
		LOGERR("Unable to find file system (first period).\n");
		return string();
	}
	Restore_File_System = Backup_FileName.substr(first_period + 1, Backup_FileName.size() - first_period - 1);
	second_period = Restore_File_System.find(".");
	if (second_period == string::npos) {
		LOGERR("Unable to find file system (second period).\n");
		return string();
	}
	Restore_File_System.resize(second_period);
	LOGINFO("Restore file system is: '%s'.\n", Restore_File_System.c_str());
	return Restore_File_System;
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
		string Command = "flash_image " + Crypto_Key_Location + " /dev/zero";
		TWFunc::Exec_Cmd(Command);
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

bool TWPartition::Backup_Tar(string backup_folder, const unsigned long long *overall_size, const unsigned long long *other_backups_size, pid_t &tar_fork_pid) {
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
	gui_msg(Msg("backing_up=Backing up {1}...")(Backup_Display_Name));

	DataManager::GetValue(TW_USE_COMPRESSION_VAR, use_compression);
	tar.use_compression = use_compression;

#ifndef TW_EXCLUDE_ENCRYPTED_BACKUPS
	DataManager::GetValue("tw_encrypt_backup", use_encryption);
	if (use_encryption && Can_Encrypt_Backup) {
		tar.use_encryption = use_encryption;
		if (Use_Userdata_Encryption)
			tar.userdata_encryption = use_encryption;
		string Password;
		DataManager::GetValue("tw_backup_password", Password);
		tar.setpassword(Password);
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
	tar.partition_name = Backup_Name;
	tar.backup_folder = backup_folder;
	if (tar.createTarFork(overall_size, other_backups_size, tar_fork_pid) != 0)
		return false;
	return true;
}

bool TWPartition::Backup_DD(string backup_folder) {
	char back_name[255], block_size[32], dd_count[32];
	string Full_FileName, Command, DD_BS, DD_COUNT;
	int use_compression;
	unsigned long long DD_Block_Size, DD_Count;

	DD_Block_Size = 16 * 1024 * 1024;
	while (Backup_Size % DD_Block_Size != 0) DD_Block_Size >>= 1;

	DD_Count = Backup_Size / DD_Block_Size;

	sprintf(dd_count, "%llu", DD_Count);
	DD_COUNT = dd_count;

	sprintf(block_size, "%llu", DD_Block_Size);
	DD_BS = block_size;

	TWFunc::GUI_Operation_Text(TW_BACKUP_TEXT, Display_Name, gui_parse_text("{@backing}"));
	gui_msg(Msg("backing_up=Backing up {1}...")(Backup_Display_Name));

	sprintf(back_name, "%s.%s.win", Backup_Name.c_str(), Current_File_System.c_str());
	Backup_FileName = back_name;

	Full_FileName = backup_folder + "/" + Backup_FileName;

	Command = "dd if=" + Actual_Block_Device + " of='" + Full_FileName + "'" + " bs=" + DD_BS + " count=" + DD_COUNT;
	LOGINFO("Backup command: '%s'\n", Command.c_str());
	TWFunc::Exec_Cmd(Command);
	tw_set_default_metadata(Full_FileName.c_str());
	if (TWFunc::Get_File_Size(Full_FileName) == 0) {
		gui_msg(Msg(msg::kError, "backup_size=Backup file size for '{1}' is 0 bytes.")(Full_FileName));
		return false;
	}
	return true;
}

bool TWPartition::Backup_Dump_Image(string backup_folder) {
	char back_name[255];
	string Full_FileName, Command;
	int use_compression;

	TWFunc::GUI_Operation_Text(TW_BACKUP_TEXT, Display_Name, gui_parse_text("{@backing}"));
	gui_msg(Msg("backing_up=Backing up {1}...")(Backup_Display_Name));

	sprintf(back_name, "%s.%s.win", Backup_Name.c_str(), Current_File_System.c_str());
	Backup_FileName = back_name;

	Full_FileName = backup_folder + "/" + Backup_FileName;

	Command = "dump_image " + MTD_Name + " '" + Full_FileName + "'";
	LOGINFO("Backup command: '%s'\n", Command.c_str());
	TWFunc::Exec_Cmd(Command);
	tw_set_default_metadata(Full_FileName.c_str());
	if (TWFunc::Get_File_Size(Full_FileName) == 0) {
		// Actual size may not match backup size due to bad blocks on MTD devices so just check for 0 bytes
		gui_msg(Msg(msg::kError, "backup_size=Backup file size for '{1}' is 0 bytes.")(Full_FileName));
		return false;
	}
	return true;
}

unsigned long long TWPartition::Get_Restore_Size(string restore_folder) {
	InfoManager restore_info(restore_folder + "/" + Backup_Name + ".info");
	if (restore_info.LoadValues() == 0) {
		if (restore_info.GetValue("backup_size", Restore_Size) == 0) {
			LOGINFO("Read info file, restore size is %llu\n", Restore_Size);
			return Restore_Size;
		}
	}
	string Full_FileName, Restore_File_System = Get_Restore_File_System(restore_folder);

	Full_FileName = restore_folder + "/" + Backup_FileName;

	if (Is_Image(Restore_File_System)) {
		Restore_Size = TWFunc::Get_File_Size(Full_FileName);
		return Restore_Size;
	}

	twrpTar tar;
	tar.setdir(Backup_Path);
	tar.setfn(Full_FileName);
	tar.backup_name = Backup_Name;
#ifndef TW_EXCLUDE_ENCRYPTED_BACKUPS
	string Password;
	DataManager::GetValue("tw_restore_password", Password);
	if (!Password.empty())
		tar.setpassword(Password);
#endif
	tar.partition_name = Backup_Name;
	tar.backup_folder = restore_folder;
	Restore_Size = tar.get_size();
	return Restore_Size;
}

bool TWPartition::Restore_Tar(string restore_folder, string Restore_File_System, const unsigned long long *total_restore_size, unsigned long long *already_restored_size) {
	string Full_FileName, Command;
	int index = 0;
	char split_index[5];
	bool ret = false;

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

	if (!Mount(true))
		return false;

	Full_FileName = restore_folder + "/" + Backup_FileName;
	twrpTar tar;
	tar.setdir(Backup_Path);
	tar.setfn(Full_FileName);
	tar.backup_name = Backup_Name;
#ifndef TW_EXCLUDE_ENCRYPTED_BACKUPS
	string Password;
	DataManager::GetValue("tw_restore_password", Password);
	if (!Password.empty())
		tar.setpassword(Password);
#endif
	if (tar.extractTarFork(total_restore_size, already_restored_size) != 0)
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
	return ret;
}

bool TWPartition::Restore_Image(string restore_folder, const unsigned long long *total_restore_size, unsigned long long *already_restored_size, string Restore_File_System) {
	string Full_FileName;
	double display_percent, progress_percent;
	char size_progress[1024];

	TWFunc::GUI_Operation_Text(TW_RESTORE_TEXT, Backup_Display_Name, gui_parse_text("{@restoring_hdr}"));
	gui_msg(Msg("restoring=Restoring {1}...")(Backup_Display_Name));
	Full_FileName = restore_folder + "/" + Backup_FileName;

	if (Restore_File_System == "emmc") {
		if (!Flash_Image_DD(Full_FileName))
			return false;
	} else if (Restore_File_System == "mtd" || Restore_File_System == "bml") {
		if (!Flash_Image_FI(Full_FileName))
			return false;
	}
	display_percent = (double)(Restore_Size + *already_restored_size) / (double)(*total_restore_size) * 100;
	sprintf(size_progress, "%lluMB of %lluMB, %i%%", (Restore_Size + *already_restored_size) / 1048576, *total_restore_size / 1048576, (int)(display_percent));
	DataManager::SetValue("tw_size_progress", size_progress);
	progress_percent = (display_percent / 100);
	DataManager::SetProgress((float)(progress_percent));
	*already_restored_size += Restore_Size;
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

bool TWPartition::Flash_Image(string Filename) {
	string Restore_File_System;

	LOGINFO("Image filename is: %s\n", Filename.c_str());

	if (Backup_Method == FILES) {
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
		unsigned long long image_size = TWFunc::Get_File_Size(Filename);
		if (image_size > Size) {
			LOGINFO("Size (%llu bytes) of image '%s' is larger than target device '%s' (%llu bytes)\n",
				image_size, Filename.c_str(), Actual_Block_Device.c_str(), Size);
			gui_err("img_size_err=Size of image is larger than target device");
			return false;
		}
		if (Backup_Method == DD)
			return Flash_Image_DD(Filename);
		else if (Backup_Method == FLASH_UTILS)
			return Flash_Image_FI(Filename);
	}

	LOGERR("Unknown flash method for '%s'\n", Mount_Point.c_str());
	return false;
}

bool TWPartition::Flash_Image_DD(string Filename) {
	string Command;

	gui_msg(Msg("flashing=Flashing {1}...")(Display_Name));

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
	if (magic == SPARSE_HEADER_MAGIC) {
		Command = "simg2img '" + Filename + "' " + Actual_Block_Device;
	} else {
		Command = "dd bs=8388608 if='" + Filename + "' of=" + Actual_Block_Device;
	}
	LOGINFO("Flash command: '%s'\n", Command.c_str());
	TWFunc::Exec_Cmd(Command);
	return true;
}

bool TWPartition::Flash_Image_FI(string Filename) {
	string Command;

	gui_msg(Msg("flashing=Flashing {1}...")(Display_Name));
	// Sometimes flash image doesn't like to flash due to the first 2KB matching, so we erase first to ensure that it flashes
	Command = "erase_image " + MTD_Name;
	LOGINFO("Erase command: '%s'\n", Command.c_str());
	TWFunc::Exec_Cmd(Command);
	Command = "flash_image " + MTD_Name + " '" + Filename + "'";
	LOGINFO("Flash command: '%s'\n", Command.c_str());
	TWFunc::Exec_Cmd(Command);
	return true;
}

void TWPartition::Change_Mount_Read_Only(bool new_value) {
	Mount_Read_Only = new_value;
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
			if (!Mount(false)) {
				LOGERR("Failed to mount decrypted adopted storage device\n");
				Is_Decrypted = false;
				Is_Encrypted = false;
				cryptfs_revert_ext_volume(part_guid);
				ret = 1;
			} else {
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
