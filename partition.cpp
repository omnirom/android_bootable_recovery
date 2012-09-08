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

#include "variables.h"
#include "common.h"
#include "partitions.hpp"
#include "data.hpp"
extern "C" {
	#include "extra-functions.h"
	int __system(const char *command);
}

TWPartition::TWPartition(void) {
	Can_Be_Mounted = false;
	Can_Be_Wiped = false;
	Wipe_During_Factory_Reset = false;
	Wipe_Available_in_GUI = false;
	Is_SubPartition = false;
	SubPartition_Of = "";
	Symlink_Path = "";
	Symlink_Mount_Point = "";
	Mount_Point = "";
	Block_Device = "";
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
	Backup_Method = NONE;
	Has_Data_Media = false;
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
	string mount_pt(full_line);
	Mount_Point = mount_pt;
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
			if (*ptr != '/') {
				if (Display_Error)
					LOGE("Invalid block device on '%s', '%s', %i\n", Line.c_str(), ptr, index);
				else
					LOGI("Invalid block device on '%s', '%s', %i\n", Line.c_str(), ptr, index);
				return 0;
			}
			Block_Device = ptr;
			Find_Real_Block_Device(Block_Device, Display_Error);
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
			} else {
				// Unhandled data
				LOGI("Unhandled fstab information: '%s', %i\n", ptr, index);
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
			Make_Dir("/sdcard", Display_Error);
			Make_Dir("/emmc", Display_Error);
#endif
#ifdef TW_INCLUDE_CRYPTO
			Can_Be_Encrypted = true;
			if (!Mount(false)) {
				Is_Encrypted = true;
				Is_Decrypted = false;
				DataManager::SetValue(TW_IS_ENCRYPTED, 1);
				DataManager::SetValue(TW_CRYPTO_PASSWORD, "");
				DataManager::SetValue("tw_crypto_display", "");
			}
#endif
		} else if (Mount_Point == "/cache") {
			Display_Name = "Cache";
			Wipe_Available_in_GUI = true;
			Wipe_During_Factory_Reset = true;
			Update_Size(Display_Error);
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
		} else
			Update_Size(Display_Error);
	} else if (Is_Image(Fstab_File_System)) {
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
	if (!Path_Exists(Path)) {
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

	// Check to see if the block device exists
	if (Path_Exists(Block_Device)) {
		Is_Present = true;
	} else if (Alternate_Block_Device != "" && Path_Exists(Alternate_Block_Device)) {
		Flip_Block_Device();
		Is_Present = true;
	}
	// Make the mount point folder if it doesn't exist
	Make_Dir(Mount_Point, Display_Error);
	Display_Name = Mount_Point.substr(1, Mount_Point.size() - 1);
	Backup_Name = Display_Name;
	Backup_Method = FILES;
}

void TWPartition::Setup_Image(bool Display_Error) {
	if (Path_Exists(Block_Device)) {
		Is_Present = true;
	} else if (Alternate_Block_Device != "" && Path_Exists(Alternate_Block_Device)) {
		Flip_Block_Device();
		Is_Present = true;
	}
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
			LOGE("Unable to find parition size for '%s'\n", Block_Device.c_str());
		else
			LOGI("Unable to find parition size for '%s'\n", Block_Device.c_str());
	}
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

	min_len = Block_Device.size() + 2;
	sprintf(command, "df %s > /tmp/dfoutput.txt", Mount_Point.c_str());
	__system(command);
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

unsigned long long TWPartition::Get_Folder_Size(string Path, bool Display_Error) {
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

		if (strlen(line) < 7 || line[0] == 'm')     continue;
		sscanf(line + 1, "%lu %lu %lu %s", &major, &minor, &blocks, device);

		tmpdevice = "/dev/block/";
		tmpdevice += device;
		if (tmpdevice == Block_Device || tmpdevice == Alternate_Block_Device) {
			// Adjust block size to byte size
			Size = blocks * 1024ULL;
			fclose(fp);
			return true;
		}
	}
	fclose(fp);
	return false;
}

bool TWPartition::Path_Exists(string Path) {
	// Check to see if the Path exists
	struct statfs st;

	if (statfs(Path.c_str(), &st) != 0)
		return false;
	else
		return true;
}

void TWPartition::Flip_Block_Device(void) {
	string temp;

	temp = Alternate_Block_Device;
	Block_Device = Alternate_Block_Device;
	Alternate_Block_Device = temp;
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
	if (Removable)
		Check_FS_Type();
	if (Is_Decrypted) {
		if (mount(Decrypted_Block_Device.c_str(), Mount_Point.c_str(), Current_File_System.c_str(), 0, NULL) != 0) {
			Check_FS_Type();
			if (mount(Decrypted_Block_Device.c_str(), Mount_Point.c_str(), Current_File_System.c_str(), 0, NULL) != 0) {
				if (Display_Error)
					LOGE("Unable to mount decrypted block device '%s' to '%s'\n", Decrypted_Block_Device.c_str(), Mount_Point.c_str());
				else
					LOGI("Unable to mount decrypted block device '%s' to '%s'\n", Decrypted_Block_Device.c_str(), Mount_Point.c_str());
				return false;
			} else {
				if (Removable)
					Update_Size(Display_Error);
				return true;
			}
		} else {
			if (Removable)
				Update_Size(Display_Error);
			return true;
		}
	}
	if (mount(Block_Device.c_str(), Mount_Point.c_str(), Current_File_System.c_str(), 0, NULL) != 0) {
		Check_FS_Type();
		if (mount(Block_Device.c_str(), Mount_Point.c_str(), Current_File_System.c_str(), 0, NULL) != 0) {
			if (Alternate_Block_Device != "" && Path_Exists(Alternate_Block_Device)) {
				Flip_Block_Device();
				Check_FS_Type();
				if (mount(Block_Device.c_str(), Mount_Point.c_str(), Current_File_System.c_str(), 0, NULL) != 0) {
					if (Display_Error)
						LOGE("Unable to mount '%s'\n", Mount_Point.c_str());
					else
						LOGI("Unable to mount '%s'\n", Mount_Point.c_str());
					return false;
				} else {
					if (Removable)
						Update_Size(Display_Error);
					return true;
				}
			} else
				return false;
		} else {
			if (Removable)
				Update_Size(Display_Error);
			return true;
		}
	}
	if (Removable)
		Update_Size(Display_Error);
	return true;
}

bool TWPartition::UnMount(bool Display_Error) {
	if (Is_Mounted()) {
		int never_unmount_system;

		DataManager::GetValue(TW_DONT_UNMOUNT_SYSTEM, never_unmount_system);
		if (never_unmount_system == 1 && Mount_Point == "/system")
			return true; // Never unmount system if you're not supposed to unmount it

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
	LOGI("STUB TWPartition::Wipe\n");
	return 1;
}

bool TWPartition::Backup(string backup_folder) {
	LOGI("STUB TWPartition::Backup, backup_folder: '%s'\n", backup_folder.c_str());
	return 1;
}

bool TWPartition::Restore(string restore_folder) {
	LOGI("STUB TWPartition::Restore, restore_folder: '%s'\n", restore_folder.c_str());
	return 1;
}

string TWPartition::Backup_Method_By_Name() {
	LOGI("STUB TWPartition::Backup_Method_By_Name\n");
	return "STUB";
}

bool TWPartition::Decrypt(string Password) {
	LOGI("STUB TWPartition::Decrypt, password: '%s'\n", Password.c_str());
	return 1;
}

bool TWPartition::Wipe_Encryption() {
	LOGI("STUB TWPartition::Wipe_Encryption\n");
	return 1;
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

	if (Is_Decrypted)
		blkCommand = "blkid " + Decrypted_Block_Device + " > /tmp/blkidoutput.txt";
	else
		blkCommand = "blkid " + Block_Device + " > /tmp/blkidoutput.txt";

	__system(blkCommand.c_str());
	fp = fopen("/tmp/blkidoutput.txt", "rt");
	while (fgets(blkOutput, sizeof(blkOutput), fp) != NULL)
	{
		blk = blkOutput;
		ptr = blkOutput;
		while (*ptr > 32 && *ptr != ':')        ptr++;
		if (*ptr == 0)                          continue;
		*ptr = 0;

		// Increment by two, but verify that we don't hit a NULL
		ptr++;
		if (*ptr != 0)      ptr++;

		// Now, find the TYPE field
		while (1)
		{
			arg = ptr;
			while (*ptr > 32)       ptr++;
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
	__pclose(fp);
	return;
}

bool TWPartition::Wipe_EXT23() {
	LOGI("STUB TWPartition::Wipe_EXT23\n");
	return 1;
}

bool TWPartition::Wipe_EXT4() {
	LOGI("STUB TWPartition::Wipe_EXT4\n");
	return 1;
}

bool TWPartition::Wipe_FAT() {
	LOGI("STUB TWPartition::Wipe_FAT\n");
	return 1;
}

bool TWPartition::Wipe_YAFFS2() {
	LOGI("STUB TWPartition::Wipe_YAFFS2\n");
	return 1;
}

bool TWPartition::Wipe_RMRF() {
	LOGI("STUB TWPartition::Wipe_RMRF\n");
	return 1;
}

bool TWPartition::Wipe_Data_Without_Wiping_Media() {
	LOGI("STUB TWPartition::Wipe_Data_Without_Wiping_Media\n");
	return 1;
}

bool TWPartition::Backup_Tar(string backup_folder) {
	LOGI("STUB TWPartition::Backup_Tar, backup_folder: '%s'\n", backup_folder.c_str());
	return 1;
}

bool TWPartition::Backup_DD(string backup_folder) {
	LOGI("STUB TWPartition::Backup_DD, backup_folder: '%s'\n", backup_folder.c_str());
	return 1;
}

bool TWPartition::Backup_Dump_Image(string backup_folder) {
	LOGI("STUB TWPartition::Backup_Dump_Image, backup_folder: '%s'\n", backup_folder.c_str());
	return 1;
}

bool TWPartition::Restore_Tar(string restore_folder) {
	LOGI("STUB TWPartition::Restore_Tar, backup_folder: '%s'\n", restore_folder.c_str());
	return 1;
}

bool TWPartition::Restore_DD(string restore_folder) {
	LOGI("STUB TWPartition::Restore_DD, backup_folder: '%s'\n", restore_folder.c_str());
	return 1;
}

bool TWPartition::Restore_Flash_Image(string restore_folder) {
	LOGI("STUB TWPartition::Restore_Flash_Image, backup_folder: '%s'\n", restore_folder.c_str());
	return 1;
}

bool TWPartition::Update_Size(bool Display_Error) {
	bool ret = false;

	if (!Can_Be_Mounted)
		return false;

	if (!Mount(Display_Error))
		return false;

	ret = Get_Size_Via_statfs(Display_Error);
	if (!ret || Size == 0)
		if (!Get_Size_Via_df(Display_Error))
			return false;

	if (Has_Data_Media) {
		if (Mount(Display_Error)) {
			unsigned long long data_media_used, actual_data;
			data_media_used = Get_Folder_Size("/data/media", Display_Error);
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
	}
	return true;
}
