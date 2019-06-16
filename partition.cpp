/*
	Copyright 2013 to 2017 TeamWin
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

#include "cutils/properties.h"
#include "libblkid/include/blkid.h"
#include "variables.h"
#include "twcommon.h"
#include "partitions.hpp"
#include "data.hpp"
#include "twrp-functions.hpp"
#include "twrpTar.hpp"
#include "exclude.hpp"
#include "infomanager.hpp"
#include "set_metadata.h"
#include "gui/gui.hpp"
#include "adbbu/libtwadbbu.hpp"
#ifdef TW_INCLUDE_CRYPTO
	#include "crypto/fde/cryptfs.h"
	#ifdef TW_INCLUDE_FBE
		#include "crypto/ext4crypt/Decrypt.h"
	#endif
#else
	#define CRYPT_FOOTER_OFFSET 0x4000
#endif
extern "C" {
	#include "mtdutils/mtdutils.h"
	#include "mtdutils/mounts.h"
#ifdef USE_EXT4
	// #include "make_ext4fs.h" TODO need ifdef for android8
	#include <ext4_utils/make_ext4fs.h>
#endif
#ifdef TW_INCLUDE_CRYPTO
	#include "gpt/gpt.h"
#endif
}
#include <selinux/selinux.h>
#include <selinux/label.h>
#ifdef HAVE_CAPABILITIES
#include <sys/capability.h>
#include <sys/xattr.h>
#include <linux/xattr.h>
#endif
#include <sparse_format.h>
#include "progresstracking.hpp"

using namespace std;

static int auto_index = 0; // v2 fstab allows you to specify a mount point of "auto" with no /. These items are given a mount point of /auto* where * == auto_index

extern struct selabel_handle *selinux_handle;
extern bool datamedia;

struct flag_list {
	const char *name;
	unsigned long flag;
};

const struct flag_list mount_flags[] = {
	{ "noatime",          MS_NOATIME },
	{ "noexec",           MS_NOEXEC },
	{ "nosuid",           MS_NOSUID },
	{ "nodev",            MS_NODEV },
	{ "nodiratime",       MS_NODIRATIME },
	{ "ro",               MS_RDONLY },
	{ "rw",               0 },
	{ "remount",          MS_REMOUNT },
	{ "bind",             MS_BIND },
	{ "rec",              MS_REC },
#ifdef MS_UNBINDABLE
	{ "unbindable",       MS_UNBINDABLE },
#endif
#ifdef MS_PRIVATE
	{ "private",          MS_PRIVATE },
#endif
#ifdef MS_SLAVE
	{ "slave",            MS_SLAVE },
#endif
#ifdef MS_SHARED
	{ "shared",           MS_SHARED },
#endif
	{ "sync",             MS_SYNCHRONOUS },
	{ 0,                  0 },
};

const char *ignored_mount_items[] = {
	"defaults=",
	"errors=",
	"latemount",
	"sysfs_path=",
	NULL
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
	TWFLAG_FILEENCRYPTION,
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
	TWFLAG_SLOTSELECT,
	TWFLAG_WAIT,
	TWFLAG_VERIFY,
	TWFLAG_CHECK,
	TWFLAG_ALTDEVICE,
	TWFLAG_NOTRIM,
	TWFLAG_VOLDMANAGED,
	TWFLAG_FORMATTABLE,
	TWFLAG_RESIZE,
	TWFLAG_KEYDIRECTORY,
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
	{ "fileencryption=",        TWFLAG_FILEENCRYPTION },
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
	{ "slotselect",             TWFLAG_SLOTSELECT },
	{ "wait",                   TWFLAG_WAIT },
	{ "verify",                 TWFLAG_VERIFY },
	{ "check",                  TWFLAG_CHECK },
	{ "altdevice",              TWFLAG_ALTDEVICE },
	{ "notrim",                 TWFLAG_NOTRIM },
	{ "voldmanaged=",           TWFLAG_VOLDMANAGED },
	{ "formattable",            TWFLAG_FORMATTABLE },
	{ "resize",                 TWFLAG_RESIZE },
	{ "keydirectory=",          TWFLAG_KEYDIRECTORY },
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
	Wildcard_Block_Device = false;
	Sysfs_Entry = "";
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
	Is_FBE = false;
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
	Crypto_Key_Location = "";
	MTP_Storage_ID = 0;
	Can_Flash_Img = false;
	Mount_Read_Only = false;
	Is_Adopted_Storage = false;
	Adopted_GUID = "";
	SlotSelect = false;
	Key_Directory = "";
}

TWPartition::~TWPartition(void) {
	// Do nothing
}

bool TWPartition::Process_Fstab_Line(const char *fstab_line, bool Display_Error, std::map<string, Flags_Map> *twrp_flags) {
	char full_line[MAX_FSTAB_LINE_LENGTH];
	char twflags[MAX_FSTAB_LINE_LENGTH] = "";
	char* ptr;
	int line_len = strlen(fstab_line), index = 0, item_index = 0;
	bool skip = false;
	int fstab_version = 1, mount_point_index = 0, fs_index = 1, block_device_index = 2;
	TWPartition *additional_entry = NULL;
	std::map<string, Flags_Map>::iterator it;

	strlcpy(full_line, fstab_line, sizeof(full_line));
	for (index = 0; index < line_len; index++) {
		if (full_line[index] == 34)
			skip = !skip;
		if (!skip && full_line[index] <= 32)
			full_line[index] = '\0';
	}
	if (line_len < 10)
		return false; // There can't possibly be a valid fstab line that is less than 10 chars
	if (strncmp(fstab_line, "/dev/", strlen("/dev/")) == 0 || strncmp(fstab_line, "/devices/", strlen("/devices/")) == 0) {
		fstab_version = 2;
		block_device_index = 0;
		mount_point_index = 1;
		fs_index = 2;
	}

	index = 0;
	while (index < line_len) {
		while (index < line_len && full_line[index] == '\0')
			index++;
		if (index >= line_len)
			continue;
		ptr = full_line + index;
		if (item_index == mount_point_index) {
			Mount_Point = ptr;
			if (fstab_version == 2) {
				additional_entry = PartitionManager.Find_Partition_By_Path(Mount_Point);
				if (additional_entry) {
					LOGINFO("Found an additional entry for '%s'\n", Mount_Point.c_str());
				}
			}
			LOGINFO("Processing '%s'\n", Mount_Point.c_str());
			Backup_Path = Mount_Point;
			Storage_Path = Mount_Point;
			Display_Name = ptr + 1;
			Backup_Display_Name = Display_Name;
			Storage_Name = Display_Name;
			item_index++;
		} else if (item_index == fs_index) {
			// File System
			Fstab_File_System = ptr;
			Current_File_System = ptr;
			item_index++;
		} else if (item_index == block_device_index) {
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
		} else if (item_index > 2) {
			if (fstab_version == 2) {
				if (item_index == 3) {
					Process_FS_Flags(ptr);
					if (additional_entry) {
						additional_entry->Save_FS_Flags(Fstab_File_System, Mount_Flags, Mount_Options);
						return false; // We save the extra fs flags in the other partition entry and by returning false, this entry will be deleted
					}
				} else {
					strlcpy(twflags, ptr, sizeof(twflags));
				}
				item_index++;
			} else if (*ptr == '/') { // v2 fstab does not allow alternate block devices
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

	// override block devices from the v2 fstab with the ones we read from the twrp.flags file in case they are different
	if (fstab_version == 2 && twrp_flags && twrp_flags->size() > 0) {
		it = twrp_flags->find(Mount_Point);
		if (it != twrp_flags->end()) {
			if (!it->second.Primary_Block_Device.empty()) {
				Primary_Block_Device = it->second.Primary_Block_Device;
				Find_Real_Block_Device(Primary_Block_Device, Display_Error);
			}
			if (!it->second.Alternate_Block_Device.empty()) {
				Alternate_Block_Device = it->second.Alternate_Block_Device;
				Find_Real_Block_Device(Alternate_Block_Device, Display_Error);
			}
		}
	}

	if (strncmp(fstab_line, "/devices/", strlen("/devices/")) == 0) {
		Sysfs_Entry = Primary_Block_Device;
		Primary_Block_Device = "";
		Is_Storage = true;
		Removable = true;
		Wipe_Available_in_GUI = true;
		Wildcard_Block_Device = true;
	}
	if (Primary_Block_Device.find("*") != string::npos)
		Wildcard_Block_Device = true;

	if (Mount_Point == "auto") {
		Mount_Point = "/auto";
		char autoi[5];
		sprintf(autoi, "%i", auto_index);
		Mount_Point += autoi;
		Backup_Path = Mount_Point;
		Storage_Path = Mount_Point;
		auto_index++;
		Setup_File_System(Display_Error);
		Display_Name = "Storage";
		Backup_Display_Name = Display_Name;
		Storage_Name = Display_Name;
		Can_Be_Backed_Up = false;
		Wipe_Available_in_GUI = true;
		Is_Storage = true;
		Removable = true;
		Wipe_Available_in_GUI = true;
	} else if (!Is_File_System(Fstab_File_System) && !Is_Image(Fstab_File_System)) {
		if (Display_Error)
			LOGERR("Unknown File System: '%s'\n", Fstab_File_System.c_str());
		else
			LOGINFO("Unknown File System: '%s'\n", Fstab_File_System.c_str());
		return false;
	} else if (Is_File_System(Fstab_File_System)) {
		Find_Actual_Block_Device();
		Setup_File_System(Display_Error);
		if (Mount_Point == PartitionManager.Get_Android_Root_Path()) {
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
		Setup_Image();
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

		Process_TW_Flags(twflags, (fstab_version == 1), fstab_version);
		Save_FS_Flags(Fstab_File_System, Mount_Flags, Mount_Options);

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

	if (fstab_version == 2 && twrp_flags && twrp_flags->size() > 0) {
		it = twrp_flags->find(Mount_Point);
		if (it != twrp_flags->end()) {
			char twrpflags[MAX_FSTAB_LINE_LENGTH] = "";
			int skip = 0;
			string Flags = it->second.Flags;
			strcpy(twrpflags, Flags.c_str());
			if (strlen(twrpflags) > strlen("flags=") && strncmp(twrpflags, "flags=", strlen("flags=")) == 0)
				skip += strlen("flags=");
			char* flagptr = twrpflags;
			flagptr += skip;
			Process_TW_Flags(flagptr, Display_Error, 1); // Forcing the fstab to ver 1 because this data is coming from the /etc/twrp.flags which should be using the TWRP v1 flags format
		}
	}

	if (Mount_Point == "/persist" && Can_Be_Mounted) {
		bool mounted = Is_Mounted();
		if (mounted || Mount(false)) {
			// Read the backup settings file
			DataManager::LoadPersistValues();
			TWFunc::Fixup_Time_On_Boot("/persist/time/");
			if (!mounted)
				UnMount(false);
		}
	}

	return true;
}

void TWPartition::Partition_Post_Processing(bool Display_Error) {
	if (Mount_Point == "/data")
		Setup_Data_Partition(Display_Error);
	else if (Mount_Point == "/cache")
		Setup_Cache_Partition(Display_Error);
}

void TWPartition::ExcludeAll(const string& path) {
	backup_exclusions.add_absolute_dir(path);
	wipe_exclusions.add_absolute_dir(path);
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
		if (Key_Directory.empty())
			Is_FBE = false;
		else
			Is_FBE = true;
		DataManager::SetValue(TW_IS_FBE, 0);
		Decrypted_Block_Device = crypto_blkdev;
		LOGINFO("Data already decrypted, new block device: '%s'\n", crypto_blkdev);
	} else if (!Mount(false)) {
		if (Is_Present) {
			if (Key_Directory.empty()) {
				set_partition_data(Actual_Block_Device.c_str(), Crypto_Key_Location.c_str(), Fstab_File_System.c_str());
				if (cryptfs_check_footer() == 0) {
					Is_Encrypted = true;
					Is_Decrypted = false;
					Can_Be_Mounted = false;
					Current_File_System = "emmc";
					Setup_Image();
					DataManager::SetValue(TW_IS_ENCRYPTED, 1);
					DataManager::SetValue(TW_CRYPTO_PWTYPE, cryptfs_get_password_type());
					DataManager::SetValue(TW_CRYPTO_PASSWORD, "");
					DataManager::SetValue("tw_crypto_display", "");
				} else {
					gui_err("mount_data_footer=Could not mount /data and unable to find crypto footer.");
				}
			} else {
				Is_Encrypted = true;
				Is_Decrypted = false;
			}
		} else if (Key_Directory.empty()) {
			LOGERR("Primary block device '%s' for mount point '%s' is not present!\n", Primary_Block_Device.c_str(), Mount_Point.c_str());
		}
	} else {
		Decrypt_FBE_DE();
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

bool TWPartition::Decrypt_FBE_DE() {
if (TWFunc::Path_Exists("/data/unencrypted/key/version")) {
		LOGINFO("File Based Encryption is present\n");
#ifdef TW_INCLUDE_FBE
		ExcludeAll(Mount_Point + "/convert_fbe");
		ExcludeAll(Mount_Point + "/unencrypted");
		//ExcludeAll(Mount_Point + "/system/users/0"); // we WILL need to retain some of this if multiple users are present or we just need to delete more folders for the extra users somewhere else
		ExcludeAll(Mount_Point + "/misc/vold/user_keys");
		//ExcludeAll(Mount_Point + "/system_ce");
		//ExcludeAll(Mount_Point + "/system_de");
		//ExcludeAll(Mount_Point + "/misc_ce");
		//ExcludeAll(Mount_Point + "/misc_de");
		ExcludeAll(Mount_Point + "/system/gatekeeper.password.key");
		ExcludeAll(Mount_Point + "/system/gatekeeper.pattern.key");
		ExcludeAll(Mount_Point + "/system/locksettings.db");
		//ExcludeAll(Mount_Point + "/system/locksettings.db-shm"); // don't seem to need this one, but the other 2 are needed
		ExcludeAll(Mount_Point + "/system/locksettings.db-wal");
		//ExcludeAll(Mount_Point + "/user_de");
		//ExcludeAll(Mount_Point + "/misc/profiles/cur/0"); // might be important later
		ExcludeAll(Mount_Point + "/misc/gatekeeper");
		ExcludeAll(Mount_Point + "/misc/keystore");
		ExcludeAll(Mount_Point + "/drm/kek.dat");
		ExcludeAll(Mount_Point + "/system_de/0/spblob"); // contains data needed to decrypt pixel 2
		int retry_count = 3;
		while (!Decrypt_DE() && --retry_count)
			usleep(2000);
		if (retry_count > 0) {
			property_set("ro.crypto.state", "encrypted");
			Is_Encrypted = true;
			Is_Decrypted = false;
			Is_FBE = true;
			DataManager::SetValue(TW_IS_FBE, 1);
			DataManager::SetValue(TW_IS_ENCRYPTED, 1);
			string filename;
			int pwd_type = Get_Password_Type(0, filename);
			if (pwd_type < 0) {
				LOGERR("This TWRP does not have synthetic password decrypt support\n");
				pwd_type = 0; // default password
			}
			DataManager::SetValue(TW_CRYPTO_PWTYPE, pwd_type);
			DataManager::SetValue(TW_CRYPTO_PASSWORD, "");
			DataManager::SetValue("tw_crypto_display", "");
			return true;
		}
#else
		LOGERR("FBE found but FBE support not present in TWRP\n");
#endif
	}
	return false;
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
	for (ptr = strtok_r(options, ",", &savep); ptr; ptr = strtok_r(NULL, ",", &savep)) {
		char *equals = strstr(ptr, "=");
		size_t name_len;

		if (!equals)
			name_len = strlen(ptr);
		else
			name_len = equals - ptr;

		// There are some flags that we want to ignore in TWRP
		bool found_match = false;
		for (const char** ignored_mount_item = ignored_mount_items; *ignored_mount_item; ignored_mount_item++) {
			if (strncmp(ptr, *ignored_mount_item, name_len) == 0) {
				found_match = true;
				break;
			}
		}
		if (found_match)
			continue;

		// mount_flags are never postfixed by '='
		if (!equals) {
			const struct flag_list* mount_flag = mount_flags;
			for (; mount_flag->name; mount_flag++) {
				if (strcmp(ptr, mount_flag->name) == 0) {
					if (mount_flag->flag == MS_RDONLY)
						Mount_Read_Only = true;
					else
						Mount_Flags |= (unsigned)mount_flag->flag;
					found_match = true;
					break;
				}
			}
			if (found_match)
				continue;
		}

		// If we aren't ignoring this flag and it's not a mount flag, then it must be a mount option
		if (!Mount_Options.empty())
			Mount_Options += ",";
		Mount_Options += ptr;
	}
	free(options);
}

void TWPartition::Save_FS_Flags(const string& local_File_System, int local_Mount_Flags, const string& local_Mount_Options) {
	partition_fs_flags_struct flags;
	flags.File_System = local_File_System;
	flags.Mount_Flags = local_Mount_Flags;
	flags.Mount_Options = local_Mount_Options;
	fs_flags.push_back(flags);
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
		case TWFLAG_WAIT:
		case TWFLAG_VERIFY:
		case TWFLAG_CHECK:
		case TWFLAG_NOTRIM:
		case TWFLAG_VOLDMANAGED:
		case TWFLAG_RESIZE:
			// Do nothing
			break;
		case TWFLAG_DISPLAY:
			Display_Name = str;
			break;
		case TWFLAG_ENCRYPTABLE:
		case TWFLAG_FORCEENCRYPT:
			Crypto_Key_Location = str;
			break;
		case TWFLAG_FILEENCRYPTION:
			// This flag isn't used by TWRP but is needed in 9.0 FBE decrypt
			// fileencryption=ice:aes-256-heh
			{
				std::string FBE = str;
				size_t colon_loc = FBE.find(":");
				if (colon_loc == std::string::npos) {
					property_set("fbe.contents", FBE.c_str());
					property_set("fbe.filenames", "");
					LOGINFO("FBE contents '%s', filenames ''\n", FBE.c_str());
					break;
				}
				std::string FBE_contents, FBE_filenames;
				FBE_contents = FBE.substr(0, colon_loc);
				FBE_filenames = FBE.substr(colon_loc + 1);
				property_set("fbe.contents", FBE_contents.c_str());
				property_set("fbe.filenames", FBE_filenames.c_str());
				LOGINFO("FBE contents '%s', filenames '%s'\n", FBE_contents.c_str(), FBE_filenames.c_str());
			}
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
		case TWFLAG_FORMATTABLE:
			Wipe_Available_in_GUI = val;
			if (Wipe_Available_in_GUI)
				Can_Be_Wiped = true;
			break;
		case TWFLAG_SLOTSELECT:
			SlotSelect = true;
			break;
		case TWFLAG_ALTDEVICE:
			Alternate_Block_Device = str;
			break;
		case TWFLAG_KEYDIRECTORY:
			Key_Directory = str;
		default:
			// Should not get here
			LOGINFO("Flag identified for processing, but later unmatched: %i\n", flag);
			break;
	}
}

void TWPartition::Process_TW_Flags(char *flags, bool Display_Error, int fstab_ver) {
	char separator[2] = {'\n', 0};
	char *ptr, *savep;
	char source_separator = ';';

	if (fstab_ver == 2)
		source_separator = ',';

	// Semicolons within double-quotes are not forbidden, so replace
	// only the semicolons intended as separators with '\n' for strtok
	for (unsigned i = 0, skip = 0; i < strlen(flags); i++) {
		if (flags[i] == '\"')
			skip = !skip;
		if (!skip && flags[i] == source_separator)
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
		File_System == "squashfs" ||
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
	if (TWFunc::Get_D_Type_From_Stat(Path) != S_IFDIR)
		unlink(Path.c_str());
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
	Can_Be_Mounted = true;
	Can_Be_Wiped = true;

	// Make the mount point folder if it doesn't exist
	Make_Dir(Mount_Point, Display_Error);
	Display_Name = Mount_Point.substr(1, Mount_Point.size() - 1);
	Backup_Name = Display_Name;
	Backup_Method = BM_FILES;
}

void TWPartition::Setup_Image() {
	Display_Name = Mount_Point.substr(1, Mount_Point.size() - 1);
	Backup_Name = Display_Name;
	if (Current_File_System == "emmc")
		Backup_Method = BM_DD;
	else if (Current_File_System == "mtd" || Current_File_System == "bml")
		Backup_Method = BM_FLASH_UTILS;
	else
		LOGINFO("Unhandled file system '%s' on image '%s'\n", Current_File_System.c_str(), Display_Name.c_str());
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
		backup_exclusions.add_absolute_dir("/data/data/com.google.android.music/files");
		wipe_exclusions.add_absolute_dir(Mount_Point + "/misc/vold"); // adopted storage keys
		ExcludeAll(Mount_Point + "/.layout_version");
		ExcludeAll(Mount_Point + "/system/storage.xml");
	} else {
		if (Mount(true) && TWFunc::Path_Exists(Mount_Point + "/media/0")) {
			Storage_Path = Mount_Point + "/media/0";
			Symlink_Path = Storage_Path;
			UnMount(true);
		}
	}
	ExcludeAll(Mount_Point + "/media");
}

void TWPartition::Find_Real_Block_Device(string& Block, bool Display_Error) {
	char device[PATH_MAX], realDevice[PATH_MAX];

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
		if (never_unmount_system == 1 && Mount_Point == PartitionManager.Get_Android_Root_Path())
			return true; // Never unmount system if you're not supposed to unmount it

		if (Is_Storage && MTP_Storage_ID > 0)
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
			wiped = Wipe_EXTFS(New_File_System);
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

bool TWPartition::Restore(PartitionSettings *part_settings) {
	TWFunc::GUI_Operation_Text(TW_RESTORE_TEXT, Display_Name, gui_parse_text("{@restoring_hdr}"));
	LOGINFO("Restore filename is: %s/%s\n", part_settings->Backup_Folder.c_str(), Backup_FileName.c_str());

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
	bool ret = false;
	BasePartition* base_partition = make_partition();

	if (!base_partition->PreWipeEncryption())
		goto exit;

	Find_Actual_Block_Device();
	if (!Is_Present) {
		LOGINFO("Block device not present, cannot format %s.\n", Display_Name.c_str());
		gui_msg(Msg(msg::kError, "unable_to_wipe=Unable to wipe {1}.")(Display_Name));
		return false;
	}
	if (!UnMount(true))
		goto exit;

#ifdef TW_INCLUDE_CRYPTO
	if (Is_Decrypted && !Decrypted_Block_Device.empty()) {
		if (delete_crypto_blk_dev((char*)("userdata")) != 0) {
			LOGERR("Error deleting crypto block device, continuing anyway.\n");
		}
	}
#endif
	Has_Data_Media = false;
	Decrypted_Block_Device = "";
	Is_Decrypted = false;
	Is_Encrypted = false;
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
		ret = true;
		if (!Key_Directory.empty())
			ret = PartitionManager.Wipe_By_Path(Key_Directory);
		if (ret)
			ret = base_partition->PostWipeEncryption();
		goto exit;
	} else {
		Has_Data_Media = Save_Data_Media;
		gui_err("format_data_err=Unable to format to remove encryption.");
		if (Has_Data_Media && Mount(false))
			PartitionManager.Add_MTP_Storage(MTP_Storage_ID);
		goto exit;
	}
exit:
	delete base_partition;
	return ret;
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
	if (fs_flags.size() > 1) {
		std::vector<partition_fs_flags_struct>::iterator iter;
		std::vector<partition_fs_flags_struct>::iterator found = fs_flags.begin();

		for (iter = fs_flags.begin(); iter != fs_flags.end(); iter++) {
			if (iter->File_System == Current_File_System) {
				found = iter;
				break;
			}
		}
		// If we don't find a match, we default the flags to the first set of flags that we received from the fstab
		if (Mount_Flags != found->Mount_Flags || Mount_Options != found->Mount_Options) {
			Mount_Flags = found->Mount_Flags;
			Mount_Options = found->Mount_Options;
			LOGINFO("Mount_Flags: %i, Mount_Options: %s\n", Mount_Flags, Mount_Options.c_str());
		}
	}
}

bool TWPartition::Wipe_EXTFS(string File_System) {
#if PLATFORM_SDK_VERSION < 28
	if (!TWFunc::Path_Exists("/sbin/mke2fs"))
#else
	if (!TWFunc::Path_Exists("/sbin/mke2fs") || !TWFunc::Path_Exists("/sbin/e2fsdroid"))
#endif
		return Wipe_RMRF();

	int ret;
	bool NeedPreserveFooter = true;

	Find_Actual_Block_Device();
	if (!Is_Present) {
		LOGINFO("Block device not present, cannot wipe %s.\n", Display_Name.c_str());
		gui_msg(Msg(msg::kError, "unable_to_wipe=Unable to wipe {1}.")(Display_Name));
		return false;
	}
	if (!UnMount(true))
		return false;

	/**
	 * On decrypted devices, IOCTL_Get_Block_Size calculates size on device mapper,
	 * so there's no need to preserve footer.
	 */
	if ((Is_Decrypted && !Decrypted_Block_Device.empty()) ||
			Crypto_Key_Location != "footer") {
		NeedPreserveFooter = false;
	}

	unsigned long long dev_sz = TWFunc::IOCTL_Get_Block_Size(Actual_Block_Device.c_str());
	if (!dev_sz)
		return false;

	if (NeedPreserveFooter)
		Length < 0 ? dev_sz += Length : dev_sz -= CRYPT_FOOTER_OFFSET;

	char dout[16];
	sprintf(dout, "%llu", dev_sz / 4096);

	//string size_str =to_string(dev_sz / 4096);
	string size_str = dout;
	string Command;

	gui_msg(Msg("formatting_using=Formatting {1} using {2}...")(Display_Name)("mke2fs"));

	// Execute mke2fs to create empty ext4 filesystem
	Command = "mke2fs -t " + File_System + " -b 4096 " + Actual_Block_Device + " " + size_str;
	LOGINFO("mke2fs command: %s\n", Command.c_str());
	ret = TWFunc::Exec_Cmd(Command);
	if (ret) {
		gui_msg(Msg(msg::kError, "unable_to_wipe=Unable to wipe {1}.")(Display_Name));
		return false;
	}

	if (TWFunc::Path_Exists("/sbin/e2fsdroid")) {
		const string& File_Contexts_Entry = (Mount_Point == "/system_root" ? "/" : Mount_Point);
		char *secontext = NULL;
		if (!selinux_handle || selabel_lookup(selinux_handle, &secontext, File_Contexts_Entry.c_str(), S_IFDIR) < 0) {
			LOGINFO("Cannot lookup security context for '%s'\n", Mount_Point.c_str());
		} else {
			// Execute e2fsdroid to initialize selinux context
			Command = "e2fsdroid -e -S /file_contexts -a " + File_Contexts_Entry + " " + Actual_Block_Device;
			LOGINFO("e2fsdroid command: %s\n", Command.c_str());
			ret = TWFunc::Exec_Cmd(Command);
			if (ret) {
				gui_msg(Msg(msg::kError, "unable_to_wipe=Unable to wipe {1}.")(Display_Name));
				return false;
			}
		}
	} else {
		LOGINFO("e2fsdroid not present\n");
	}

	if (NeedPreserveFooter)
		Wipe_Crypto_Key();
	Current_File_System = File_System;
	Recreate_AndSec_Folder();
	gui_msg("done=Done.");
	return true;
}

bool TWPartition::Wipe_EXT4() {
#ifdef USE_EXT4
	int ret;
	bool NeedPreserveFooter = true;

	Find_Actual_Block_Device();
	if (!Is_Present) {
		LOGINFO("Block device not present, cannot wipe %s.\n", Display_Name.c_str());
		gui_msg(Msg(msg::kError, "unable_to_wipe=Unable to wipe {1}.")(Display_Name));
		return false;
	}
	if (!UnMount(true))
		return false;

	/**
	 * On decrypted devices, IOCTL_Get_Block_Size calculates size on device mapper,
	 * so there's no need to preserve footer.
	 */
	if ((Is_Decrypted && !Decrypted_Block_Device.empty()) ||
			Crypto_Key_Location != "footer") {
		NeedPreserveFooter = false;
	}

	unsigned long long dev_sz = TWFunc::IOCTL_Get_Block_Size(Actual_Block_Device.c_str());
	if (!dev_sz)
		return false;

	if (NeedPreserveFooter)
		Length < 0 ? dev_sz += Length : dev_sz -= CRYPT_FOOTER_OFFSET;

	char *secontext = NULL;

	gui_msg(Msg("formatting_using=Formatting {1} using {2}...")(Display_Name)("make_ext4fs"));

	if (!selinux_handle || selabel_lookup(selinux_handle, &secontext, Mount_Point.c_str(), S_IFDIR) < 0) {
		LOGINFO("Cannot lookup security context for '%s'\n", Mount_Point.c_str());
		ret = make_ext4fs(Actual_Block_Device.c_str(), dev_sz, Mount_Point.c_str(), NULL);
	} else {
		ret = make_ext4fs(Actual_Block_Device.c_str(), dev_sz, Mount_Point.c_str(), selinux_handle);
	}
	if (ret != 0) {
		gui_msg(Msg(msg::kError, "unable_to_wipe=Unable to wipe {1}.")(Display_Name));
		return false;
	} else {
		if (NeedPreserveFooter)
			Wipe_Crypto_Key();
		string sedir = Mount_Point + "/lost+found";
		PartitionManager.Mount_By_Path(sedir.c_str(), true);
		rmdir(sedir.c_str());
		mkdir(sedir.c_str(), S_IRWXU | S_IRWXG | S_IWGRP | S_IXGRP);
		return true;
	}
#else
	return Wipe_EXTFS("ext4");
#endif
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
		bool NeedPreserveFooter = true;

		Find_Actual_Block_Device();
		if (!Is_Present) {
			LOGINFO("Block device not present, cannot wipe %s.\n", Display_Name.c_str());
			gui_msg(Msg(msg::kError, "unable_to_wipe=Unable to wipe {1}.")(Display_Name));
			return false;
		}
		if (!UnMount(true))
			return false;

		/**
		 * On decrypted devices, IOCTL_Get_Block_Size calculates size on device mapper,
		 * so there's no need to preserve footer.
		 */
		if ((Is_Decrypted && !Decrypted_Block_Device.empty()) ||
				Crypto_Key_Location != "footer") {
			NeedPreserveFooter = false;
		}

		gui_msg(Msg("formatting_using=Formatting {1} using {2}...")(Display_Name)("mkfs.f2fs"));
		// First determine if we have the old mkfs.f2fs that uses "-r reserved_bytes"
		// or the new mkfs.f2fs that expects the number of sectors as the optional last argument
		// Note: some 7.1 trees have the old and some have the new.
		command = "mkfs.f2fs | grep \"reserved\" > /tmp/f2fsversiontest";
		TWFunc::Exec_Cmd(command, false); // no help argument so printing usage exits with an error code
		if (!TWFunc::Path_Exists("/tmp/f2fsversiontest")) {
			LOGINFO("Error determining mkfs.f2fs version\n");
			return false;
		}
		if (TWFunc::Get_File_Size("/tmp/f2fsversiontest") <= 0) {
			LOGINFO("Using newer mkfs.f2fs\n");
			unsigned long long dev_sz = TWFunc::IOCTL_Get_Block_Size(Actual_Block_Device.c_str());
			if (!dev_sz)
				return false;

			if (NeedPreserveFooter)
				Length < 0 ? dev_sz += Length : dev_sz -= CRYPT_FOOTER_OFFSET;

			char dev_sz_str[48];
			sprintf(dev_sz_str, "%llu", (dev_sz / 4096));
			command = "mkfs.f2fs -d1 -f -O encrypt -O quota -O verity -w 4096 " + Actual_Block_Device + " " + dev_sz_str;
			if (TWFunc::Path_Exists("/sbin/sload.f2fs")) {
				command += " && sload.f2fs -t /data " + Actual_Block_Device;
			}
		} else {
			LOGINFO("Using older mkfs.f2fs\n");
			command = "mkfs.f2fs -t 0";
			if (NeedPreserveFooter) {
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
		}
		LOGINFO("mkfs.f2fs command: %s\n", command.c_str());
		if (TWFunc::Exec_Cmd(command) == 0) {
			if (NeedPreserveFooter)
				Wipe_Crypto_Key();
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
			if (wipe_exclusions.check_skip_dirs(dir)) {
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
				if (unlink(dir.c_str()) != 0)
					LOGINFO("Unable to unlink '%s': %s\n", dir.c_str(), strerror(errno));
			}
		}
		closedir(d);

		return true;
	}
	gui_msg(Msg(msg::kError, "error_opening_strerr=Error opening: '{1}' ({2})")(Mount_Point)(strerror(errno)));
	return false;
}

void TWPartition::Wipe_Crypto_Key() {
	Find_Actual_Block_Device();
	if (Crypto_Key_Location.empty())
		return;
	else if (Crypto_Key_Location == "footer") {
		int fd = open(Actual_Block_Device.c_str(), O_RDWR);
		if (fd < 0) {
			gui_print_color("warning", "Unable to open '%s' to wipe crypto key\n", Actual_Block_Device.c_str());
			return;
		}

		unsigned int block_count;
		if ((ioctl(fd, BLKGETSIZE, &block_count)) == -1) {
			gui_print_color("warning", "Unable to get block size for wiping crypto footer.\n");
		} else {
			int newlen = Length < 0 ? -Length : CRYPT_FOOTER_OFFSET;
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
	} else {
		if (TWFunc::IOCTL_Get_Block_Size(Crypto_Key_Location.c_str()) >= 16384LLU) {
			string Command = "dd of='" + Crypto_Key_Location + "' if=/dev/zero bs=16384 count=1";
			TWFunc::Exec_Cmd(Command);
		} else {
			LOGINFO("Crypto key location reports size < 16K so not wiping crypto footer.\n");
		}
	}
}

bool TWPartition::Backup_Tar(PartitionSettings *part_settings, pid_t *tar_fork_pid) {
	string Full_FileName;
	twrpTar tar;

	if (!Mount(true))
		return false;

	TWFunc::GUI_Operation_Text(TW_BACKUP_TEXT, Backup_Display_Name, gui_parse_text("{@backing}"));
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
	Full_FileName = part_settings->Backup_Folder + "/" + Backup_FileName;
	if (Has_Data_Media)
		gui_msg(Msg(msg::kWarning, "backup_storage_warning=Backups of {1} do not include any files in internal storage such as pictures or downloads.")(Display_Name));
	tar.part_settings = part_settings;
	tar.backup_exclusions = &backup_exclusions;
	tar.setdir(Backup_Path);
	tar.setfn(Full_FileName);
	tar.setsize(Backup_Size);
	tar.partition_name = Backup_Name;
	tar.backup_folder = part_settings->Backup_Folder;
	if (tar.createTarFork(tar_fork_pid) != 0)
		return false;
	return true;
}

bool TWPartition::Backup_Image(PartitionSettings *part_settings) {
	string Full_FileName, adb_file_name;

	TWFunc::GUI_Operation_Text(TW_BACKUP_TEXT, Display_Name, gui_parse_text("{@backing}"));
	gui_msg(Msg("backing_up=Backing up {1}...")(Backup_Display_Name));

	Backup_FileName = Backup_Name + "." + Current_File_System + ".win";

	if (part_settings->adbbackup) {
		Full_FileName = TW_ADB_BACKUP;
		adb_file_name  = part_settings->Backup_Folder + "/" + Backup_FileName;
	}
	else
		Full_FileName = part_settings->Backup_Folder + "/" + Backup_FileName;

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
		else {
			destfn = part_settings->Backup_Folder + "/" + Backup_FileName;
		}
	}
	else {
		destfn = Actual_Block_Device;
		if (part_settings->adbbackup) {
			srcfn = TW_ADB_RESTORE;
		} else {
			srcfn = part_settings->Backup_Folder + "/" + Backup_FileName;
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
		if (read(src_fd, buffer, bs) != bs) {
			LOGINFO("Error reading source fd (%s)\n", strerror(errno));
			goto exit;
		}
		if (write(dest_fd, buffer, bs) != bs) {
			LOGINFO("Error writing destination fd (%s)\n", strerror(errno));
			goto exit;
		}
		backedup_size += (unsigned long long)(bs);
		Remain -= (unsigned long long)(bs);
		if (part_settings->progress)
			part_settings->progress->UpdateSize(backedup_size);
		if (PartitionManager.Check_Backup_Cancel() != 0)
			goto exit;
	}
	if (part_settings->progress)
		part_settings->progress->UpdateDisplayDetails(true);
	fsync(dest_fd);

	if (!part_settings->adbbackup && part_settings->PM_Method == PM_BACKUP) {
		tw_set_default_metadata(destfn.c_str());
		LOGINFO("Restored default metadata for %s\n", destfn.c_str());
	}

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

	TWFunc::GUI_Operation_Text(TW_BACKUP_TEXT, Display_Name, gui_parse_text("{@backing}"));
	gui_msg(Msg("backing_up=Backing up {1}...")(Backup_Display_Name));

	if (part_settings->progress)
		part_settings->progress->SetPartitionSize(Backup_Size);

	Backup_FileName = Backup_Name + "." + Current_File_System + ".win";
	Full_FileName = part_settings->Backup_Folder + "/" + Backup_FileName;

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
		InfoManager restore_info(part_settings->Backup_Folder + "/" + Backup_Name + ".info");
		if (restore_info.LoadValues() == 0) {
			if (restore_info.GetValue("backup_size", Restore_Size) == 0) {
				LOGINFO("Read info file, restore size is %llu\n", Restore_Size);
				return Restore_Size;
			}
		}
	}

	string Full_FileName = part_settings->Backup_Folder + "/" + Backup_FileName;
	string Restore_File_System = Get_Restore_File_System(part_settings);

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
	tar.backup_folder = part_settings->Backup_Folder;
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

	Full_FileName = part_settings->Backup_Folder + "/" + Backup_FileName;
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
	if (Mount_Point == PartitionManager.Get_Android_Root_Path() && Mount(true) && TWFunc::Path_Exists("/system/bin/run-as")) {
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
		Full_FileName = part_settings->Backup_Folder + "/" + Backup_FileName;

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

	Find_Actual_Block_Device();

	if (!Can_Be_Mounted && !Is_Encrypted) {
		if (TWFunc::Path_Exists(Actual_Block_Device) && Find_Partition_Size()) {
			Used = Size;
			Backup_Size = Size;
			return true;
		}
		return false;
	}

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
			Used = backup_exclusions.Get_Folder_Size(Mount_Point);
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
			Backup_Size = backup_exclusions.Get_Folder_Size(Backup_Path);
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

bool TWPartition::Find_Wildcard_Block_Devices(const string& Device) {
	int mount_point_index = 0; // we will need to create separate mount points for each partition found and we use this index to name each one
	string Path = TWFunc::Get_Path(Device);
	string Dev = TWFunc::Get_Filename(Device);
	size_t wildcard_index = Dev.find("*");
	if (wildcard_index != string::npos)
		Dev = Dev.substr(0, wildcard_index);
	wildcard_index = Dev.size();
	DIR* d = opendir(Path.c_str());
	if (d == NULL) {
		LOGINFO("Error opening '%s': %s\n", Path.c_str(), strerror(errno));
		return false;
	}
	struct dirent* de;
	while ((de = readdir(d)) != NULL) {
		if (de->d_type != DT_BLK || strlen(de->d_name) <= wildcard_index || strncmp(de->d_name, Dev.c_str(), wildcard_index) != 0)
			continue;

		string item = Path + "/";
		item.append(de->d_name);
		if (PartitionManager.Find_Partition_By_Block_Device(item))
			continue;
		TWPartition *part = new TWPartition;
		char buffer[MAX_FSTAB_LINE_LENGTH];
		sprintf(buffer, "%s %s-%i auto defaults defaults", item.c_str(), Mount_Point.c_str(), ++mount_point_index);
		part->Process_Fstab_Line(buffer, false, NULL);
		char display[MAX_FSTAB_LINE_LENGTH];
		sprintf(display, "%s %i", Storage_Name.c_str(), mount_point_index);
		part->Storage_Name = display;
		part->Display_Name = display;
		part->Primary_Block_Device = item;
		part->Wildcard_Block_Device = false;
		part->Is_SubPartition = true;
		part->SubPartition_Of = Mount_Point;
		part->Is_Storage = Is_Storage;
		part->Can_Be_Mounted = true;
		part->Removable = true;
		part->Can_Be_Wiped = Can_Be_Wiped;
		part->Wipe_Available_in_GUI = Wipe_Available_in_GUI;
		part->Find_Actual_Block_Device();
		part->Update_Size(false);
		Has_SubPartition = true;
		PartitionManager.Output_Partition(part);
		PartitionManager.Add_Partition(part);
	}
	closedir(d);
	return (mount_point_index > 0);
}

void TWPartition::Find_Actual_Block_Device(void) {
	if (!Sysfs_Entry.empty() && Primary_Block_Device.empty() && Decrypted_Block_Device.empty()) {
		/* Sysfs_Entry.empty() indicates if this is a sysfs entry that begins with /device/
		 * If we have a syfs entry then we are looking for this device from a uevent add.
		 * The uevent add will set the primary block device based on the data we receive from
		 * after checking for adopted storage. If the device ends up being adopted, then the
		 * decrypted block device will be set instead of the primary block device. */
		Is_Present = false;
		return;
	}
	if (Wildcard_Block_Device && !Is_Adopted_Storage) {
		Is_Present = false;
		Actual_Block_Device = "";
		Can_Be_Mounted = false;
		if (!Find_Wildcard_Block_Devices(Primary_Block_Device)) {
			string Dev = Primary_Block_Device.substr(0, Primary_Block_Device.find("*"));
			if (TWFunc::Path_Exists(Dev)) {
				Is_Present = true;
				Can_Be_Mounted = true;
				Actual_Block_Device = Dev;
			}
		}
		return;
	} else if (Is_Decrypted && !Decrypted_Block_Device.empty()) {
		Actual_Block_Device = Decrypted_Block_Device;
		if (TWFunc::Path_Exists(Decrypted_Block_Device)) {
			Is_Present = true;
			return;
		}
	} else if (SlotSelect && TWFunc::Path_Exists(Primary_Block_Device + PartitionManager.Get_Active_Slot_Suffix())) {
		Actual_Block_Device = Primary_Block_Device + PartitionManager.Get_Active_Slot_Suffix();
		unlink(Primary_Block_Device.c_str());
		symlink(Actual_Block_Device.c_str(), Primary_Block_Device.c_str()); // we create a non-slot symlink pointing to the currently selected slot which may assist zips with installing
		Is_Present = true;
		return;
	} else if (TWFunc::Path_Exists(Primary_Block_Device)) {
		Is_Present = true;
		Actual_Block_Device = Primary_Block_Device;
		return;
	}
	if (!Alternate_Block_Device.empty() && TWFunc::Path_Exists(Alternate_Block_Device)) {
		Actual_Block_Device = Alternate_Block_Device;
		Is_Present = true;
	} else {
		Is_Present = false;
	}
}

void TWPartition::Recreate_Media_Folder(void) {
	string Command;
	string Media_Path = Mount_Point + "/media";

	if (Is_FBE) {
		LOGINFO("Not recreating media folder on FBE\n");
		return;
	}
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

		// Afterwards, we will try to set the
		// default metadata that we were hopefully able to get during
		// early boot.
		tw_set_default_metadata(Media_Path.c_str());
		if (!Internal_path.empty())
			tw_set_default_metadata(Internal_path.c_str());

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

	full_filename = part_settings->Backup_Folder + "/" + Backup_FileName;

	LOGINFO("Image filename is: %s\n", Backup_FileName.c_str());

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
				image_size, Backup_FileName.c_str(), Actual_Block_Device.c_str(), Size);
			gui_err("img_size_err=Size of image is larger than target device");
			return false;
		}
		if (Backup_Method == BM_DD) {
			if (!part_settings->adbbackup) {
				if (Is_Sparse_Image(full_filename)) {
					return Flash_Sparse_Image(full_filename);
				}
			}
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
		string temp = Actual_Block_Device;
		Find_Real_Block_Device(temp, false);
		string block = basename(temp.c_str());
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
	close(fd);
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

void TWPartition::Set_Backup_FileName(string fname) {
	Backup_FileName = fname;
}

string TWPartition::Get_Backup_Name() {
	return Backup_Name;
}
