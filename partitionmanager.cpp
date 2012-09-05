/* Partition Management classes for TWRP
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

int TWPartitionManager::Process_Fstab(string Fstab_Filename, bool Display_Error) {
	LOGI("STUB TWPartitionManager::Process_Fstab, Fstab_Filename: '%s', Display_Error: %i\n", Fstab_Filename.c_str(), Display_Error);
	return 1;
}

int TWPartitionManager::Mount_By_Path(string Path, bool Display_Error) {
	LOGI("STUB TWPartitionManager::Mount_By_Path, Path: '%s', Display_Error: %i\n", Path.c_str(), Display_Error);
	return 1;
}

int TWPartitionManager::Mount_By_Block(string Block, bool Display_Error) {
	LOGI("STUB TWPartitionManager::Mount_By_Block, Block: '%s', Display_Error: %i\n", Block.c_str(), Display_Error);
	return 1;
}

int TWPartitionManager::Mount_By_Name(string Name, bool Display_Error) {
	LOGI("STUB TWPartitionManager::Mount_By_Path, Name: '%s', Display_Error: %i\n", Name.c_str(), Display_Error);
	return 1;
}

int TWPartitionManager::UnMount_By_Path(string Path, bool Display_Error) {
	LOGI("STUB TWPartitionManager::UnMount_By_Path, Path: '%s', Display_Error: %i\n", Path.c_str(), Display_Error);
	return 1;
}

int TWPartitionManager::UnMount_By_Block(string Block, bool Display_Error) {
	LOGI("STUB TWPartitionManager::UnMount_By_Block, Block: '%s', Display_Error: %i\n", Block.c_str(), Display_Error);
	return 1;
}

int TWPartitionManager::UnMount_By_Name(string Name, bool Display_Error) {
	LOGI("STUB TWPartitionManager::UnMount_By_Name, Name: '%s', Display_Error: %i\n", Name.c_str(), Display_Error);
	return 1;
}

int TWPartitionManager::Is_Mounted_By_Path(string Path) {
	LOGI("STUB TWPartitionManager::Is_Mounted_By_Path, Path: '%s'\n", Path.c_str());
	return 1;
}

int TWPartitionManager::Is_Mounted_By_Block(string Block) {
	LOGI("STUB TWPartitionManager::Is_Mounted_By_Block, Block: '%s'\n", Block.c_str());
	return 1;
}

int TWPartitionManager::Is_Mounted_By_Name(string Name) {
	LOGI("STUB TWPartitionManager::Is_Mounted_By_Name, Name: '%s'\n", Name.c_str());
	return 1;
}

int TWPartitionManager::Mount_Current_Storage(void) {
	LOGI("STUB TWPartitionManager::Mount_Current_Storage\n");
	return 1;
}

/**TWPartition TWPartitionManager::Find_Partition_By_Path(string Path) {
	LOGI("STUB TWPartitionManager::Find_Partition_By_Path, Path: '%s'\n", Path.c_str());
	return NULL;
}

*TWPartition TWPartitionManager::Find_Partition_By_Block(string Block) {
	LOGI("STUB TWPartitionManager::Find_Partition_By_Block, Block: '%s'\n", Block.c_str());
	return NULL;
}*/

int TWPartitionManager::Run_Backup(string Backup_Name) {
	LOGI("STUB TWPartitionManager::Run_Backup, Backup_Name: '%s'\n", Backup_Name.c_str());
	return 1;
}

int TWPartitionManager::Run_Restore(string Restore_Name) {
	LOGI("STUB TWPartitionManager::Run_Restore, Restore_Name: '%s'\n", Restore_Name.c_str());
	return 1;
}

void TWPartitionManager::Set_Restore_Files(string Restore_Name) {
	LOGI("STUB TWPartitionManager::Set_Restore_Files\n");
	return;
}

int TWPartitionManager::Wipe_By_Path(string Path) {
	LOGI("STUB TWPartitionManager::Wipe_By_Path, Path: '%s'\n", Path.c_str());
	return 1;
}

int TWPartitionManager::Wipe_By_Block(string Block) {
	LOGI("STUB TWPartitionManager::Wipe_By_Block, Block: '%s'\n", Block.c_str());
	return 1;
}

int TWPartitionManager::Wipe_By_Name(string Name) {
	LOGI("STUB TWPartitionManager::Wipe_By_Name, Name: '%s'\n", Name.c_str());
	return 1;
}

int TWPartitionManager::Factory_Reset(void) {
	LOGI("STUB TWPartitionManager::Factory_Reset\n");
	return 1;
}

void TWPartitionManager::Refresh_Sizes(void) {
	LOGI("STUB TWPartitionManager::Refresh_Sizes\n");
	return;
}

void TWPartitionManager::Update_System_Details(void) {
	LOGI("STUB TWPartitionManager::Update_System_Details\n");
	return;
}

int TWPartitionManager::Decrypt_Device(string Password) {
	LOGI("STUB TWPartitionManager::Decrypt_Device, Password: '%s'\n", Password.c_str());
	return 1;
}