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
#include <vector>

#include "variables.h"
#include "common.h"
#include "partitions.hpp"
#include "data.hpp"

#ifdef TW_INCLUDE_CRYPTO
	#ifdef TW_INCLUDE_JB_CRYPTO
		#include "crypto/jb/cryptfs.h"
	#else
		#include "crypto/ics/cryptfs.h"
	#endif
	#include "cutils/properties.h"
#endif

int TWPartitionManager::Process_Fstab(string Fstab_Filename, bool Display_Error) {
	FILE *fstabFile;
	char fstab_line[MAX_FSTAB_LINE_LENGTH];

	fstabFile = fopen(Fstab_Filename.c_str(), "rt");
	if (fstabFile == NULL) {
		LOGE("Critical Error: Unable to open fstab at '%s'.\n", Fstab_Filename.c_str());
		return false;
	}

	while (fgets(fstab_line, sizeof(fstab_line), fstabFile) != NULL) {
		if (fstab_line[0] != '/')
			continue;

		TWPartition* partition = new TWPartition();
		string line(fstab_line);
		if (partition->Process_Fstab_Line(line, Display_Error)) {
			Partitions.push_back(partition);
		} else {
			delete partition;
		}
	}
	fclose(fstabFile);
	if (!Write_Fstab()) {
		if (Display_Error)
			LOGE("Error creating fstab\n");
		else
			LOGI("Error creating fstab\n");
	}
	Update_System_Details();
	return true;
}

int TWPartitionManager::Write_Fstab(void) {
	FILE *fp;
	std::vector<TWPartition*>::iterator iter;
	string Line;

	fp = fopen("/etc/fstab", "w");
	if (fp == NULL) {
        LOGI("Can not open /etc/fstab.\n");
		return false;
	}
    for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		if ((*iter)->Can_Be_Mounted) {
			if ((*iter)->Is_Decrypted)
				Line = (*iter)->Decrypted_Block_Device + " " + (*iter)->Mount_Point + " " + (*iter)->Current_File_System + " rw\n";
			else
				Line = (*iter)->Block_Device + " " + (*iter)->Mount_Point + " " + (*iter)->Current_File_System + " rw\n";
			fputs(Line.c_str(), fp);
			// Handle subpartition tracking
			if ((*iter)->Is_SubPartition) {
				TWPartition* ParentPartition = Find_Partition_By_Path((*iter)->SubPartition_Of);
				if (ParentPartition)
					ParentPartition->Has_SubPartition = true;
				else
					LOGE("Unable to locate parent partition '%s' of '%s'\n", (*iter)->SubPartition_Of.c_str(), (*iter)->Mount_Point.c_str());
			}
		}
	}
	fclose(fp);
	return true;
}

int TWPartitionManager::Mount_By_Path(string Path, bool Display_Error) {
	std::vector<TWPartition*>::iterator iter;
	int ret = false;
	bool found = false;
	string Local_Path = Get_Root_Path(Path);

	// Iterate through all partitions
	for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		if ((*iter)->Mount_Point == Local_Path) {
			ret = (*iter)->Mount(Display_Error);
			found = true;
		} else if ((*iter)->Is_SubPartition && (*iter)->SubPartition_Of == Local_Path) {
			(*iter)->Mount(Display_Error);
		}
	}
	if (found) {
		return ret;
	} else if (Display_Error) {
		LOGE("Mount: Unable to find partition for path '%s'\n", Local_Path.c_str());
	} else {
		LOGI("Mount: Unable to find partition for path '%s'\n", Local_Path.c_str());
	}
	return false;
}

int TWPartitionManager::Mount_By_Block(string Block, bool Display_Error) {
	TWPartition* Part = Find_Partition_By_Block(Block);

	if (Part) {
		if (Part->Has_SubPartition) {
			std::vector<TWPartition*>::iterator subpart;

			for (subpart = Partitions.begin(); subpart != Partitions.end(); subpart++) {
				if ((*subpart)->Is_SubPartition && (*subpart)->SubPartition_Of == Part->Mount_Point)
					(*subpart)->Mount(Display_Error);
			}
			return Part->Mount(Display_Error);
		} else
			return Part->Mount(Display_Error);
	}
	if (Display_Error)
		LOGE("Mount: Unable to find partition for block '%s'\n", Block.c_str());
	else
		LOGI("Mount: Unable to find partition for block '%s'\n", Block.c_str());
	return false;
}

int TWPartitionManager::Mount_By_Name(string Name, bool Display_Error) {
	TWPartition* Part = Find_Partition_By_Name(Name);

	if (Part) {
		if (Part->Has_SubPartition) {
			std::vector<TWPartition*>::iterator subpart;

			for (subpart = Partitions.begin(); subpart != Partitions.end(); subpart++) {
				if ((*subpart)->Is_SubPartition && (*subpart)->SubPartition_Of == Part->Mount_Point)
					(*subpart)->Mount(Display_Error);
			}
			return Part->Mount(Display_Error);
		} else
			return Part->Mount(Display_Error);
	}
	if (Display_Error)
		LOGE("Mount: Unable to find partition for name '%s'\n", Name.c_str());
	else
		LOGI("Mount: Unable to find partition for name '%s'\n", Name.c_str());
	return false;
}

int TWPartitionManager::UnMount_By_Path(string Path, bool Display_Error) {
	std::vector<TWPartition*>::iterator iter;
	int ret = false;
	bool found = false;
	string Local_Path = Get_Root_Path(Path);

	// Iterate through all partitions
	for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		if ((*iter)->Mount_Point == Local_Path) {
			ret = (*iter)->UnMount(Display_Error);
			found = true;
		} else if ((*iter)->Is_SubPartition && (*iter)->SubPartition_Of == Local_Path) {
			(*iter)->UnMount(Display_Error);
		}
	}
	if (found) {
		return ret;
	} else if (Display_Error) {
		LOGE("UnMount: Unable to find partition for path '%s'\n", Local_Path.c_str());
	} else {
		LOGI("UnMount: Unable to find partition for path '%s'\n", Local_Path.c_str());
	}
	return false;
}

int TWPartitionManager::UnMount_By_Block(string Block, bool Display_Error) {
	TWPartition* Part = Find_Partition_By_Block(Block);

	if (Part) {
		if (Part->Has_SubPartition) {
			std::vector<TWPartition*>::iterator subpart;

			for (subpart = Partitions.begin(); subpart != Partitions.end(); subpart++) {
				if ((*subpart)->Is_SubPartition && (*subpart)->SubPartition_Of == Part->Mount_Point)
					(*subpart)->UnMount(Display_Error);
			}
			return Part->UnMount(Display_Error);
		} else
			return Part->UnMount(Display_Error);
	}
	if (Display_Error)
		LOGE("UnMount: Unable to find partition for block '%s'\n", Block.c_str());
	else
		LOGI("UnMount: Unable to find partition for block '%s'\n", Block.c_str());
	return false;
}

int TWPartitionManager::UnMount_By_Name(string Name, bool Display_Error) {
	TWPartition* Part = Find_Partition_By_Name(Name);

	if (Part) {
		if (Part->Has_SubPartition) {
			std::vector<TWPartition*>::iterator subpart;

			for (subpart = Partitions.begin(); subpart != Partitions.end(); subpart++) {
				if ((*subpart)->Is_SubPartition && (*subpart)->SubPartition_Of == Part->Mount_Point)
					(*subpart)->UnMount(Display_Error);
			}
			return Part->UnMount(Display_Error);
		} else
			return Part->UnMount(Display_Error);
	}
	if (Display_Error)
		LOGE("UnMount: Unable to find partition for name '%s'\n", Name.c_str());
	else
		LOGI("UnMount: Unable to find partition for name '%s'\n", Name.c_str());
	return false;
}

int TWPartitionManager::Is_Mounted_By_Path(string Path) {
	TWPartition* Part = Find_Partition_By_Path(Path);

	if (Part)
		return Part->Is_Mounted();
	else
		LOGI("Is_Mounted: Unable to find partition for path '%s'\n", Path.c_str());
	return false;
}

int TWPartitionManager::Is_Mounted_By_Block(string Block) {
	TWPartition* Part = Find_Partition_By_Block(Block);

	if (Part)
		return Part->Is_Mounted();
	else
		LOGI("Is_Mounted: Unable to find partition for block '%s'\n", Block.c_str());
	return false;
}

int TWPartitionManager::Is_Mounted_By_Name(string Name) {
	TWPartition* Part = Find_Partition_By_Name(Name);

	if (Part)
		return Part->Is_Mounted();
	else
		LOGI("Is_Mounted: Unable to find partition for name '%s'\n", Name.c_str());
	return false;
}

int TWPartitionManager::Mount_Current_Storage(bool Display_Error) {
	string current_storage_path = DataManager::GetCurrentStoragePath();

	if (Mount_By_Path(current_storage_path, Display_Error)) {
		TWPartition* FreeStorage = Find_Partition_By_Path(current_storage_path);
		if (FreeStorage)
			DataManager::SetValue(TW_STORAGE_FREE_SIZE, (int)(FreeStorage->Free / 1048576LLU));
		return true;
	}
	return false;
}

int TWPartitionManager::Mount_Settings_Storage(bool Display_Error) {
	return Mount_By_Path(DataManager::GetSettingsStoragePath(), Display_Error);
}

TWPartition* TWPartitionManager::Find_Partition_By_Path(string Path) {
	std::vector<TWPartition*>::iterator iter;
	string Local_Path = Get_Root_Path(Path);

	for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		if ((*iter)->Mount_Point == Local_Path)
			return (*iter);
	}
	return NULL;
}

TWPartition* TWPartitionManager::Find_Partition_By_Block(string Block) {
	std::vector<TWPartition*>::iterator iter;

	for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		if ((*iter)->Block_Device == Block || (*iter)->Alternate_Block_Device == Block || ((*iter)->Is_Decrypted && (*iter)->Decrypted_Block_Device == Block))
			return (*iter);
	}
	return NULL;
}

TWPartition* TWPartitionManager::Find_Partition_By_Name(string Name) {
	std::vector<TWPartition*>::iterator iter;

	for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		if ((*iter)->Display_Name == Name)
			return (*iter);
	}
	return NULL;
}

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
	Update_System_Details();
	return;
}

void TWPartitionManager::Update_System_Details(void) {
	std::vector<TWPartition*>::iterator iter;
	int data_size = 0;

	LOGI("Updating system details...\n");
	for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		if ((*iter)->Can_Be_Mounted) {
			(*iter)->Update_Size(true);
			if ((*iter)->Mount_Point == "/system") {
				int backup_display_size = (int)((*iter)->Backup_Size / 1048576LLU);
				DataManager::SetValue(TW_BACKUP_SYSTEM_SIZE, backup_display_size);
			} else if ((*iter)->Mount_Point == "/data" || (*iter)->Mount_Point == "/datadata") {
				data_size += (int)((*iter)->Backup_Size / 1048576LLU);
			} else if ((*iter)->Mount_Point == "/cache") {
				int backup_display_size = (int)((*iter)->Backup_Size / 1048576LLU);
				DataManager::SetValue(TW_BACKUP_CACHE_SIZE, backup_display_size);
			} else if ((*iter)->Mount_Point == "/sd-ext") {
				int backup_display_size = (int)((*iter)->Backup_Size / 1048576LLU);
				DataManager::SetValue(TW_BACKUP_SDEXT_SIZE, backup_display_size);
				if ((*iter)->Backup_Size == 0) {
					DataManager::SetValue(TW_HAS_SDEXT_PARTITION, 0);
					DataManager::SetValue(TW_BACKUP_SDEXT_VAR, 0);
				} else
					DataManager::SetValue(TW_HAS_SDEXT_PARTITION, 1);
			}
		}
	}
	DataManager::SetValue(TW_BACKUP_DATA_SIZE, data_size);
	string current_storage_path = DataManager::GetCurrentStoragePath();
	TWPartition* FreeStorage = Find_Partition_By_Path(current_storage_path);
	if (FreeStorage)
		DataManager::SetValue(TW_STORAGE_FREE_SIZE, (int)(FreeStorage->Free / 1048576LLU));
	else
		LOGI("Unable to find storage partition '%s'.\n", current_storage_path.c_str());
	if (!Write_Fstab())
		LOGE("Error creating fstab\n");
	return;
}

int TWPartitionManager::Decrypt_Device(string Password) {
#ifdef TW_INCLUDE_CRYPTO
	int ret_val, password_len;
	char crypto_blkdev[255], cPassword[255];
	size_t result;

	property_set("ro.crypto.state", "encrypted");
#ifdef TW_INCLUDE_JB_CRYPTO
	// No extra flags needed
#else
	property_set("ro.crypto.fs_type", CRYPTO_FS_TYPE);
	property_set("ro.crypto.fs_real_blkdev", CRYPTO_REAL_BLKDEV);
	property_set("ro.crypto.fs_mnt_point", CRYPTO_MNT_POINT);
	property_set("ro.crypto.fs_options", CRYPTO_FS_OPTIONS);
	property_set("ro.crypto.fs_flags", CRYPTO_FS_FLAGS);
	property_set("ro.crypto.keyfile.userdata", CRYPTO_KEY_LOC);
#endif
	strcpy(cPassword, Password.c_str());
	if (cryptfs_check_passwd(cPassword) != 0) {
		LOGE("Failed to decrypt data.\n");
		return -1;
	}
	property_get("ro.crypto.fs_crypto_blkdev", crypto_blkdev, "error");
	if (strcmp(crypto_blkdev, "error") == 0) {
		LOGE("Error retrieving decrypted data block device.\n");
	} else {
		TWPartition* dat = Find_Partition_By_Path("/data");
		if (dat != NULL) {
			DataManager::SetValue(TW_DATA_BLK_DEVICE, dat->Block_Device);
			DataManager::SetValue(TW_IS_DECRYPTED, 1);
			dat->Is_Decrypted = true;
			dat->Decrypted_Block_Device = crypto_blkdev;
			LOGI("Data successfully decrypted, new block device: '%s'\n", crypto_blkdev);
			// Sleep for a bit so that the device will be ready
			sleep(1);
			Update_System_Details();
		} else
			LOGE("Unable to locate data partition.\n");
	}
	return 0;
#else
	LOGE("No crypto support was compiled into this build.\n");
	return -1;
#endif
	return 1;
}

string TWPartitionManager::Get_Root_Path(string Path) {
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
