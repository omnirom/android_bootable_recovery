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
#include <unistd.h>

#include "variables.h"
#include "common.h"
#include "partitions.hpp"

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

bool TWPartition::Process_Fstab_Line(string Line) {
	LOGI("STUB TWPartition::Process_Fstab_Line, Line: '%s'\n", Line.c_str());
	return 1;
}

bool TWPartition::Is_Mounted(void) {
	LOGI("STUB TWPartition::Is_Mounted\n");
	return 1;
}

bool TWPartition::Mount(bool Display_Error) {
	LOGI("STUB TWPartition::Mount, Display_Error: %i\n", Display_Error);
	if (Is_Mounted()) {
		return 1;
	} else {
		return 1;
	}
}

bool TWPartition::UnMount(bool Display_Error) {
	LOGI("STUB TWPartition::Mount, Display_Error: %i\n", Display_Error);
	if (Is_Mounted()) {
		return 1;
	} else {
		return 1;
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
	LOGI("STUB TWPartition::Check_FS_Type\n");
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
