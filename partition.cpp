/* Partition class for TWRP
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * The code was written from scratch by Dees_Troy dees_troy at
 * yahoo
 *
 * Copyright (c) 2012
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <sys/mount.h>
#include <unistd.h>
#include <dirent.h>

#ifdef TW_INCLUDE_CRYPTO
	#include "cutils/properties.h"
#endif

#include "variables.h"
#include "common.h"
#include "partitions.hpp"
#include "data.hpp"
#include "twrp-functions.hpp"
#include "makelist.hpp"
extern "C" {
	#include "mtdutils/mtdutils.h"
	#include "mtdutils/mounts.h"
}

TWPartition::TWPartition(void) {
	Can_Be_Mounted = false;
	Can_Be_Wiped = false;
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
	Backup_Name = "";
	Backup_FileName = "";
	MTD_Name = "";
	Backup_Method = NONE;
	Has_Data_Media = false;
	Has_Android_Secure = false;
	Is_Storage = false;
	Storage_Path = "";
	Current_File_System = "";
	Fstab_File_System = "";
	Format_Block_Size = 0;
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

	for (index = 0; index < line_len; index++) {
		if (full_line[index] <= 32)
			full_line[index] = '\0';
	}
	Mount_Point = full_line;
	LOGI("Processing '%s'\n", Mount_Point.c_str());
	Backup_Path = Mount_Point;
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
			} else if (*ptr != '/') {
				if (Display_Error)
					LOGE("Invalid block device on '%s', '%s', %i\n", Line.c_str(), ptr, index);
				else
					LOGI("Invalid block device on '%s', '%s', %i\n", Line.c_str(), ptr, index);
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
			} else if (strlen(ptr) == 4 && (strncmp(ptr, "NULL", 4) == 0 || strncmp(ptr, "null", 4) == 0 || strncmp(ptr, "null", 4) == 0)) {
				// Do nothing
			} else {
				// Unhandled data
				LOGI("Unhandled fstab information: '%s', %i, line: '%s'\n", ptr, index, Line.c_str());
			}
		}
		while (index < line_len && full_line[index] != '\0')
			index++;
	}

	if (!Is_File_System(Fstab_File_System) && !Is_Image(Fstab_File_System)) {
		if (Display_Error)
			LOGE("Unknown File System: '%s'\n", Fstab_File_System.c_str());
		else
			LOGI("Unknown File System: '%s'\n", Fstab_File_System.c_str());
		return 0;
	} else if (Is_File_System(Fstab_File_System)) {
		Find_Actual_Block_Device();
		Setup_File_System(Display_Error);
		if (Mount_Point == "/system") {
			Display_Name = "System";
			Wipe_Available_in_GUI = true;
		} else if (Mount_Point == "/data") {
			Display_Name = "Data";
			Wipe_Available_in_GUI = true;
			Wipe_During_Factory_Reset = true;
#ifdef RECOVERY_SDCARD_ON_DATA
			Has_Data_Media = true;
			Is_Storage = true;
			Storage_Path = "/data/media";
			if (strcmp(EXPAND(TW_EXTERNAL_STORAGE_PATH), "/sdcard") == 0) {
				Make_Dir("/emmc", Display_Error);
				Symlink_Path = "/data/media";
				Symlink_Mount_Point = "/emmc";
			} else {
				Make_Dir("/sdcard", Display_Error);
				Symlink_Path = "/data/media";
				Symlink_Mount_Point = "/sdcard";
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
				LOGI("Data already decrypted, new block device: '%s'\n", crypto_blkdev);
			} else if (!Mount(false)) {
				Is_Encrypted = true;
				Is_Decrypted = false;
				DataManager::SetValue(TW_IS_ENCRYPTED, 1);
				DataManager::SetValue(TW_CRYPTO_PASSWORD, "");
				DataManager::SetValue("tw_crypto_display", "");
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
			Wipe_Available_in_GUI = true;
			Wipe_During_Factory_Reset = true;
			if (Mount(false) && !TWFunc::Path_Exists("/cache/recovery/.")) {
				string Recreate_Command = "cd /cache && mkdir recovery";
				LOGI("Recreating /cache/recovery folder.\n");
				system(Recreate_Command.c_str());
			}
		} else if (Mount_Point == "/datadata") {
			Wipe_During_Factory_Reset = true;
			Display_Name = "DataData";
			Is_SubPartition = true;
			SubPartition_Of = "/data";
			DataManager::SetValue(TW_HAS_DATADATA, 1);
		} else if (Mount_Point == "/sd-ext") {
			Wipe_During_Factory_Reset = true;
			Display_Name = "SD-Ext";
			Wipe_Available_in_GUI = true;
			Removable = true;
		} else if (Mount_Point == "/boot") {
			Display_Name = "Boot";
			DataManager::SetValue("tw_boot_is_mountable", 1);
		}
#ifdef TW_EXTERNAL_STORAGE_PATH
		if (Mount_Point == EXPAND(TW_EXTERNAL_STORAGE_PATH)) {
			Is_Storage = true;
			Storage_Path = EXPAND(TW_EXTERNAL_STORAGE_PATH);
			Removable = true;
		}
#else
		if (Mount_Point == "/sdcard") {
			Is_Storage = true;
			Storage_Path = "/sdcard";
			Removable = true;
#ifndef RECOVERY_SDCARD_ON_DATA
			Setup_AndSec();
#endif
		}
#endif
#ifdef TW_INTERNAL_STORAGE_PATH
		if (Mount_Point == EXPAND(TW_INTERNAL_STORAGE_PATH)) {
			Is_Storage = true;
			Storage_Path = EXPAND(TW_INTERNAL_STORAGE_PATH);
#ifndef RECOVERY_SDCARD_ON_DATA
			Setup_AndSec();
#endif
		}
#else
		if (Mount_Point == "/emmc") {
			Is_Storage = true;
			Storage_Path = "/emmc";
#ifndef RECOVERY_SDCARD_ON_DATA
			Setup_AndSec();
#endif
		}
#endif
	} else if (Is_Image(Fstab_File_System)) {
		Find_Actual_Block_Device();
		Setup_Image(Display_Error);
		if (Mount_Point == "/boot") {
			int backup_display_size = (int)(Backup_Size / 1048576LLU);
			DataManager::SetValue(TW_BACKUP_BOOT_SIZE, backup_display_size);
			if (Backup_Size == 0) {
				DataManager::SetValue(TW_HAS_BOOT_PARTITION, 0);
				DataManager::SetValue(TW_BACKUP_BOOT_VAR, 0);
			} else
				DataManager::SetValue(TW_HAS_BOOT_PARTITION, 1);
		} else if (Mount_Point == "/recovery") {
			int backup_display_size = (int)(Backup_Size / 1048576LLU);
			DataManager::SetValue(TW_BACKUP_RECOVERY_SIZE, backup_display_size);
			if (Backup_Size == 0) {
				DataManager::SetValue(TW_HAS_RECOVERY_PARTITION, 0);
				DataManager::SetValue(TW_BACKUP_RECOVERY_VAR, 0);
			} else
				DataManager::SetValue(TW_HAS_RECOVERY_PARTITION, 1);
		}
#ifdef SP1_NAME
		string SP1_Path = "/";
		SP1_Path += EXPAND(SP1_NAME);
		if (Mount_Point == SP1_Path) {
			int backup_display_size = (int)(Backup_Size / 1048576LLU);
			DataManager::SetValue(TW_BACKUP_SP1_SIZE, backup_display_size);
		}
#endif
#ifdef SP2_NAME
		string SP2_Path = "/";
		SP2_Path += EXPAND(SP2_NAME);
		if (Mount_Point == SP2_Path) {
			int backup_display_size = (int)(Backup_Size / 1048576LLU);
			DataManager::SetValue(TW_BACKUP_SP2_SIZE, backup_display_size);
		}
#endif
#ifdef SP3_NAME
		string SP3_Path = "/";
		SP3_Path += EXPAND(SP3_NAME);
		if (Mount_Point == SP3_Path) {
			int backup_display_size = (int)(Backup_Size / 1048576LLU);
			DataManager::SetValue(TW_BACKUP_SP3_SIZE, backup_display_size);
		}
#endif
	}

	// Process any custom flags
	if (Flags.size() > 0)
		Process_Flags(Flags, Display_Error);

	return true;
}

bool TWPartition::Process_Flags(string Flags, bool Display_Error) {
	char flags[MAX_FSTAB_LINE_LENGTH];
	int flags_len, index = 0;
	char* ptr;

	strcpy(flags, Flags.c_str());
	flags_len = Flags.size();
	for (index = 0; index < flags_len; index++) {
		if (flags[index] == ';')
			flags[index] = '\0';
	}

	index = 0;
	while (index < flags_len) {
		while (index < flags_len && flags[index] == '\0')
			index++;
		if (index >= flags_len)
			continue;
		ptr = flags + index;
		if (strcmp(ptr, "removable") == 0) {
			Removable = true;
		} else if (strcmp(ptr, "storage") == 0) {
			Is_Storage = true;
		} else if (strcmp(ptr, "canbewiped") == 0) {
			Can_Be_Wiped = true;
		} else if (strcmp(ptr, "wipeingui") == 0) {
			Can_Be_Wiped = true;
			Wipe_Available_in_GUI = true;
		} else if (strcmp(ptr, "wipeduringfactoryreset") == 0) {
			Can_Be_Wiped = true;
			Wipe_Available_in_GUI = true;
			Wipe_During_Factory_Reset = true;
		} else if (strlen(ptr) > 15 && strncmp(ptr, "subpartitionof=", 15) == 0) {
			ptr += 13;
			Is_SubPartition = true;
			SubPartition_Of = ptr;
		} else if (strlen(ptr) > 8 && strncmp(ptr, "symlink=", 8) == 0) {
			ptr += 8;
			Symlink_Path = ptr;
		} else if (strlen(ptr) > 8 && strncmp(ptr, "display=", 8) == 0) {
			ptr += 8;
			Display_Name = ptr;
		} else if (strlen(ptr) > 10 && strncmp(ptr, "blocksize=", 10) == 0) {
			ptr += 10;
			Format_Block_Size = atoi(ptr);
		} else if (strlen(ptr) > 7 && strncmp(ptr, "length=", 7) == 0) {
			ptr += 7;
			Length = atoi(ptr);
		} else {
			if (Display_Error)
				LOGE("Unhandled flag: '%s'\n", ptr);
			else
				LOGI("Unhandled flag: '%s'\n", ptr);
		}
		while (index < flags_len && flags[index] != '\0')
			index++;
	}
	return true;
}

bool TWPartition::Is_File_System(string File_System) {
	if (File_System == "ext2" ||
		File_System == "ext3" ||
		File_System == "ext4" ||
		File_System == "vfat" ||
		File_System == "ntfs" ||
		File_System == "yaffs2" ||
		File_System == "auto")
		return true;
	else
		return false;
}

bool TWPartition::Is_Image(string File_System) {
	if (File_System == "emmc" ||
		File_System == "mtd")
		return true;
	else
		return false;
}

bool TWPartition::Make_Dir(string Path, bool Display_Error) {
	if (!TWFunc::Path_Exists(Path)) {
		if (mkdir(Path.c_str(), 0777) == -1) {
			if (Display_Error)
				LOGE("Can not create '%s' folder.\n", Path.c_str());
			else
				LOGI("Can not create '%s' folder.\n", Path.c_str());
			return false;
		} else {
			LOGI("Created '%s' folder.\n", Path.c_str());
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
	if (Fstab_File_System == "emmc")
		Backup_Method = DD;
	else if (Fstab_File_System == "mtd")
		Backup_Method = FLASH_UTILS;
	else
		LOGI("Unhandled file system '%s' on image '%s'\n", Fstab_File_System.c_str(), Display_Name.c_str());
	if (Find_Partition_Size()) {
		Used = Size;
		Backup_Size = Size;
	} else {
		if (Display_Error)
			LOGE("Unable to find parition size for '%s'\n", Mount_Point.c_str());
		else
			LOGI("Unable to find parition size for '%s'\n", Mount_Point.c_str());
	}
}

void TWPartition::Setup_AndSec(void) {
	Backup_Name = "and-sec";
	Has_Android_Secure = true;
	Symlink_Path = Mount_Point + "/.android_secure";
	Symlink_Mount_Point = "/and-sec";
	Backup_Path = Symlink_Mount_Point;
	Make_Dir("/and-sec", true);
	Recreate_AndSec_Folder();
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
			LOGE("Invalid symlink path '%s' found on block device '%s'\n", device, Block.c_str());
		else
			LOGI("Invalid symlink path '%s' found on block device '%s'\n", device, Block.c_str());
		return;
	} else {
		Block = device;
		return;
	}
}

bool TWPartition::Find_MTD_Block_Device(string MTD_Name) {
	FILE *fp = NULL;
	char line[255];

	fp = fopen("/proc/mtd", "rt");
	if (fp == NULL) {
		LOGE("Device does not support /proc/mtd\n");
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
				LOGE("Unable to statfs '%s'\n", Local_Path.c_str());
			else
				LOGI("Unable to statfs '%s'\n", Local_Path.c_str());
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
	system(command);
	fp = fopen("/tmp/dfoutput.txt", "rt");
	if (fp == NULL) {
		LOGI("Unable to open /tmp/dfoutput.txt.\n");
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
	if (Is_Mounted()) {
		return true;
	} else if (!Can_Be_Mounted) {
		return false;
	}

	Find_Actual_Block_Device();

	// Check the current file system before mounting
	Check_FS_Type();

	if (mount(Actual_Block_Device.c_str(), Mount_Point.c_str(), Current_File_System.c_str(), 0, NULL) != 0) {
		if (Display_Error)
			LOGE("Unable to mount '%s'\n", Mount_Point.c_str());
		else
			LOGI("Unable to mount '%s'\n", Mount_Point.c_str());
		LOGI("Actual block device: '%s', current file system: '%s'\n", Actual_Block_Device.c_str(), Current_File_System.c_str());
		return false;
	} else {
		if (Removable)
			Update_Size(Display_Error);

		if (!Symlink_Mount_Point.empty()) {
			string Command;

			Command = "mount " + Symlink_Path + " " + Symlink_Mount_Point;
			system(Command.c_str());
		}
		return true;
	}
	return true;
}

bool TWPartition::UnMount(bool Display_Error) {
	if (Is_Mounted()) {
		int never_unmount_system;

		DataManager::GetValue(TW_DONT_UNMOUNT_SYSTEM, never_unmount_system);
		if (never_unmount_system == 1 && Mount_Point == "/system")
			return true; // Never unmount system if you're not supposed to unmount it

		if (!Symlink_Mount_Point.empty())
			umount(Symlink_Mount_Point.c_str());

		if (umount(Mount_Point.c_str()) != 0) {
			if (Display_Error)
				LOGE("Unable to unmount '%s'\n", Mount_Point.c_str());
			else
				LOGI("Unable to unmount '%s'\n", Mount_Point.c_str());
			return false;
		} else
			return true;
	} else {
		return true;
	}
}

bool TWPartition::Wipe() {
	if (!Can_Be_Wiped) {
		LOGE("Partition '%s' cannot be wiped.\n", Mount_Point.c_str());
		return false;
	}

	if (Mount_Point == "/cache")
		tmplog_offset = 0;

	if (Has_Data_Media)
		return Wipe_Data_Without_Wiping_Media();

	int check;
	DataManager::GetValue(TW_RM_RF_VAR, check);
	if (check)
		return Wipe_RMRF();

	if (Current_File_System == "ext4")
		return Wipe_EXT4();

	if (Current_File_System == "ext2" || Current_File_System == "ext3")
		return Wipe_EXT23();

	if (Current_File_System == "vfat")
		return Wipe_FAT();

	if (Current_File_System == "yaffs2")
		return Wipe_MTD();

	LOGE("Unable to wipe '%s' -- unknown file system '%s'\n", Mount_Point.c_str(), Current_File_System.c_str());
	return false;
}

bool TWPartition::Wipe_AndSec(void) {
	if (!Has_Android_Secure)
		return false;

	char cmd[512];

	if (!Mount(true))
		return false;

	ui_print("Using rm -rf on .android_secure\n");
	sprintf(cmd, "rm -rf %s/.android_secure/* && rm -rf %s/.android_secure/.*", Mount_Point.c_str(), Mount_Point.c_str());

	LOGI("rm -rf command is: '%s'\n", cmd);
	system(cmd);
    return true;
}

bool TWPartition::Backup(string backup_folder) {
	if (Backup_Method == FILES)
		return Backup_Tar(backup_folder);
	else if (Backup_Method == DD)
		return Backup_DD(backup_folder);
	else if (Backup_Method == FLASH_UTILS)
		return Backup_Dump_Image(backup_folder);
	LOGE("Unknown backup method for '%s'\n", Mount_Point.c_str());
	return false;
}

bool TWPartition::Check_MD5(string restore_folder) {
	string Full_Filename;
	char split_filename[512];
	int index = 0;

	Full_Filename = restore_folder + "/" + Backup_FileName;
	if (!TWFunc::Path_Exists(Full_Filename)) {
		// This is a split archive, we presume
		sprintf(split_filename, "%s%03i", Full_Filename.c_str(), index);
		while (index < 1000 && TWFunc::Path_Exists(split_filename)) {
			if (TWFunc::Check_MD5(split_filename) == 0) {
				LOGE("MD5 failed to match on '%s'.\n", split_filename);
				return false;
			}
			index++;
			sprintf(split_filename, "%s%03i", Full_Filename.c_str(), index);
		}
		return true;
	} else {
		// Single file archive
		if (TWFunc::Check_MD5(Full_Filename) == 0) {
			LOGE("MD5 failed to match on '%s'.\n", split_filename);
			return false;
		} else
			return true;
	}
	return false;
}

bool TWPartition::Restore(string restore_folder) {
	if (Backup_Method == FILES)
		return Restore_Tar(restore_folder);
	else if (Backup_Method == DD)
		return Restore_DD(restore_folder);
	else if (Backup_Method == FLASH_UTILS)
		return Restore_Flash_Image(restore_folder);
	LOGE("Unknown restore method for '%s'\n", Mount_Point.c_str());
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
	LOGI("STUB TWPartition::Decrypt, password: '%s'\n", Password.c_str());
	// Is this needed?
	return 1;
}

bool TWPartition::Wipe_Encryption() {
	bool Save_Data_Media = Has_Data_Media;

	if (!UnMount(true))
		return false;

	Current_File_System = Fstab_File_System;
	Is_Encrypted = false;
	Is_Decrypted = false;
	Decrypted_Block_Device = "";
	Has_Data_Media = false;
	if (Wipe()) {
		Has_Data_Media = Save_Data_Media;
		if (Has_Data_Media && !Symlink_Mount_Point.empty()) {
			Recreate_Media_Folder();
		}
		ui_print("You may need to reboot recovery to be able to use /data again.\n");
		return true;
	} else {
		Has_Data_Media = Save_Data_Media;
		LOGE("Unable to format to remove encryption.\n");
		return false;
	}
	return false;
}

void TWPartition::Check_FS_Type() {
	FILE *fp;
	string blkCommand;
	char blkOutput[255];
	char* blk;
	char* arg;
	char* ptr;

	if (Fstab_File_System == "yaffs2" || Fstab_File_System == "mtd")
		return; // Running blkid on some mtd devices causes a massive crash

	Find_Actual_Block_Device();
	if (!Is_Present)
		return;

	if (TWFunc::Path_Exists("/tmp/blkidoutput.txt"))
		system("rm /tmp/blkidoutput.txt");

	blkCommand = "blkid " + Actual_Block_Device + " > /tmp/blkidoutput.txt";
	system(blkCommand.c_str());
	fp = fopen("/tmp/blkidoutput.txt", "rt");
	if (fp == NULL)
		return;
	while (fgets(blkOutput, sizeof(blkOutput), fp) != NULL)
	{
		blk = blkOutput;
		ptr = blkOutput;
		while (*ptr > 32 && *ptr != ':')		ptr++;
		if (*ptr == 0)						  continue;
		*ptr = 0;

		// Increment by two, but verify that we don't hit a NULL
		ptr++;
		if (*ptr != 0)	  ptr++;

		// Now, find the TYPE field
		while (1)
		{
			arg = ptr;
			while (*ptr > 32)	   ptr++;
			if (*ptr != 0)
			{
				*ptr = 0;
				ptr++;
			}

			if (strlen(arg) > 6)
			{
				if (memcmp(arg, "TYPE=\"", 6) == 0)  break;
			}

			if (*ptr == 0)
			{
				arg = NULL;
				break;
			}
		}

		if (arg && strlen(arg) > 7)
		{
			arg += 6;   // Skip the TYPE=" portion
			arg[strlen(arg)-1] = '\0';  // Drop the tail quote
		}
		else
			continue;

		if (strcmp(Current_File_System.c_str(), arg) != 0) {
			LOGI("'%s' was '%s' now set to '%s'\n", Mount_Point.c_str(), Current_File_System.c_str(), arg);
			Current_File_System = arg;
		}
	}
	fclose(fp);
	return;
}

bool TWPartition::Wipe_EXT23() {
	if (!UnMount(true))
		return false;

	if (TWFunc::Path_Exists("/sbin/mke2fs")) {
		char command[512];

		ui_print("Formatting %s using mke2fs...\n", Display_Name.c_str());
		Find_Actual_Block_Device();
		sprintf(command, "mke2fs -t %s -m 0 %s", Current_File_System.c_str(), Actual_Block_Device.c_str());
		LOGI("mke2fs command: %s\n", command);
		if (system(command) == 0) {
			Recreate_AndSec_Folder();
			ui_print("Done.\n");
			return true;
		} else {
			LOGE("Unable to wipe '%s'.\n", Mount_Point.c_str());
			return false;
		}
	} else
		return Wipe_RMRF();

	return false;
}

bool TWPartition::Wipe_EXT4() {
	if (!UnMount(true))
		return false;

	if (TWFunc::Path_Exists("/sbin/make_ext4fs")) {
		string Command;

		ui_print("Formatting %s using make_ext4fs...\n", Display_Name.c_str());
		Find_Actual_Block_Device();
		Command = "make_ext4fs";
		if (!Is_Decrypted && Length != 0) {
			// Only use length if we're not decrypted
			char len[32];
			sprintf(len, "%i", Length);
			Command += " -l ";
			Command += len;
		}
		Command += " " + Actual_Block_Device;
		LOGI("make_ext4fs command: %s\n", Command.c_str());
		if (system(Command.c_str()) == 0) {
			Recreate_AndSec_Folder();
			ui_print("Done.\n");
			return true;
		} else {
			LOGE("Unable to wipe '%s'.\n", Mount_Point.c_str());
			return false;
		}
	} else
		return Wipe_EXT23();

	return false;
}

bool TWPartition::Wipe_FAT() {
	char command[512];

	if (TWFunc::Path_Exists("/sbin/mkdosfs")) {
		if (!UnMount(true))
			return false;

		ui_print("Formatting %s using mkdosfs...\n", Display_Name.c_str());
		Find_Actual_Block_Device();
		sprintf(command,"mkdosfs %s", Actual_Block_Device.c_str()); // use mkdosfs to format it
		if (system(command) == 0) {
			Recreate_AndSec_Folder();
			ui_print("Done.\n");
			return true;
		} else {
			LOGE("Unable to wipe '%s'.\n", Mount_Point.c_str());
			return false;
		}
		return true;
	}
	else
		return Wipe_RMRF();

	return false;
}

bool TWPartition::Wipe_MTD() {
	if (!UnMount(true))
		return false;

	ui_print("MTD Formatting \"%s\"\n", MTD_Name.c_str());

    mtd_scan_partitions();
    const MtdPartition* mtd = mtd_find_partition_by_name(MTD_Name.c_str());
    if (mtd == NULL) {
        LOGE("No mtd partition named '%s'", MTD_Name.c_str());
        return false;
    }

    MtdWriteContext* ctx = mtd_write_partition(mtd);
    if (ctx == NULL) {
        LOGE("Can't write '%s', failed to format.", MTD_Name.c_str());
        return false;
    }
    if (mtd_erase_blocks(ctx, -1) == -1) {
        mtd_write_close(ctx);
        LOGE("Failed to format '%s'", MTD_Name.c_str());
        return false;
    }
    if (mtd_write_close(ctx) != 0) {
        LOGE("Failed to close '%s'", MTD_Name.c_str());
        return false;
    }
	Recreate_AndSec_Folder();
	ui_print("Done.\n");
    return true;
}

bool TWPartition::Wipe_RMRF() {
	char cmd[512];

	if (!Mount(true))
		return false;

	ui_print("Using rm -rf on '%s'\n", Mount_Point.c_str());
	sprintf(cmd, "rm -rf %s/* && rm -rf %s/.*", Mount_Point.c_str(), Mount_Point.c_str());

	LOGI("rm -rf command is: '%s'\n", cmd);
	system(cmd);
	Recreate_AndSec_Folder();
    return true;
}

bool TWPartition::Wipe_Data_Without_Wiping_Media() {
	char cmd[256];

	// This handles wiping data on devices with "sdcard" in /data/media
	if (!Mount(true))
		return false;

	ui_print("Wiping data without wiping /data/media ...\n");
	system("rm -f /data/*");
	system("rm -f /data/.*");

	DIR* d;
	d = opendir("/data");
	if (d != NULL)
	{
		struct dirent* de;
		while ((de = readdir(d)) != NULL) {
			if (strcmp(de->d_name, "media") == 0)   continue;

			sprintf(cmd, "rm -fr /data/%s", de->d_name);
			system(cmd);
		}
		closedir(d);
	}
	ui_print("Done.\n");
	return true;
}

bool TWPartition::Backup_Tar(string backup_folder) {
	char back_name[255], split_index[5];
	string Full_FileName, Split_FileName, Tar_Args, Command;
	int use_compression, index, backup_count;
	struct stat st;
	unsigned long long total_bsize = 0, file_size;

	if (!Mount(true))
		return false;

	if (Backup_Path == "/and-sec") {
		TWFunc::GUI_Operation_Text(TW_BACKUP_TEXT, "Android Secure", "Backing Up");
		ui_print("Backing up %s...\n", "Android Secure");
	} else {
		TWFunc::GUI_Operation_Text(TW_BACKUP_TEXT, Display_Name, "Backing Up");
		ui_print("Backing up %s...\n", Display_Name.c_str());
	}

	DataManager::GetValue(TW_USE_COMPRESSION_VAR, use_compression);
	if (use_compression)
		Tar_Args = "-cz";
	else
		Tar_Args = "-c";

	sprintf(back_name, "%s.%s.win", Backup_Name.c_str(), Current_File_System.c_str());
	Backup_FileName = back_name;

	if (Backup_Size > MAX_ARCHIVE_SIZE) {
		// This backup needs to be split into multiple archives
		ui_print("Breaking backup file into multiple archives...\nGenerating file lists\n");
		sprintf(back_name, "%s", Backup_Path.c_str());
		backup_count = MakeList::Make_File_List(back_name);
		if (backup_count < 1) {
			LOGE("Error generating file list!\n");
			return false;
		}
		for (index=0; index<backup_count; index++) {
			sprintf(split_index, "%03i", index);
			Full_FileName = backup_folder + "/" + Backup_FileName + split_index;
			Command = "tar " + Tar_Args + " -f '" + Full_FileName + "' -T /tmp/list/filelist" + split_index;
			LOGI("Backup command: '%s'\n", Command.c_str());
			ui_print("Backup archive %i of %i...\n", (index + 1), backup_count);
			system(Command.c_str()); // sending backup command formed earlier above

			file_size = TWFunc::Get_File_Size(Full_FileName);
			if (file_size == 0) {
				LOGE("Backup file size for '%s' is 0 bytes.\n", Full_FileName.c_str()); // oh noes! file size is 0, abort! abort!
				return false;
			}
			total_bsize += file_size;
		}
		ui_print(" * Total size: %llu bytes.\n", total_bsize);
		system("cd /tmp && rm -rf list");
	} else {
		Full_FileName = backup_folder + "/" + Backup_FileName;
		if (Has_Data_Media)
			Command = "cd " + Backup_Path + " && tar " + Tar_Args + " ./ --exclude='media*' -f '" + Full_FileName + "'";
		else
			Command = "cd " + Backup_Path + " && tar " + Tar_Args + " -f '" + Full_FileName + "' ./*";
		LOGI("Backup command: '%s'\n", Command.c_str());
		system(Command.c_str());
		if (TWFunc::Get_File_Size(Full_FileName) == 0) {
			LOGE("Backup file size for '%s' is 0 bytes.\n", Full_FileName.c_str());
			return false;
		}
	}
	return true;
}

bool TWPartition::Backup_DD(string backup_folder) {
	char back_name[255];
	string Full_FileName, Command;
	int use_compression;

	TWFunc::GUI_Operation_Text(TW_BACKUP_TEXT, Display_Name, "Backing Up");
	ui_print("Backing up %s...\n", Display_Name.c_str());

	sprintf(back_name, "%s.%s.win", Backup_Name.c_str(), Current_File_System.c_str());
	Backup_FileName = back_name;

	Full_FileName = backup_folder + "/" + Backup_FileName;

	Command = "dd if=" + Actual_Block_Device + " of='" + Full_FileName + "'";
	LOGI("Backup command: '%s'\n", Command.c_str());
	system(Command.c_str());
	if (TWFunc::Get_File_Size(Full_FileName) == 0) {
		LOGE("Backup file size for '%s' is 0 bytes.\n", Full_FileName.c_str());
		return false;
	}
	return true;
}

bool TWPartition::Backup_Dump_Image(string backup_folder) {
	char back_name[255];
	string Full_FileName, Command;
	int use_compression;

	TWFunc::GUI_Operation_Text(TW_BACKUP_TEXT, Display_Name, "Backing Up");
	ui_print("Backing up %s...\n", Display_Name.c_str());

	sprintf(back_name, "%s.%s.win", Backup_Name.c_str(), Current_File_System.c_str());
	Backup_FileName = back_name;

	Full_FileName = backup_folder + "/" + Backup_FileName;

	Command = "dump_image " + MTD_Name + " '" + Full_FileName + "'";
	LOGI("Backup command: '%s'\n", Command.c_str());
	system(Command.c_str());
	if (TWFunc::Get_File_Size(Full_FileName) == 0) {
		// Actual size may not match backup size due to bad blocks on MTD devices so just check for 0 bytes
		LOGE("Backup file size for '%s' is 0 bytes.\n", Full_FileName.c_str());
		return false;
	}
	return true;
}

bool TWPartition::Restore_Tar(string restore_folder) {
	size_t first_period, second_period;
	string Restore_File_System, Full_FileName, Command;
	int index = 0;
	char split_index[5];

	TWFunc::GUI_Operation_Text(TW_RESTORE_TEXT, Display_Name, "Restoring");
	LOGI("Restore filename is: %s\n", Backup_FileName.c_str());

	// Parse backup filename to extract the file system before wiping
	first_period = Backup_FileName.find(".");
	if (first_period == string::npos) {
		LOGE("Unable to find file system (first period).\n");
		return false;
	}
	Restore_File_System = Backup_FileName.substr(first_period + 1, Backup_FileName.size() - first_period - 1);
	second_period = Restore_File_System.find(".");
	if (second_period == string::npos) {
		LOGE("Unable to find file system (second period).\n");
		return false;
	}
	Restore_File_System.resize(second_period);
	LOGI("Restore file system is: '%s'.\n", Restore_File_System.c_str());
	Current_File_System = Restore_File_System;
	if (Has_Android_Secure) {
		ui_print("Wiping android secure...\n");
		if (!Wipe_AndSec())
			return false;
	} else if (!Wipe()) {
		ui_print("Wiping %s...\n", Display_Name.c_str());
		return false;
	}

	if (!Mount(true))
		return false;

	ui_print("Restoring %s...\n", Display_Name.c_str());
	Full_FileName = restore_folder + "/" + Backup_FileName;
	if (!TWFunc::Path_Exists(Full_FileName)) {
		// Backup is multiple archives
		LOGI("Backup is multiple archives.\n");
		sprintf(split_index, "%03i", index);
		Full_FileName = restore_folder + "/" + Backup_FileName + split_index;
		while (TWFunc::Path_Exists(Full_FileName)) {
			ui_print("Restoring archive %i...\n", index + 1);
			Command = "tar -xf '" + Full_FileName + "'";
			LOGI("Restore command: '%s'\n", Command.c_str());
			system(Command.c_str());
			index++;
			sprintf(split_index, "%03i", index);
			Full_FileName = restore_folder + "/" + Backup_FileName + split_index;
		}
		if (index == 0) {
			LOGE("Error locating restore file: '%s'\n", Full_FileName.c_str());
			return false;
		}
	} else {
		Command = "cd " + Backup_Path + " && tar -xf '" + Full_FileName + "'";
		LOGI("Restore command: '%s'\n", Command.c_str());
		system(Command.c_str());
	}
	return true;
}

bool TWPartition::Restore_DD(string restore_folder) {
	string Full_FileName, Command;

	TWFunc::GUI_Operation_Text(TW_RESTORE_TEXT, Display_Name, "Restoring");
	ui_print("Restoring %s...\n", Display_Name.c_str());
	Full_FileName = restore_folder + "/" + Backup_FileName;
	Command = "dd bs=4096 if='" + Full_FileName + "' of=" + Actual_Block_Device;
	LOGI("Restore command: '%s'\n", Command.c_str());
	system(Command.c_str());
	return true;
}

bool TWPartition::Restore_Flash_Image(string restore_folder) {
	string Full_FileName, Command;

	TWFunc::GUI_Operation_Text(TW_RESTORE_TEXT, Display_Name, "Restoring");
	ui_print("Restoring %s...\n", Display_Name.c_str());
	Full_FileName = restore_folder + "/" + Backup_FileName;
	// Sometimes flash image doesn't like to flash due to the first 2KB matching, so we erase first to ensure that it flashes
	Command = "erase_image " + MTD_Name;
	LOGI("Erase command: '%s'\n", Command.c_str());
	system(Command.c_str());
	Command = "flash_image " + MTD_Name + " '" + Full_FileName + "'";
	LOGI("Restore command: '%s'\n", Command.c_str());
	system(Command.c_str());
	return true;
}

bool TWPartition::Update_Size(bool Display_Error) {
	bool ret = false;

	if (!Can_Be_Mounted && !Is_Encrypted)
		return false;

	if (Removable || Is_Encrypted) {
		if (!Mount(false))
			return true;
	} else if (!Mount(Display_Error))
		return false;

	ret = Get_Size_Via_statfs(Display_Error);
	if (!ret || Size == 0)
		if (!Get_Size_Via_df(Display_Error))
			return false;

	if (Has_Data_Media) {
		if (Mount(Display_Error)) {
			unsigned long long data_media_used, actual_data;
			Used = TWFunc::Get_Folder_Size("/data", Display_Error);
			data_media_used = TWFunc::Get_Folder_Size("/data/media", Display_Error);
			actual_data = Used - data_media_used;
			Backup_Size = actual_data;
			int bak = (int)(Backup_Size / 1048576LLU);
			int total = (int)(Size / 1048576LLU);
			int us = (int)(Used / 1048576LLU);
			int fre = (int)(Free / 1048576LLU);
			int datmed = (int)(data_media_used / 1048576LLU);
			LOGI("Data backup size is %iMB, size: %iMB, used: %iMB, free: %iMB, in data/media: %iMB.\n", bak, total, us, fre, datmed);
		} else
			return false;
	} else if (Has_Android_Secure) {
		if (Mount(Display_Error))
			Backup_Size = TWFunc::Get_Folder_Size(Backup_Path, Display_Error);
		else
			return false;
	}
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
	} else if (!Alternate_Block_Device.empty() && TWFunc::Path_Exists(Alternate_Block_Device)) {
		Actual_Block_Device = Alternate_Block_Device;
		Is_Present = true;
	} else
		Is_Present = false;
}

void TWPartition::Recreate_Media_Folder(void) {
	string Command;

	if (!Mount(true)) {
		LOGE("Unable to recreate /data/media folder.\n");
	} else if (!TWFunc::Path_Exists("/data/media")) {
		LOGI("Recreating /data/media folder.\n");
		system("cd /data && mkdir media && chmod 755 media");
		Command = "umount " + Symlink_Mount_Point;
		system(Command.c_str());
		Command = "mount " + Symlink_Path + " " + Symlink_Mount_Point;
		system(Command.c_str());
	}
}

void TWPartition::Recreate_AndSec_Folder(void) {
	string Command;

	if (!Has_Android_Secure)
		return;

	if (!Mount(true)) {
		LOGE("Unable to recreate android secure folder.\n");
	} else if (!TWFunc::Path_Exists(Symlink_Path)) {
		LOGI("Recreating android secure folder.\n");
		Command = "umount " + Symlink_Mount_Point;
		system(Command.c_str());
		Command = "cd " + Mount_Point + " && mkdir .android_secure";
		system(Command.c_str());
		Command = "mount " + Symlink_Path + " " + Symlink_Mount_Point;
		system(Command.c_str());
	}
}
