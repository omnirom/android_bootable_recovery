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
#include <dirent.h>
#include <time.h>

#include "variables.h"
#include "common.h"
#include "partitions.hpp"
#include "data.hpp"
#include "twrp-functions.hpp"

extern "C" {
	#include "extra-functions.h"
	int __system(const char *command);
}

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
			Line = (*iter)->Actual_Block_Device + " " + (*iter)->Mount_Point + " " + (*iter)->Current_File_System + " rw\n";
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
	string Local_Path = TWFunc::Get_Root_Path(Path);

	if (Local_Path == "/tmp")
		return true;

	// Iterate through all partitions
	for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		if ((*iter)->Mount_Point == Local_Path || (!(*iter)->Symlink_Mount_Point.empty() && (*iter)->Symlink_Mount_Point == Local_Path)) {
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
	string Local_Path = TWFunc::Get_Root_Path(Path);

	// Iterate through all partitions
	for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		if ((*iter)->Mount_Point == Local_Path || (!(*iter)->Symlink_Mount_Point.empty() && (*iter)->Symlink_Mount_Point == Local_Path)) {
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
	string Local_Path = TWFunc::Get_Root_Path(Path);

	for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		if ((*iter)->Mount_Point == Local_Path || (!(*iter)->Symlink_Mount_Point.empty() && (*iter)->Symlink_Mount_Point == Local_Path))
			return (*iter);
	}
	return NULL;
}

TWPartition* TWPartitionManager::Find_Partition_By_Block(string Block) {
	std::vector<TWPartition*>::iterator iter;

	for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		if ((*iter)->Primary_Block_Device == Block || (*iter)->Alternate_Block_Device == Block || ((*iter)->Is_Decrypted && (*iter)->Decrypted_Block_Device == Block))
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

bool TWPartitionManager::Make_MD5(bool generate_md5, string Backup_Folder, string Backup_Filename)
{
	char command[512];
	string Full_File = Backup_Folder + Backup_Filename;

	if (!generate_md5) {
		LOGI("MD5 disabled\n");
		return true;
	}

	ui_print(" * Generating md5...\n");

	if (TWFunc::Path_Exists(Full_File)) {
		sprintf(command, "cd '%s' && md5sum %s > %s.md5",Backup_Folder.c_str(), Backup_Filename.c_str(), Backup_Filename.c_str());
		LOGI("MD5 command is: '%s'\n", command);
		if (system(command) == 0) {
			ui_print("....MD5 Created.\n");
			return true;
		} else {
			ui_print("....MD5 Error.\n");
			return false;
		}
	} else {
		char filename[512];
		int index = 0;

		sprintf(filename, "%s%03i", Full_File.c_str(), index);
		while (TWFunc::Path_Exists(filename)) {
			sprintf(command, "cd '%s' && md5sum %s%03i > %s%03i.md5",Backup_Folder.c_str(), Backup_Filename.c_str(), index, Backup_Filename.c_str(), index);
			LOGI("MD5 command is: '%s'\n", command);
			if (system(command) == 0) {
				ui_print("....MD5 Created.\n");
			} else {
				ui_print("....MD5 Error.\n");
				return false;
			}
			index++;
		}
		if (index == 0) {
			LOGE("Backup file: '%s' not found!\n", filename);
			return false;
		}
	}
	return true;
}

bool TWPartitionManager::Backup_Partition(TWPartition* Part, string Backup_Folder, bool generate_md5, unsigned long long* img_bytes_remaining, unsigned long long* file_bytes_remaining, unsigned long *img_time, unsigned long *file_time) {
	time_t start, stop;

	if (Part == NULL)
		return true;

	time(&start);

	if (Part->Backup(Backup_Folder)) {
		time(&stop);
		if (Part->Backup_Method == 1) {
			*file_bytes_remaining -= Part->Backup_Size;
			*file_time += (int) difftime(stop, start);
		} else {
			*img_bytes_remaining -= Part->Backup_Size;
			*img_time += (int) difftime(stop, start);
		}
		return Make_MD5(generate_md5, Backup_Folder, Part->Backup_FileName);
	} else {
		return false;
	}
}

int TWPartitionManager::Run_Backup(void) {
	int check, do_md5, partition_count = 0;
	string Backup_Folder, Backup_Name, Full_Backup_Path;
	unsigned long long total_bytes = 0, file_bytes = 0, img_bytes = 0, free_space = 0, img_bytes_remaining, file_bytes_remaining;
	unsigned long img_time = 0, file_time = 0;
	TWPartition* backup_sys = NULL;
	TWPartition* backup_data = NULL;
	TWPartition* backup_cache = NULL;
	TWPartition* backup_recovery = NULL;
	TWPartition* backup_boot = NULL;
	TWPartition* backup_andsec = NULL;
	TWPartition* backup_sdext = NULL;
	TWPartition* backup_sp1 = NULL;
	TWPartition* backup_sp2 = NULL;
	TWPartition* backup_sp3 = NULL;
	TWPartition* storage = NULL;
	struct tm *t;
	time_t start, stop, seconds, total_start, total_stop;
	seconds = time(0);
    t = localtime(&seconds);

	time(&total_start);

	Update_System_Details();

	if (!Mount_Current_Storage(true))
		return false;

	DataManager::GetValue(TW_SKIP_MD5_GENERATE_VAR, do_md5);
	if (do_md5 != 0) {
		LOGI("MD5 creation enabled.\n");
		do_md5 = true;
	}
	DataManager::GetValue(TW_BACKUPS_FOLDER_VAR, Backup_Folder);
	DataManager::GetValue(TW_BACKUP_NAME, Backup_Name);
	if (Backup_Name == "(Current Date)" || Backup_Name == "0") {
		char timestamp[255];
		sprintf(timestamp,"%04d-%02d-%02d--%02d-%02d-%02d",t->tm_year+1900,t->tm_mon+1,t->tm_mday,t->tm_hour,t->tm_min,t->tm_sec);
		Backup_Name = timestamp;
	}
	LOGI("Backup Name is: '%s'\n", Backup_Name.c_str());
	Full_Backup_Path = Backup_Folder + "/" + Backup_Name + "/";
	LOGI("Full_Backup_Path is: '%s'\n", Full_Backup_Path.c_str());

	ui_print("\n[BACKUP STARTED]\n");
    ui_print(" * Backup Folder: %s\n", Full_Backup_Path.c_str());
	if (!TWFunc::Recursive_Mkdir(Full_Backup_Path)) {
		LOGE("Failed to make backup folder.\n");
		return false;
	}

	LOGI("Calculating backup details...\n");
	DataManager::GetValue(TW_BACKUP_SYSTEM_VAR, check);
	if (check) {
		backup_sys = Find_Partition_By_Path("/system");
		if (backup_sys != NULL) {
			partition_count++;
			if (backup_sys->Backup_Method == 1)
				file_bytes += backup_sys->Backup_Size;
			else
				img_bytes += backup_sys->Backup_Size;
		} else {
			LOGE("Unable to locate system partition.\n");
			return false;
		}
	}
	DataManager::GetValue(TW_BACKUP_DATA_VAR, check);
	if (check) {
		backup_data = Find_Partition_By_Path("/data");
		if (backup_data != NULL) {
			partition_count++;
			if (backup_data->Backup_Method == 1)
				file_bytes += backup_data->Backup_Size;
			else
				img_bytes += backup_data->Backup_Size;
		} else {
			LOGE("Unable to locate data partition.\n");
			return false;
		}
	}
	DataManager::GetValue(TW_BACKUP_CACHE_VAR, check);
	if (check) {
		backup_cache = Find_Partition_By_Path("/cache");
		if (backup_cache != NULL) {
			partition_count++;
			if (backup_cache->Backup_Method == 1)
				file_bytes += backup_cache->Backup_Size;
			else
				img_bytes += backup_cache->Backup_Size;
		} else {
			LOGE("Unable to locate cache partition.\n");
			return false;
		}
	}
	DataManager::GetValue(TW_BACKUP_RECOVERY_VAR, check);
	if (check) {
		backup_recovery = Find_Partition_By_Path("/recovery");
		if (backup_recovery != NULL) {
			partition_count++;
			if (backup_recovery->Backup_Method == 1)
				file_bytes += backup_recovery->Backup_Size;
			else
				img_bytes += backup_recovery->Backup_Size;
		} else {
			LOGE("Unable to locate recovery partition.\n");
			return false;
		}
	}
	DataManager::GetValue(TW_BACKUP_BOOT_VAR, check);
	if (check) {
		backup_boot = Find_Partition_By_Path("/boot");
		if (backup_boot != NULL) {
			partition_count++;
			if (backup_boot->Backup_Method == 1)
				file_bytes += backup_boot->Backup_Size;
			else
				img_bytes += backup_boot->Backup_Size;
		} else {
			LOGE("Unable to locate boot partition.\n");
			return false;
		}
	}
	DataManager::GetValue(TW_BACKUP_ANDSEC_VAR, check);
	if (check) {
		backup_andsec = Find_Partition_By_Path("/and-sec");
		if (backup_andsec != NULL) {
			partition_count++;
			if (backup_andsec->Backup_Method == 1)
				file_bytes += backup_andsec->Backup_Size;
			else
				img_bytes += backup_andsec->Backup_Size;
		} else {
			LOGE("Unable to locate android secure partition.\n");
			return false;
		}
	}
	DataManager::GetValue(TW_BACKUP_SDEXT_VAR, check);
	if (check) {
		backup_sdext = Find_Partition_By_Path("/sd-ext");
		if (backup_sdext != NULL) {
			partition_count++;
			if (backup_sdext->Backup_Method == 1)
				file_bytes += backup_sdext->Backup_Size;
			else
				img_bytes += backup_sdext->Backup_Size;
		} else {
			LOGE("Unable to locate sd-ext partition.\n");
			return false;
		}
	}
#ifdef SP1_NAME
	DataManager::GetValue(TW_BACKUP_SP1_VAR, check);
	if (check) {
		backup_sp1 = Find_Partition_By_Path(SP1_NAME);
		if (backup_sp1 != NULL) {
			partition_count++;
			if (backup_sp1->Backup_Method == 1)
				file_bytes += backup_sp1->Backup_Size;
			else
				img_bytes += backup_sp1->Backup_Size;
		} else {
			LOGE("Unable to locate %s partition.\n", SP1_NAME);
			return false;
		}
	}
#endif
#ifdef SP2_NAME
	DataManager::GetValue(TW_BACKUP_SP2_VAR, check);
	if (check) {
		backup_sp2 = Find_Partition_By_Path(SP2_NAME);
		if (backup_sp2 != NULL) {
			partition_count++;
			if (backup_sp2->Backup_Method == 1)
				file_bytes += backup_sp2->Backup_Size;
			else
				img_bytes += backup_sp2->Backup_Size;
		} else {
			LOGE("Unable to locate %s partition.\n", SP2_NAME);
			return false;
		}
	}
#endif
#ifdef SP3_NAME
	DataManager::GetValue(TW_BACKUP_SP3_VAR, check);
	if (check) {
		backup_sp3 = Find_Partition_By_Path(SP3_NAME);
		if (backup_sp3 != NULL) {
			partition_count++;
			if (backup_sp3->Backup_Method == 1)
				file_bytes += backup_sp3->Backup_Size;
			else
				img_bytes += backup_sp3->Backup_Size;
		} else {
			LOGE("Unable to locate %s partition.\n", SP3_NAME);
			return false;
		}
	}
#endif

	if (partition_count == 0) {
		ui_print("No partitions selected for backup.\n");
		return false;
	}
	total_bytes = file_bytes + img_bytes;
	ui_print(" * Total number of partitions to back up: %d\n", partition_count);
    ui_print(" * Total size of all data: %lluMB\n", total_bytes / 1024 / 1024);
	storage = Find_Partition_By_Path(DataManager::GetCurrentStoragePath());
	if (storage != NULL) {
		free_space = storage->Free;
		ui_print(" * Available space: %lluMB\n", free_space / 1024 / 1024);
	} else {
		LOGE("Unable to locate storage device.\n");
		return false;
	}
	if (free_space + (32 * 1024 * 1024) < total_bytes) {
		// We require an extra 32MB just in case
		LOGE("Not enough free space on storage.\n");
		return false;
	}
	img_bytes_remaining = img_bytes;
    file_bytes_remaining = file_bytes;

	if (!Backup_Partition(backup_sys, Full_Backup_Path, do_md5, &img_bytes_remaining, &file_bytes_remaining, &img_time, &file_time))
		return false;
	if (!Backup_Partition(backup_data, Full_Backup_Path, do_md5, &img_bytes_remaining, &file_bytes_remaining, &img_time, &file_time))
		return false;
	if (!Backup_Partition(backup_cache, Full_Backup_Path, do_md5, &img_bytes_remaining, &file_bytes_remaining, &img_time, &file_time))
		return false;
	if (!Backup_Partition(backup_recovery, Full_Backup_Path, do_md5, &img_bytes_remaining, &file_bytes_remaining, &img_time, &file_time))
		return false;
	if (!Backup_Partition(backup_boot, Full_Backup_Path, do_md5, &img_bytes_remaining, &file_bytes_remaining, &img_time, &file_time))
		return false;
	if (!Backup_Partition(backup_andsec, Full_Backup_Path, do_md5, &img_bytes_remaining, &file_bytes_remaining, &img_time, &file_time))
		return false;
	if (!Backup_Partition(backup_sdext, Full_Backup_Path, do_md5, &img_bytes_remaining, &file_bytes_remaining, &img_time, &file_time))
		return false;
	if (!Backup_Partition(backup_sp1, Full_Backup_Path, do_md5, &img_bytes_remaining, &file_bytes_remaining, &img_time, &file_time))
		return false;
	if (!Backup_Partition(backup_sp2, Full_Backup_Path, do_md5, &img_bytes_remaining, &file_bytes_remaining, &img_time, &file_time))
		return false;
	if (!Backup_Partition(backup_sp3, Full_Backup_Path, do_md5, &img_bytes_remaining, &file_bytes_remaining, &img_time, &file_time))
		return false;

	// Average BPS
	if (img_time == 0)
		img_time = 1;
	if (file_time == 0)
		file_time = 1;
	unsigned long int img_bps = img_bytes / img_time;
	unsigned long int file_bps = file_bytes / file_time;

	ui_print("Average backup rate for file systems: %lu MB/sec\n", (file_bps / (1024 * 1024)));
	ui_print("Average backup rate for imaged drives: %lu MB/sec\n", (img_bps / (1024 * 1024)));

	time(&total_stop);
	int total_time = (int) difftime(total_stop, total_start);
	unsigned long long actual_backup_size = TWFunc::Get_Folder_Size(Full_Backup_Path, true);
    actual_backup_size /= (1024LLU * 1024LLU);

	ui_print("[%llu MB TOTAL BACKED UP]\n", actual_backup_size);
	Update_System_Details();
	ui_print("[BACKUP COMPLETED IN %d SECONDS]\n\n", total_time); // the end
    return true;
}

int TWPartitionManager::Run_Restore(string Restore_Name) {
	int check, restore_sys, restore_data, restore_cache, restore_boot, restore_andsec, restore_sdext, restore_sp1, restore_sp2, restore_sp3;
	TWPartition* Part;

	DataManager::GetValue(TW_SKIP_MD5_CHECK_VAR, check);
	DataManager::GetValue(TW_RESTORE_SYSTEM_VAR, restore_sys);
	DataManager::GetValue(TW_RESTORE_DATA_VAR, restore_data);
	DataManager::GetValue(TW_RESTORE_CACHE_VAR, restore_cache);
	DataManager::GetValue(TW_RESTORE_BOOT_VAR, restore_boot);
	DataManager::GetValue(TW_RESTORE_ANDSEC_VAR, restore_andsec);
	DataManager::GetValue(TW_RESTORE_SDEXT_VAR, restore_sdext);
	DataManager::GetValue(TW_RESTORE_SP1_VAR, restore_sp1);
	DataManager::GetValue(TW_RESTORE_SP2_VAR, restore_sp2);
	DataManager::GetValue(TW_RESTORE_SP3_VAR, restore_sp3);

	if (check > 0) {
		// Check MD5 files first before restoring to ensure that all of them match before starting a restore
		if (restore_sys > 0) {
			Part = Find_Partition_By_Path("/system");
			if (Part) {
				if (!Part->Check_MD5(Restore_Name))
					return false;
			} else
				LOGE("Restore: Unable to locate system partition.\n");
		}

		if (restore_data > 0) {
			Part = Find_Partition_By_Path("/data");
			if (Part) {
				if (!Part->Check_MD5(Restore_Name))
					return false;
			} else
				LOGE("Restore: Unable to locate data partition.\n");
		}

		if (restore_cache > 0) {
			Part = Find_Partition_By_Path("/cache");
			if (Part) {
				if (!Part->Check_MD5(Restore_Name))
					return false;
			} else
				LOGE("Restore: Unable to locate cache partition.\n");
		}

		if (restore_boot > 0) {
			Part = Find_Partition_By_Path("/boot");
			if (Part) {
				if (!Part->Check_MD5(Restore_Name))
					return false;
			} else
				LOGE("Restore: Unable to locate boot partition.\n");
		}

		if (restore_andsec > 0) {
			Part = Find_Partition_By_Path("/.android_secure");
			if (Part) {
				if (!Part->Check_MD5(Restore_Name))
					return false;
			} else
				LOGE("Restore: Unable to locate android_secure partition.\n");
		}

		if (restore_sdext > 0) {
			Part = Find_Partition_By_Path("/sd-ext");
			if (Part) {
				if (!Part->Check_MD5(Restore_Name))
					return false;
			} else
				LOGE("Restore: Unable to locate sd-ext partition.\n");
		}
#ifdef SP1_NAME
		if (restore_sp1 > 0) {
			Part = Find_Partition_By_Path(TWFunc::Get_Root_Path(SP1_NAME));
			if (Part) {
				if (!Part->Check_MD5(Restore_Name))
					return false;
			} else
				LOGE("Restore: Unable to locate %s partition.\n", SP1_NAME);
		}
#endif
#ifdef SP2_NAME
		if (restore_sp2 > 0) {
			Part = Find_Partition_By_Path(TWFunc::Get_Root_Path(SP2_NAME));
			if (Part) {
				if (!Part->Check_MD5(Restore_Name))
					return false;
			} else
				LOGE("Restore: Unable to locate %s partition.\n", SP2_NAME);
		}
#endif
#ifdef SP3_NAME
		if (restore_sp3 > 0) {
			Part = Find_Partition_By_Path(TWFunc::Get_Root_Path(SP3_NAME));
			if (Part) {
				if (!Part->Check_MD5(Restore_Name))
					return false;
			} else
				LOGE("Restore: Unable to locate %s partition.\n", SP3_NAME);
		}
#endif
	}

	if (restore_sys > 0) {
		Part = Find_Partition_By_Path("/system");
		if (Part) {
			if (!Part->Restore(Restore_Name))
				return false;
		} else
			LOGE("Restore: Unable to locate system partition.\n");
	}

	if (restore_data > 0) {
		Part = Find_Partition_By_Path("/data");
		if (Part) {
			if (!Part->Restore(Restore_Name))
				return false;
		} else
			LOGE("Restore: Unable to locate data partition.\n");
	}

	if (restore_cache > 0) {
		Part = Find_Partition_By_Path("/cache");
		if (Part) {
			if (!Part->Restore(Restore_Name))
				return false;
		} else
			LOGE("Restore: Unable to locate cache partition.\n");
	}

	if (restore_boot > 0) {
		Part = Find_Partition_By_Path("/boot");
		if (Part) {
			if (!Part->Restore(Restore_Name))
				return false;
		} else
			LOGE("Restore: Unable to locate boot partition.\n");
	}

	if (restore_andsec > 0) {
		Part = Find_Partition_By_Path("/.android_secure");
		if (Part) {
			if (!Part->Restore(Restore_Name))
				return false;
		} else
			LOGE("Restore: Unable to locate android_secure partition.\n");
	}

	if (restore_sdext > 0) {
		Part = Find_Partition_By_Path("/sd-ext");
		if (Part) {
			if (!Part->Restore(Restore_Name))
				return false;
		} else
			LOGE("Restore: Unable to locate sd-ext partition.\n");
	}
#ifdef SP1_NAME
	if (restore_sp1 > 0) {
		Part = Find_Partition_By_Path(TWFunc::Get_Root_Path(SP1_NAME));
		if (Part) {
			if (!Part->Restore(Restore_Name))
				return false;
		} else
			LOGE("Restore: Unable to locate %s partition.\n", SP1_NAME);
	}
#endif
#ifdef SP2_NAME
	if (restore_sp2 > 0) {
		Part = Find_Partition_By_Path(TWFunc::Get_Root_Path(SP2_NAME));
		if (Part) {
			if (!Part->Restore(Restore_Name))
				return false;
		} else
			LOGE("Restore: Unable to locate %s partition.\n", SP2_NAME);
	}
#endif
#ifdef SP3_NAME
	if (restore_sp3 > 0) {
		Part = Find_Partition_By_Path(TWFunc::Get_Root_Path(SP3_NAME));
		if (Part) {
			if (!Part->Restore(Restore_Name))
				return false;
		} else
			LOGE("Restore: Unable to locate %s partition.\n", SP3_NAME);
	}
#endif
	Update_System_Details();
	return true;
}

void TWPartitionManager::Set_Restore_Files(string Restore_Name) {
	// Start with the default values
	int tw_restore_system = -1;
	int tw_restore_data = -1;
	int tw_restore_cache = -1;
	int tw_restore_recovery = -1;
	int tw_restore_boot = -1;
	int tw_restore_andsec = -1;
	int tw_restore_sdext = -1;
	int tw_restore_sp1 = -1;
	int tw_restore_sp2 = -1;
	int tw_restore_sp3 = -1;
	bool get_date = true;

	DIR* d;
	d = opendir(Restore_Name.c_str());
	if (d == NULL)
	{
		LOGE("Error opening %s\n", Restore_Name.c_str());
		return;
	}

	struct dirent* de;
	while ((de = readdir(d)) != NULL)
	{
		// Strip off three components
		char str[256];
		char* label;
		char* fstype = NULL;
		char* extn = NULL;
		char* ptr;

		strcpy(str, de->d_name);
		if (strlen(str) <= 2)
			continue;

		if (get_date) {
			char file_path[255];
			struct stat st;

			strcpy(file_path, Restore_Name.c_str());
			strcat(file_path, "/");
			strcat(file_path, str);
			stat(file_path, &st);
			string backup_date = ctime((const time_t*)(&st.st_mtime));
			DataManager::SetValue(TW_RESTORE_FILE_DATE, backup_date);
			get_date = false;
		}

		label = str;
		ptr = label;
		while (*ptr && *ptr != '.')	 ptr++;
		if (*ptr == '.')
		{
			*ptr = 0x00;
			ptr++;
			fstype = ptr;
		}
		while (*ptr && *ptr != '.')	 ptr++;
		if (*ptr == '.')
		{
			*ptr = 0x00;
			ptr++;
			extn = ptr;
		}

		if (extn == NULL || (strlen(extn) >= 3 && strncmp(extn, "win", 3) != 0))   continue;

		TWPartition* Part = Find_Partition_By_Path(label);
		if (Part == NULL)
		{
			LOGE(" Unable to locate partition by backup name: '%s'\n", label);
			continue;
		}

		Part->Backup_FileName = de->d_name;
		if (strlen(extn) > 3) {
			Part->Backup_FileName.resize(Part->Backup_FileName.size() - strlen(extn) + 3);
		}

		// Now, we just need to find the correct label
		if (Part->Mount_Point == "/system")
			tw_restore_system = 1;
		if (Part->Mount_Point == "/data")
			tw_restore_data = 1;
		if (Part->Mount_Point == "/cache")
			tw_restore_cache = 1;
		if (Part->Mount_Point == "/recovery")
			tw_restore_recovery = 1;
		if (Part->Mount_Point == "/boot")
			tw_restore_boot = 1;
		if (Part->Mount_Point == "/.android_secure")
			tw_restore_andsec = 1;
		if (Part->Mount_Point == "/sd-ext")
			tw_restore_sdext = 1;
#ifdef SP1_NAME
		if (Part->Mount_Point == TWFunc::Get_Root_Path(SP1_Name))
			tw_restore_sp1 = 1;
#endif
#ifdef SP2_NAME
		if (Part->Mount_Point == TWFunc::Get_Root_Path(SP2_Name))
			tw_restore_sp2 = 1;
#endif
#ifdef SP3_NAME
		if (Part->Mount_Point == TWFunc::Get_Root_Path(SP3_Name))
			tw_restore_sp3 = 1;
#endif
	}
	closedir(d);

	// Set the final values
	DataManager::SetValue(TW_RESTORE_SYSTEM_VAR, tw_restore_system);
	DataManager::SetValue(TW_RESTORE_DATA_VAR, tw_restore_data);
	DataManager::SetValue(TW_RESTORE_CACHE_VAR, tw_restore_cache);
	DataManager::SetValue(TW_RESTORE_RECOVERY_VAR, tw_restore_recovery);
	DataManager::SetValue(TW_RESTORE_BOOT_VAR, tw_restore_boot);
	DataManager::SetValue(TW_RESTORE_ANDSEC_VAR, tw_restore_andsec);
	DataManager::SetValue(TW_RESTORE_SDEXT_VAR, tw_restore_sdext);
	DataManager::SetValue(TW_RESTORE_SP1_VAR, tw_restore_sp1);
	DataManager::SetValue(TW_RESTORE_SP2_VAR, tw_restore_sp2);
	DataManager::SetValue(TW_RESTORE_SP3_VAR, tw_restore_sp3);

	return;
}

int TWPartitionManager::Wipe_By_Path(string Path) {
	std::vector<TWPartition*>::iterator iter;
	int ret = false;
	bool found = false;
	string Local_Path = TWFunc::Get_Root_Path(Path);

	// Iterate through all partitions
	for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		if ((*iter)->Mount_Point == Local_Path || (!(*iter)->Symlink_Mount_Point.empty() && (*iter)->Symlink_Mount_Point == Local_Path)) {
			ret = (*iter)->Wipe();
			found = true;
		} else if ((*iter)->Is_SubPartition && (*iter)->SubPartition_Of == Local_Path) {
			(*iter)->Wipe();
		}
	}
	if (found) {
		return ret;
	} else
		LOGE("Wipe: Unable to find partition for path '%s'\n", Local_Path.c_str());
	return false;
}

int TWPartitionManager::Wipe_By_Block(string Block) {
	TWPartition* Part = Find_Partition_By_Block(Block);

	if (Part) {
		if (Part->Has_SubPartition) {
			std::vector<TWPartition*>::iterator subpart;

			for (subpart = Partitions.begin(); subpart != Partitions.end(); subpart++) {
				if ((*subpart)->Is_SubPartition && (*subpart)->SubPartition_Of == Part->Mount_Point)
					(*subpart)->Wipe();
			}
			return Part->Wipe();
		} else
			return Part->Wipe();
	}
	LOGE("Wipe: Unable to find partition for block '%s'\n", Block.c_str());
	return false;
}

int TWPartitionManager::Wipe_By_Name(string Name) {
	TWPartition* Part = Find_Partition_By_Name(Name);

	if (Part) {
		if (Part->Has_SubPartition) {
			std::vector<TWPartition*>::iterator subpart;

			for (subpart = Partitions.begin(); subpart != Partitions.end(); subpart++) {
				if ((*subpart)->Is_SubPartition && (*subpart)->SubPartition_Of == Part->Mount_Point)
					(*subpart)->Wipe();
			}
			return Part->Wipe();
		} else
			return Part->Wipe();
	}
	LOGE("Wipe: Unable to find partition for name '%s'\n", Name.c_str());
	return false;
}

int TWPartitionManager::Factory_Reset(void) {
	std::vector<TWPartition*>::iterator iter;
	int ret = true;

	for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		if ((*iter)->Wipe_During_Factory_Reset && (*iter)->Is_Present) {
			if (!(*iter)->Wipe())
				ret = false;
		}
	}
	return ret;
}

int TWPartitionManager::Wipe_Dalvik_Cache(void) {
	struct stat st;

	if (!Mount_By_Path("/data", true))
		return false;

	if (!Mount_By_Path("/cache", true))
		return false;

	ui_print("\nWiping Dalvik Cache Directories...\n");
	__system("rm -rf /data/dalvik-cache");
	ui_print("Cleaned: /data/dalvik-cache...\n");
	__system("rm -rf /cache/dalvik-cache");
	ui_print("Cleaned: /cache/dalvik-cache...\n");
	__system("rm -rf /cache/dc");
	ui_print("Cleaned: /cache/dc\n");

	TWPartition* sdext = Find_Partition_By_Path("/sd-ext");
	if (sdext != NULL) {
		if (sdext->Is_Present && sdext->Mount(false)) {
			if (stat("/sd-ext/dalvik-cache", &st) == 0) {
                __system("rm -rf /sd-ext/dalvik-cache");
        	    ui_print("Cleaned: /sd-ext/dalvik-cache...\n");
    	    }
        }
	}
	ui_print("-- Dalvik Cache Directories Wipe Complete!\n\n");
	return true;
}

int TWPartitionManager::Wipe_Rotate_Data(void) {
	if (!Mount_By_Path("/data", true))
		return false;

	__system("rm -r /data/misc/akmd*");
	__system("rm -r /data/misc/rild*");
	ui_print("Rotation data wiped.\n");
	return true;
}

int TWPartitionManager::Wipe_Battery_Stats(void) {
	struct stat st;

	if (!Mount_By_Path("/data", true))
		return false;

	if (0 != stat("/data/system/batterystats.bin", &st)) {
		ui_print("No Battery Stats Found. No Need To Wipe.\n");
	} else {
		remove("/data/system/batterystats.bin");
		ui_print("Cleared battery stats.\n");
	}
	return true;
}

int TWPartitionManager::Format_Data(void) {
	TWPartition* dat = Find_Partition_By_Path("/data");

	if (dat != NULL) {
		if (!dat->UnMount(true))
			return false;

		return dat->Wipe_Encryption();
	} else {
		LOGE("Unable to locate /data.\n");
		return false;
	}
	return false;
}

int TWPartitionManager::Wipe_Media_From_Data(void) {
	TWPartition* dat = Find_Partition_By_Path("/data");

	if (dat != NULL) {
		if (!dat->Has_Data_Media) {
			LOGE("This device does not have /data/media\n");
			return false;
		}
		if (!dat->Mount(true))
			return false;

		ui_print("Wiping internal storage -- /data/media...\n");
		__system("rm -rf /data/media");
		__system("cd /data && mkdir media && chmod 775 media");
		if (dat->Has_Data_Media) {
			dat->Recreate_Media_Folder();
		}
		return true;
	} else {
		LOGE("Unable to locate /data.\n");
		return false;
	}
	return false;
}

void TWPartitionManager::Refresh_Sizes(void) {
	Update_System_Details();
	return;
}

void TWPartitionManager::Update_System_Details(void) {
	std::vector<TWPartition*>::iterator iter;
	int data_size = 0;

	ui_print("Updating partition details...\n");
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
			DataManager::SetValue(TW_DATA_BLK_DEVICE, dat->Primary_Block_Device);
			DataManager::SetValue(TW_IS_DECRYPTED, 1);
			dat->Is_Decrypted = true;
			dat->Decrypted_Block_Device = crypto_blkdev;
			ui_print("Data successfully decrypted, new block device: '%s'\n", crypto_blkdev);
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

int TWPartitionManager::Fix_Permissions(void) {
	if (!Mount_By_Path("/data", true))
		return false;

	if (!Mount_By_Path("/system", true))
		return false;

	ui_print("Fixing Permissions\nThis may take a few minutes.\n");
	__system("./sbin/fix_permissions.sh");
	ui_print("Done.\n\n");
	return true;
}