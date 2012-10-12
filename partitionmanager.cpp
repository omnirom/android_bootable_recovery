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
#include <errno.h>
#include <fcntl.h>

#include "variables.h"
#include "common.h"
#include "ui.h"
#include "partitions.hpp"
#include "data.hpp"
#include "twrp-functions.hpp"
#include "fixPermissions.hpp"

#ifdef TW_INCLUDE_CRYPTO
	#ifdef TW_INCLUDE_JB_CRYPTO
		#include "crypto/jb/cryptfs.h"
	#else
		#include "crypto/ics/cryptfs.h"
	#endif
	#include "cutils/properties.h"
#endif

extern RecoveryUI* ui;

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

		if (fstab_line[strlen(fstab_line) - 1] != '\n')
			fstab_line[strlen(fstab_line)] = '\n';

		TWPartition* partition = new TWPartition();
		string line = fstab_line;
		memset(fstab_line, 0, sizeof(fstab_line));

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
	UnMount_Main_Partitions();
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

void TWPartitionManager::Output_Partition_Logging(void) {
	std::vector<TWPartition*>::iterator iter;

	printf("\n\nPartition Logs:\n");
	for (iter = Partitions.begin(); iter != Partitions.end(); iter++)
		Output_Partition((*iter));
}

void TWPartitionManager::Output_Partition(TWPartition* Part) {
	unsigned long long mb = 1048576;

	if (Part->Can_Be_Mounted) {
		printf("%s | %s | Size: %iMB Used: %iMB Free: %iMB Backup Size: %iMB\n   Flags: ", Part->Mount_Point.c_str(), Part->Actual_Block_Device.c_str(), (int)(Part->Size / mb), (int)(Part->Used / mb), (int)(Part->Free / mb), (int)(Part->Backup_Size / mb));
		if (Part->Can_Be_Wiped)
			printf("Can_Be_Wiped ");
		if (Part->Wipe_During_Factory_Reset)
			printf("Wipe_During_Factory_Reset ");
		if (Part->Wipe_Available_in_GUI)
			printf("Wipe_Available_in_GUI ");
		if (Part->Is_SubPartition)
			printf("Is_SubPartition ");
		if (Part->Has_SubPartition)
			printf("Has_SubPartition ");
		if (Part->Removable)
			printf("Removable ");
		if (Part->Is_Present)
			printf("IsPresent ");
		if (Part->Can_Be_Encrypted)
			printf("Can_Be_Encrypted ");
		if (Part->Is_Encrypted)
			printf("Is_Encrypted ");
		if (Part->Is_Decrypted)
			printf("Is_Decrypted ");
		if (Part->Has_Data_Media)
			printf("Has_Data_Media ");
		if (Part->Has_Android_Secure)
			printf("Has_Android_Secure ");
		if (Part->Is_Storage)
			printf("Is_Storage ");
		printf("\n");
		if (!Part->SubPartition_Of.empty())
			printf("   SubPartition_Of: %s\n", Part->SubPartition_Of.c_str());
		if (!Part->Symlink_Path.empty())
			printf("   Symlink_Path: %s\n", Part->Symlink_Path.c_str());
		if (!Part->Symlink_Mount_Point.empty())
			printf("   Symlink_Mount_Point: %s\n", Part->Symlink_Mount_Point.c_str());
		if (!Part->Primary_Block_Device.empty())
			printf("   Primary_Block_Device: %s\n", Part->Primary_Block_Device.c_str());
		if (!Part->Alternate_Block_Device.empty())
			printf("   Alternate_Block_Device: %s\n", Part->Alternate_Block_Device.c_str());
		if (!Part->Decrypted_Block_Device.empty())
			printf("   Decrypted_Block_Device: %s\n", Part->Decrypted_Block_Device.c_str());
		if (Part->Length != 0)
			printf("   Length: %i\n", Part->Length);
		if (!Part->Display_Name.empty())
			printf("   Display_Name: %s\n", Part->Display_Name.c_str());
		if (!Part->Backup_Path.empty())
			printf("   Backup_Path: %s\n", Part->Backup_Path.c_str());
		if (!Part->Backup_Name.empty())
			printf("   Backup_Name: %s\n", Part->Backup_Name.c_str());
		if (!Part->Backup_FileName.empty())
			printf("   Backup_FileName: %s\n", Part->Backup_FileName.c_str());
		if (!Part->Storage_Path.empty())
			printf("   Storage_Path: %s\n", Part->Storage_Path.c_str());
		if (!Part->Current_File_System.empty())
			printf("   Current_File_System: %s\n", Part->Current_File_System.c_str());
		if (!Part->Fstab_File_System.empty())
			printf("   Fstab_File_System: %s\n", Part->Fstab_File_System.c_str());
		if (Part->Format_Block_Size != 0)
			printf("   Format_Block_Size: %i\n", Part->Format_Block_Size);
	} else {
		printf("%s | %s | Size: %iMB\n", Part->Mount_Point.c_str(), Part->Actual_Block_Device.c_str(), (int)(Part->Size / mb));
	}
	if (!Part->MTD_Name.empty())
		printf("   MTD_Name: %s\n", Part->MTD_Name.c_str());
	string back_meth = Part->Backup_Method_By_Name();
	printf("   Backup_Method: %s\n\n", back_meth.c_str());
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

int TWPartitionManager::Check_Backup_Name(bool Display_Error) {
	// Check the backup name to ensure that it is the correct size and contains only valid characters
	// and that a backup with that name doesn't already exist
	char backup_name[MAX_BACKUP_NAME_LEN];
	char backup_loc[255], tw_image_dir[255];
	int copy_size;
	int index, cur_char;
	string Backup_Name, Backup_Loc;

	DataManager::GetValue(TW_BACKUP_NAME, Backup_Name);
	copy_size = Backup_Name.size();
	// Check size
	if (copy_size > MAX_BACKUP_NAME_LEN) {
		if (Display_Error)
			LOGE("Backup name is too long.\n");
		return -2;
	}

	// Check each character
	strncpy(backup_name, Backup_Name.c_str(), copy_size);
	if (strcmp(backup_name, "0") == 0)
		return 0; // A "0" (zero) means to use the current timestamp for the backup name
	for (index=0; index<copy_size; index++) {
		cur_char = (int)backup_name[index];
		if (cur_char == 32 || (cur_char >= 48  && cur_char <= 57) || (cur_char >= 65 && cur_char <= 91) || cur_char == 93 || cur_char == 95 || (cur_char >= 97 && cur_char <= 123) || cur_char == 125 || cur_char == 45 || cur_char == 46) {
			// These are valid characters
			// Numbers
			// Upper case letters
			// Lower case letters
			// Space
			// and -_.{}[]
		} else {
			if (Display_Error)
				LOGE("Backup name '%s' contains invalid character: '%c'\n", backup_name, (char)cur_char);
			return -3;
		}
	}

	// Check to make sure that a backup with this name doesn't already exist
	DataManager::GetValue(TW_BACKUPS_FOLDER_VAR, Backup_Loc);
	strcpy(backup_loc, Backup_Loc.c_str());
	sprintf(tw_image_dir,"%s/%s/.", backup_loc, backup_name);
    if (TWFunc::Path_Exists(tw_image_dir)) {
		if (Display_Error)
			LOGE("A backup with this name already exists.\n");
		return -4;
	}

	// No problems found, return 0
	return 0;
}

bool TWPartitionManager::Make_MD5(bool generate_md5, string Backup_Folder, string Backup_Filename)
{
	char command[512];
	string Full_File = Backup_Folder + Backup_Filename;

	if (!generate_md5)
		return true;

	TWFunc::GUI_Operation_Text(TW_GENERATE_MD5_TEXT, "Generating MD5");
	ui_print(" * Generating md5...\n");

	if (TWFunc::Path_Exists(Full_File)) {
		sprintf(command, "cd '%s' && md5sum %s > %s.md5",Backup_Folder.c_str(), Backup_Filename.c_str(), Backup_Filename.c_str());
		if (system(command) == 0) {
			ui_print(" * MD5 Created.\n");
			return true;
		} else {
			ui_print(" * MD5 Error!\n");
			return false;
		}
	} else {
		char filename[512];
		int index = 0;

		sprintf(filename, "%s%03i", Full_File.c_str(), index);
		while (TWFunc::Path_Exists(filename) == true) {
			sprintf(command, "cd '%s' && md5sum %s%03i > %s%03i.md5",Backup_Folder.c_str(), Backup_Filename.c_str(), index, Backup_Filename.c_str(), index);
			if (system(command) != 0) {
				ui_print(" * MD5 Error.\n");
				return false;
			}
			index++;
			sprintf(filename, "%s%03i", Full_File.c_str(), index);
		}
		if (index == 0) {
			LOGE("Backup file: '%s' not found!\n", filename);
			return false;
		}
		ui_print(" * MD5 Created.\n");
	}
	return true;
}

bool TWPartitionManager::Backup_Partition(TWPartition* Part, string Backup_Folder, bool generate_md5, unsigned long long* img_bytes_remaining, unsigned long long* file_bytes_remaining, unsigned long *img_time, unsigned long *file_time, unsigned long long *img_bytes, unsigned long long *file_bytes) {
	time_t start, stop;
	int img_bps, file_bps;
	unsigned long total_time, remain_time, section_time;
	int use_compression, backup_time;
	float pos;

	if (Part == NULL)
		return true;

	DataManager::GetValue(TW_BACKUP_AVG_IMG_RATE, img_bps);

	DataManager::GetValue(TW_USE_COMPRESSION_VAR, use_compression);
	if (use_compression)
		DataManager::GetValue(TW_BACKUP_AVG_FILE_COMP_RATE, file_bps);
	else
		DataManager::GetValue(TW_BACKUP_AVG_FILE_RATE, file_bps);

	// We know the speed for both, how far into the whole backup are we, based on time
	total_time = (*img_bytes / (unsigned long)img_bps) + (*file_bytes / (unsigned long)file_bps);
	remain_time = (*img_bytes_remaining / (unsigned long)img_bps) + (*file_bytes_remaining / (unsigned long)file_bps);

	pos = (total_time - remain_time) / (float) total_time;
	ui->SetProgress(pos);

	LOGI("Estimated Total time: %lu  Estimated remaining time: %lu\n", total_time, remain_time);

	// And get the time
	if (Part->Backup_Method == 1)
		section_time = Part->Backup_Size / file_bps;
	else
		section_time = Part->Backup_Size / img_bps;

	// Set the position
	pos = section_time / (float) total_time;
	ui->ShowProgress(pos, section_time);

	time(&start);

	if (Part->Backup(Backup_Folder)) {
		if (Part->Has_SubPartition) {
			std::vector<TWPartition*>::iterator subpart;

			for (subpart = Partitions.begin(); subpart != Partitions.end(); subpart++) {
				if ((*subpart)->Is_SubPartition && (*subpart)->SubPartition_Of == Part->Mount_Point) {
					if (!(*subpart)->Backup(Backup_Folder))
						return false;
					if (!Make_MD5(generate_md5, Backup_Folder, (*subpart)->Backup_FileName))
						return false;
					if (Part->Backup_Method == 1) {
						*file_bytes_remaining -= (*subpart)->Backup_Size;
					} else {
						*img_bytes_remaining -= (*subpart)->Backup_Size;
					}
				}
			}
		}
		time(&stop);
		backup_time = (int) difftime(stop, start);
		LOGI("Partition Backup time: %d\n", backup_time);
		if (Part->Backup_Method == 1) {
			*file_bytes_remaining -= Part->Backup_Size;
			*file_time += backup_time;
		} else {
			*img_bytes_remaining -= Part->Backup_Size;
			*img_time += backup_time;
		}
		return Make_MD5(generate_md5, Backup_Folder, Part->Backup_FileName);
	} else {
		return false;
	}
}

int TWPartitionManager::Run_Backup(void) {
	int check, do_md5, partition_count = 0;
	string Backup_Folder, Backup_Name, Full_Backup_Path;
	unsigned long long total_bytes = 0, file_bytes = 0, img_bytes = 0, free_space = 0, img_bytes_remaining, file_bytes_remaining, subpart_size;
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
	std::vector<TWPartition*>::iterator subpart;
	struct tm *t;
	time_t start, stop, seconds, total_start, total_stop;
	seconds = time(0);
    t = localtime(&seconds);

	time(&total_start);

	Update_System_Details();

	if (!Mount_Current_Storage(true))
		return false;

	DataManager::GetValue(TW_SKIP_MD5_GENERATE_VAR, do_md5);
	if (do_md5 == 0)
		do_md5 = true;
	else
		do_md5 = false;

	DataManager::GetValue(TW_BACKUPS_FOLDER_VAR, Backup_Folder);
	DataManager::GetValue(TW_BACKUP_NAME, Backup_Name);
	if (Backup_Name == "(Current Date)" || Backup_Name == "0" || Backup_Name.empty()) {
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
			subpart_size = 0;
			if (backup_data->Has_SubPartition) {
				for (subpart = Partitions.begin(); subpart != Partitions.end(); subpart++) {
					if ((*subpart)->Is_SubPartition && (*subpart)->SubPartition_Of == backup_data->Mount_Point)
						subpart_size += (*subpart)->Backup_Size;
				}
			}
			if (backup_data->Backup_Method == 1)
				file_bytes += backup_data->Backup_Size + subpart_size;
			else
				img_bytes += backup_data->Backup_Size + subpart_size;
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
		backup_sp1 = Find_Partition_By_Path(EXPAND(SP1_NAME));
		if (backup_sp1 != NULL) {
			partition_count++;
			if (backup_sp1->Backup_Method == 1)
				file_bytes += backup_sp1->Backup_Size;
			else
				img_bytes += backup_sp1->Backup_Size;
		} else {
			LOGE("Unable to locate %s partition.\n", EXPAND(SP1_NAME));
			return false;
		}
	}
#endif
#ifdef SP2_NAME
	DataManager::GetValue(TW_BACKUP_SP2_VAR, check);
	if (check) {
		backup_sp2 = Find_Partition_By_Path(EXPAND(SP2_NAME));
		if (backup_sp2 != NULL) {
			partition_count++;
			if (backup_sp2->Backup_Method == 1)
				file_bytes += backup_sp2->Backup_Size;
			else
				img_bytes += backup_sp2->Backup_Size;
		} else {
			LOGE("Unable to locate %s partition.\n", EXPAND(SP2_NAME));
			return false;
		}
	}
#endif
#ifdef SP3_NAME
	DataManager::GetValue(TW_BACKUP_SP3_VAR, check);
	if (check) {
		backup_sp3 = Find_Partition_By_Path(EXPAND(SP3_NAME));
		if (backup_sp3 != NULL) {
			partition_count++;
			if (backup_sp3->Backup_Method == 1)
				file_bytes += backup_sp3->Backup_Size;
			else
				img_bytes += backup_sp3->Backup_Size;
		} else {
			LOGE("Unable to locate %s partition.\n", EXPAND(SP3_NAME));
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

	ui->SetProgress(0.0);

	if (!Backup_Partition(backup_sys, Full_Backup_Path, do_md5, &img_bytes_remaining, &file_bytes_remaining, &img_time, &file_time, &img_bytes, &file_bytes))
		return false;
	if (!Backup_Partition(backup_data, Full_Backup_Path, do_md5, &img_bytes_remaining, &file_bytes_remaining, &img_time, &file_time, &img_bytes, &file_bytes))
		return false;
	if (!Backup_Partition(backup_cache, Full_Backup_Path, do_md5, &img_bytes_remaining, &file_bytes_remaining, &img_time, &file_time, &img_bytes, &file_bytes))
		return false;
	if (!Backup_Partition(backup_recovery, Full_Backup_Path, do_md5, &img_bytes_remaining, &file_bytes_remaining, &img_time, &file_time, &img_bytes, &file_bytes))
		return false;
	if (!Backup_Partition(backup_boot, Full_Backup_Path, do_md5, &img_bytes_remaining, &file_bytes_remaining, &img_time, &file_time, &img_bytes, &file_bytes))
		return false;
	if (!Backup_Partition(backup_andsec, Full_Backup_Path, do_md5, &img_bytes_remaining, &file_bytes_remaining, &img_time, &file_time, &img_bytes, &file_bytes))
		return false;
	if (!Backup_Partition(backup_sdext, Full_Backup_Path, do_md5, &img_bytes_remaining, &file_bytes_remaining, &img_time, &file_time, &img_bytes, &file_bytes))
		return false;
	if (!Backup_Partition(backup_sp1, Full_Backup_Path, do_md5, &img_bytes_remaining, &file_bytes_remaining, &img_time, &file_time, &img_bytes, &file_bytes))
		return false;
	if (!Backup_Partition(backup_sp2, Full_Backup_Path, do_md5, &img_bytes_remaining, &file_bytes_remaining, &img_time, &file_time, &img_bytes, &file_bytes))
		return false;
	if (!Backup_Partition(backup_sp3, Full_Backup_Path, do_md5, &img_bytes_remaining, &file_bytes_remaining, &img_time, &file_time, &img_bytes, &file_bytes))
		return false;

	// Average BPS
	if (img_time == 0)
		img_time = 1;
	if (file_time == 0)
		file_time = 1;
	int img_bps = (int)img_bytes / (int)img_time;
	int file_bps = (int)file_bytes / (int)file_time;

	ui_print("Average backup rate for file systems: %lu MB/sec\n", (file_bps / (1024 * 1024)));
	ui_print("Average backup rate for imaged drives: %lu MB/sec\n", (img_bps / (1024 * 1024)));

	time(&total_stop);
	int total_time = (int) difftime(total_stop, total_start);
	unsigned long long actual_backup_size = TWFunc::Get_Folder_Size(Full_Backup_Path, true);
    actual_backup_size /= (1024LLU * 1024LLU);

	int prev_img_bps, prev_file_bps, use_compression;
	DataManager::GetValue(TW_BACKUP_AVG_IMG_RATE, prev_img_bps);
	img_bps += (prev_img_bps * 4);
    img_bps /= 5;

    DataManager::GetValue(TW_USE_COMPRESSION_VAR, use_compression);
	if (use_compression)
		DataManager::GetValue(TW_BACKUP_AVG_FILE_COMP_RATE, prev_file_bps);
    else
		DataManager::GetValue(TW_BACKUP_AVG_FILE_RATE, prev_file_bps);
	file_bps += (prev_file_bps * 4);
    file_bps /= 5;

    DataManager::SetValue(TW_BACKUP_AVG_IMG_RATE, img_bps);
	if (use_compression)
		DataManager::SetValue(TW_BACKUP_AVG_FILE_COMP_RATE, file_bps);
	else
		DataManager::SetValue(TW_BACKUP_AVG_FILE_RATE, file_bps);

	ui_print("[%llu MB TOTAL BACKED UP]\n", actual_backup_size);
	Update_System_Details();
	UnMount_Main_Partitions();
	ui_print("[BACKUP COMPLETED IN %d SECONDS]\n\n", total_time); // the end
    return true;
}

bool TWPartitionManager::Restore_Partition(TWPartition* Part, string Restore_Name, int partition_count) {
	time_t Start, Stop;
	time(&Start);
	ui->ShowProgress(1.0 / (float)partition_count, 150);
	if (!Part->Restore(Restore_Name))
		return false;
	if (Part->Has_SubPartition) {
		std::vector<TWPartition*>::iterator subpart;

		for (subpart = Partitions.begin(); subpart != Partitions.end(); subpart++) {
			if ((*subpart)->Is_SubPartition && (*subpart)->SubPartition_Of == Part->Mount_Point) {
				if (!(*subpart)->Restore(Restore_Name))
					return false;
			}
		}
	}
	time(&Stop);
	ui_print("[%s done (%d seconds)]\n\n", Part->Display_Name.c_str(), (int)difftime(Stop, Start));
	return true;
}

int TWPartitionManager::Run_Restore(string Restore_Name) {
	int check_md5, check, partition_count = 0;
	TWPartition* restore_sys = NULL;
	TWPartition* restore_data = NULL;
	TWPartition* restore_cache = NULL;
	TWPartition* restore_boot = NULL;
	TWPartition* restore_andsec = NULL;
	TWPartition* restore_sdext = NULL;
	TWPartition* restore_sp1 = NULL;
	TWPartition* restore_sp2 = NULL;
	TWPartition* restore_sp3 = NULL;
	time_t rStart, rStop;
	time(&rStart);

	ui_print("\n[RESTORE STARTED]\n\n");
	ui_print("Restore folder: '%s'\n", Restore_Name.c_str());

	if (!Mount_Current_Storage(true))
		return false;

	DataManager::GetValue(TW_SKIP_MD5_CHECK_VAR, check_md5);
	DataManager::GetValue(TW_RESTORE_SYSTEM_VAR, check);
	if (check > 0) {
		restore_sys = Find_Partition_By_Path("/system");
		if (restore_sys == NULL) {
			LOGE("Unable to locate system partition.\n");
			return false;
		}
		partition_count++;
	}
	DataManager::GetValue(TW_RESTORE_DATA_VAR, check);
	if (check > 0) {
		restore_data = Find_Partition_By_Path("/data");
		if (restore_data == NULL) {
			LOGE("Unable to locate data partition.\n");
			return false;
		}
		partition_count++;
	}
	DataManager::GetValue(TW_RESTORE_CACHE_VAR, check);
	if (check > 0) {
		restore_cache = Find_Partition_By_Path("/cache");
		if (restore_cache == NULL) {
			LOGE("Unable to locate cache partition.\n");
			return false;
		}
		partition_count++;
	}
	DataManager::GetValue(TW_RESTORE_BOOT_VAR, check);
	if (check > 0) {
		restore_boot = Find_Partition_By_Path("/boot");
		if (restore_boot == NULL) {
			LOGE("Unable to locate boot partition.\n");
			return false;
		}
		partition_count++;
	}
	DataManager::GetValue(TW_RESTORE_ANDSEC_VAR, check);
	if (check > 0) {
		restore_andsec = Find_Partition_By_Path("/and-sec");
		if (restore_andsec == NULL) {
			LOGE("Unable to locate android secure partition.\n");
			return false;
		}
		partition_count++;
	}
	DataManager::GetValue(TW_RESTORE_SDEXT_VAR, check);
	if (check > 0) {
		restore_sdext = Find_Partition_By_Path("/sd-ext");
		if (restore_sdext == NULL) {
			LOGE("Unable to locate sd-ext partition.\n");
			return false;
		}
		partition_count++;
	}
#ifdef SP1_NAME
	DataManager::GetValue(TW_RESTORE_SP1_VAR, check);
	if (check > 0) {
		restore_sp1 = Find_Partition_By_Path(EXPAND(SP1_NAME));
		if (restore_sp1 == NULL) {
			LOGE("Unable to locate %s partition.\n", EXPAND(SP1_NAME));
			return false;
		}
		partition_count++;
	}
#endif
#ifdef SP2_NAME
	DataManager::GetValue(TW_RESTORE_SP2_VAR, check);
	if (check > 0) {
		restore_sp2 = Find_Partition_By_Path(EXPAND(SP2_NAME));
		if (restore_sp2 == NULL) {
			LOGE("Unable to locate %s partition.\n", EXPAND(SP2_NAME));
			return false;
		}
		partition_count++;
	}
#endif
#ifdef SP3_NAME
	DataManager::GetValue(TW_RESTORE_SP3_VAR, check);
	if (check > 0) {
		restore_sp3 = Find_Partition_By_Path(EXPAND(SP3_NAME));
		if (restore_sp3 == NULL) {
			LOGE("Unable to locate %s partition.\n", EXPAND(SP3_NAME));
			return false;
		}
		partition_count++;
	}
#endif

	if (partition_count == 0) {
		LOGE("No partitions selected for restore.\n");
		return false;
	}

	if (check_md5 > 0) {
		// Check MD5 files first before restoring to ensure that all of them match before starting a restore
		TWFunc::GUI_Operation_Text(TW_VERIFY_MD5_TEXT, "Verifying MD5");
		ui_print("Verifying MD5...\n");
		if (restore_sys != NULL && !restore_sys->Check_MD5(Restore_Name))
			return false;
		if (restore_data != NULL && !restore_data->Check_MD5(Restore_Name))
			return false;
		if (restore_data != NULL && restore_data->Has_SubPartition) {
			std::vector<TWPartition*>::iterator subpart;

			for (subpart = Partitions.begin(); subpart != Partitions.end(); subpart++) {
				if ((*subpart)->Is_SubPartition && (*subpart)->SubPartition_Of == restore_data->Mount_Point) {
					if (!(*subpart)->Check_MD5(Restore_Name))
						return false;
				}
			}
		}
		if (restore_cache != NULL && !restore_cache->Check_MD5(Restore_Name))
			return false;
		if (restore_boot != NULL && !restore_boot->Check_MD5(Restore_Name))
			return false;
		if (restore_andsec != NULL && !restore_andsec->Check_MD5(Restore_Name))
			return false;
		if (restore_sdext != NULL && !restore_sdext->Check_MD5(Restore_Name))
			return false;
		if (restore_sp1 != NULL && !restore_sp1->Check_MD5(Restore_Name))
			return false;
		if (restore_sp2 != NULL && !restore_sp2->Check_MD5(Restore_Name))
			return false;
		if (restore_sp3 != NULL && !restore_sp3->Check_MD5(Restore_Name))
			return false;
		ui_print("Done verifying MD5.\n");
	} else
			ui_print("Skipping MD5 check based on user setting.\n");

	ui_print("Restoring %i partitions...\n", partition_count);
	ui->SetProgress(0.0);
	if (restore_sys != NULL && !Restore_Partition(restore_sys, Restore_Name, partition_count))
		return false;
	if (restore_data != NULL && !Restore_Partition(restore_data, Restore_Name, partition_count))
		return false;
	if (restore_cache != NULL && !Restore_Partition(restore_cache, Restore_Name, partition_count))
		return false;
	if (restore_boot != NULL && !Restore_Partition(restore_boot, Restore_Name, partition_count))
		return false;
	if (restore_andsec != NULL && !Restore_Partition(restore_andsec, Restore_Name, partition_count))
		return false;
	if (restore_sdext != NULL && !Restore_Partition(restore_sdext, Restore_Name, partition_count))
		return false;
	if (restore_sp1 != NULL && !Restore_Partition(restore_sp1, Restore_Name, partition_count))
		return false;
	if (restore_sp2 != NULL && !Restore_Partition(restore_sp2, Restore_Name, partition_count))
		return false;
	if (restore_sp3 != NULL && !Restore_Partition(restore_sp3, Restore_Name, partition_count))
		return false;

	TWFunc::GUI_Operation_Text(TW_UPDATE_SYSTEM_DETAILS_TEXT, "Updating System Details");
	Update_System_Details();
	UnMount_Main_Partitions();
	time(&rStop);
	ui_print("[RESTORE COMPLETED IN %d SECONDS]\n\n",(int)difftime(rStop,rStart));
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
		if (Part->Backup_Path == "/system")
			tw_restore_system = 1;
		if (Part->Backup_Path == "/data")
			tw_restore_data = 1;
		if (Part->Backup_Path == "/cache")
			tw_restore_cache = 1;
		if (Part->Backup_Path == "/recovery")
			tw_restore_recovery = 1;
		if (Part->Backup_Path == "/boot")
			tw_restore_boot = 1;
		if (Part->Backup_Path == "/and-sec")
			tw_restore_andsec = 1;
		if (Part->Backup_Path == "/sd-ext")
			tw_restore_sdext = 1;
#ifdef SP1_NAME
		if (Part->Backup_Path == TWFunc::Get_Root_Path(EXPAND(SP1_NAME)))
			tw_restore_sp1 = 1;
#endif
#ifdef SP2_NAME
		if (Part->Backup_Path == TWFunc::Get_Root_Path(EXPAND(SP2_NAME)))
			tw_restore_sp2 = 1;
#endif
#ifdef SP3_NAME
		if (Part->Backup_Path == TWFunc::Get_Root_Path(EXPAND(SP3_NAME)))
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
			if (Path == "/and-sec")
				ret = (*iter)->Wipe_AndSec();
			else
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
		} else if ((*iter)->Has_Android_Secure) {
			if (!(*iter)->Wipe_AndSec())
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
	system("rm -rf /data/dalvik-cache");
	ui_print("Cleaned: /data/dalvik-cache...\n");
	system("rm -rf /cache/dalvik-cache");
	ui_print("Cleaned: /cache/dalvik-cache...\n");
	system("rm -rf /cache/dc");
	ui_print("Cleaned: /cache/dc\n");

	TWPartition* sdext = Find_Partition_By_Path("/sd-ext");
	if (sdext != NULL) {
		if (sdext->Is_Present && sdext->Mount(false)) {
			if (stat("/sd-ext/dalvik-cache", &st) == 0) {
                system("rm -rf /sd-ext/dalvik-cache");
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

	system("rm -r /data/misc/akmd*");
	system("rm -r /data/misc/rild*");
	system("rm -r /data/misc/rild*");
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

int TWPartitionManager::Wipe_Android_Secure(void) {
	std::vector<TWPartition*>::iterator iter;
	int ret = false;
	bool found = false;

	// Iterate through all partitions
	for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		if ((*iter)->Has_Android_Secure) {
			ret = (*iter)->Wipe_AndSec();
			found = true;
		}
	}
	if (found) {
		return ret;
	} else {
		LOGE("No android secure partitions found.\n");
	}
	return false;
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
		system("rm -rf /data/media");
		system("cd /data && mkdir media && chmod 775 media");
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
			} else if ((*iter)->Has_Android_Secure) {
				int backup_display_size = (int)((*iter)->Backup_Size / 1048576LLU);
				DataManager::SetValue(TW_BACKUP_ANDSEC_SIZE, backup_display_size);
				if ((*iter)->Backup_Size == 0) {
					DataManager::SetValue(TW_HAS_ANDROID_SECURE, 0);
					DataManager::SetValue(TW_BACKUP_ANDSEC_VAR, 0);
				} else
					DataManager::SetValue(TW_HAS_ANDROID_SECURE, 1);
			} else if ((*iter)->Mount_Point == "/boot") {
				int backup_display_size = (int)((*iter)->Backup_Size / 1048576LLU);
				DataManager::SetValue(TW_BACKUP_BOOT_SIZE, backup_display_size);
				if ((*iter)->Backup_Size == 0) {
					DataManager::SetValue("tw_has_boot_partition", 0);
					DataManager::SetValue(TW_BACKUP_BOOT_VAR, 0);
				} else
					DataManager::SetValue("tw_has_boot_partition", 1);
			}
#ifdef SP1_NAME
			if ((*iter)->Backup_Name == EXPAND(SP1_NAME)) {
				int backup_display_size = (int)((*iter)->Backup_Size / 1048576LLU);
				DataManager::SetValue(TW_BACKUP_SP1_SIZE, backup_display_size);
			}
#endif
#ifdef SP2_NAME
			if ((*iter)->Backup_Name == EXPAND(SP2_NAME)) {
				int backup_display_size = (int)((*iter)->Backup_Size / 1048576LLU);
				DataManager::SetValue(TW_BACKUP_SP2_SIZE, backup_display_size);
			}
#endif
#ifdef SP3_NAME
			if ((*iter)->Backup_Name == EXPAND(SP3_NAME)) {
				int backup_display_size = (int)((*iter)->Backup_Size / 1048576LLU);
				DataManager::SetValue(TW_BACKUP_SP3_SIZE, backup_display_size);
			}
#endif
		}
	}
	DataManager::SetValue(TW_BACKUP_DATA_SIZE, data_size);
	string current_storage_path = DataManager::GetCurrentStoragePath();
	TWPartition* FreeStorage = Find_Partition_By_Path(current_storage_path);
	if (FreeStorage != NULL) {
		// Attempt to mount storage
		if (!FreeStorage->Mount(false)) {
			// We couldn't mount storage... check to see if we have dual storage
			int has_dual_storage;
			DataManager::GetValue(TW_HAS_DUAL_STORAGE, has_dual_storage);
			if (has_dual_storage == 1) {
				// We have dual storage, see if we're using the internal storage that should always be present
				if (current_storage_path == DataManager::GetSettingsStoragePath()) {
					if (!FreeStorage->Is_Encrypted) {
						// Not able to use internal, so error!
						LOGE("Unable to mount internal storage.\n");
					}
					DataManager::SetValue(TW_STORAGE_FREE_SIZE, 0);
				} else {
					// We were using external, flip to internal
					DataManager::SetValue(TW_USE_EXTERNAL_STORAGE, 0);
					current_storage_path = DataManager::GetCurrentStoragePath();
					FreeStorage = Find_Partition_By_Path(current_storage_path);
					if (FreeStorage != NULL) {
						DataManager::SetValue(TW_STORAGE_FREE_SIZE, (int)(FreeStorage->Free / 1048576LLU));
					} else {
						LOGE("Unable to locate internal storage partition.\n");
						DataManager::SetValue(TW_STORAGE_FREE_SIZE, 0);
					}
				}
			} else {
				// No dual storage and unable to mount storage, error!
				LOGE("Unable to mount storage.\n");
				DataManager::SetValue(TW_STORAGE_FREE_SIZE, 0);
			}
		} else {
			DataManager::SetValue(TW_STORAGE_FREE_SIZE, (int)(FreeStorage->Free / 1048576LLU));
		}
	} else {
		LOGI("Unable to find storage partition '%s'.\n", current_storage_path.c_str());
	}
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
			UnMount_Main_Partitions();
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
	int result = 0;
	if (!Mount_By_Path("/data", true))
		return false;

	if (!Mount_By_Path("/system", true))
		return false;

	Mount_By_Path("/sd-ext", false);

	fixPermissions perms;
	result = perms.fixPerms(true, false);
	ui_print("Done.\n\n");
	return result;
}

//partial kangbang from system/vold
#ifndef CUSTOM_LUN_FILE
#define CUSTOM_LUN_FILE "/sys/devices/platform/usb_mass_storage/lun%d/file"
#endif

int TWPartitionManager::usb_storage_enable(void) {
	int fd, has_dual, has_data_media;
	char lun_file[255];
	TWPartition* Part;
	string ext_path;

	DataManager::GetValue(TW_HAS_DUAL_STORAGE, has_dual);
	DataManager::GetValue(TW_HAS_DATA_MEDIA, has_data_media);
	if (has_dual == 1 && has_data_media == 0) {
		Part = Find_Partition_By_Path(DataManager::GetSettingsStoragePath());
		if (Part == NULL) {
			LOGE("Unable to locate volume information.");
			return false;
		}
		if (!Part->UnMount(true))
			return false;

		sprintf(lun_file, CUSTOM_LUN_FILE, 0);
		if ((fd = open(lun_file, O_WRONLY)) < 0) {
			LOGE("Unable to open ums lunfile '%s': (%s)\n", lun_file, strerror(errno));
			return false;
		}

		if (write(fd, Part->Actual_Block_Device.c_str(), Part->Actual_Block_Device.size()) < 0) {
			LOGE("Unable to write to ums lunfile '%s': (%s)\n", lun_file, strerror(errno));
			close(fd);
			return false;
		}
		close(fd);

		DataManager::GetValue(TW_EXTERNAL_PATH, ext_path);
		Part = Find_Partition_By_Path(ext_path);
		if (Part == NULL) {
			LOGE("Unable to locate volume information.\n");
			return false;
		}
		if (!Part->UnMount(true))
			return false;

		sprintf(lun_file, CUSTOM_LUN_FILE, 1);
		if ((fd = open(lun_file, O_WRONLY)) < 0) {
			LOGE("Unable to open ums lunfile '%s': (%s)\n", lun_file, strerror(errno));
			return false;
		}

		if (write(fd, Part->Actual_Block_Device.c_str(), Part->Actual_Block_Device.size()) < 0) {
			LOGE("Unable to write to ums lunfile '%s': (%s)\n", lun_file, strerror(errno));
			close(fd);
			return false;
		}
		close(fd);
	} else {
		if (has_data_media == 0)
			ext_path = DataManager::GetCurrentStoragePath();
		else
			DataManager::GetValue(TW_EXTERNAL_PATH, ext_path);

		Part = Find_Partition_By_Path(ext_path);
		if (Part == NULL) {
			LOGE("Unable to locate volume information.\n");
			return false;
		}
		if (!Part->UnMount(true))
			return false;

		sprintf(lun_file, CUSTOM_LUN_FILE, 0);

		if ((fd = open(lun_file, O_WRONLY)) < 0) {
			LOGE("Unable to open ums lunfile '%s': (%s)\n", lun_file, strerror(errno));
			return false;
		}

		if (write(fd, Part->Actual_Block_Device.c_str(), Part->Actual_Block_Device.size()) < 0) {
			LOGE("Unable to write to ums lunfile '%s': (%s)\n", lun_file, strerror(errno));
			close(fd);
			return false;
		}
		close(fd);
	}
	return true;
}

int TWPartitionManager::usb_storage_disable(void) {
	int fd, index;
	char lun_file[255];

	for (index=0; index<2; index++) {
		sprintf(lun_file, CUSTOM_LUN_FILE, index);

		if ((fd = open(lun_file, O_WRONLY)) < 0) {
			Mount_All_Storage();
			Update_System_Details();
			if (index == 0) {
				LOGE("Unable to open ums lunfile '%s': (%s)", lun_file, strerror(errno));
				return false;
			} else
				return true;
		}

		char ch = 0;
		if (write(fd, &ch, 1) < 0) {
			close(fd);
			Mount_All_Storage();
			Update_System_Details();
			if (index == 0) {
				LOGE("Unable to write to ums lunfile '%s': (%s)", lun_file, strerror(errno));
				return false;
			} else
				return true;
		}

		close(fd);
	}
	Mount_All_Storage();
	Update_System_Details();
	UnMount_Main_Partitions();
	return true;
}

void TWPartitionManager::Mount_All_Storage(void) {
	std::vector<TWPartition*>::iterator iter;

	for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		if ((*iter)->Is_Storage)
			(*iter)->Mount(false);
	}
}

void TWPartitionManager::UnMount_Main_Partitions(void) {
	// Unmounts system and data if data is not data/media
	// Also unmounts boot if boot is mountable
	LOGI("Unmounting main partitions...\n");

	TWPartition* Boot_Partition = Find_Partition_By_Path("/boot");

	UnMount_By_Path("/system", true);
#ifndef RECOVERY_SDCARD_ON_DATA
	UnMount_By_Path("/data", true);
#endif
	if (Boot_Partition != NULL && Boot_Partition->Can_Be_Mounted)
		Boot_Partition->UnMount(true);
}

int TWPartitionManager::Partition_SDCard(void) {
	char mkdir_path[255], temp[255], line[512];
	string Command, Device, fat_str, ext_str, swap_str, start_loc, end_loc, ext_format, sd_path, tmpdevice;
	int ext, swap, total_size = 0, fat_size;
	FILE* fp;

	ui_print("Partitioning SD Card...\n");
#ifdef TW_EXTERNAL_STORAGE_PATH
	TWPartition* SDCard = Find_Partition_By_Path(EXPAND(TW_EXTERNAL_STORAGE_PATH));
#else
	TWPartition* SDCard = Find_Partition_By_Path("/sdcard");
#endif
	if (SDCard == NULL) {
		LOGE("Unable to locate device to partition.\n");
		return false;
	}
	if (!SDCard->UnMount(true))
		return false;
	TWPartition* SDext = Find_Partition_By_Path("/sd-ext");
	if (SDext != NULL) {
		if (!SDext->UnMount(true))
			return false;
	}
	system("umount \"$SWAPPATH\"");
	Device = SDCard->Actual_Block_Device;
	// Just use the root block device
	Device.resize(strlen("/dev/block/mmcblkX"));

	// Find the size of the block device:
	fp = fopen("/proc/partitions", "rt");
	if (fp == NULL) {
		LOGE("Unable to open /proc/partitions\n");
		return false;
	}

	while (fgets(line, sizeof(line), fp) != NULL)
	{
		unsigned long major, minor, blocks;
		char device[512];
		char tmpString[64];

		if (strlen(line) < 7 || line[0] == 'm')	 continue;
		sscanf(line + 1, "%lu %lu %lu %s", &major, &minor, &blocks, device);

		tmpdevice = "/dev/block/";
		tmpdevice += device;
		if (tmpdevice == Device) {
			// Adjust block size to byte size
			total_size = (int)(blocks * 1024ULL  / 1000000LLU);
			break;
		}
	}
	fclose(fp);

	DataManager::GetValue("tw_sdext_size", ext);
	DataManager::GetValue("tw_swap_size", swap);
	DataManager::GetValue("tw_sdpart_file_system", ext_format);
	fat_size = total_size - ext - swap;
	LOGI("sd card block device is '%s', sdcard size is: %iMB, fat size: %iMB, ext size: %iMB, ext system: '%s', swap size: %iMB\n", Device.c_str(), total_size, fat_size, ext, ext_format.c_str(), swap);
	memset(temp, 0, sizeof(temp));
	sprintf(temp, "%i", fat_size);
	fat_str = temp;
	memset(temp, 0, sizeof(temp));
	sprintf(temp, "%i", fat_size + ext);
	ext_str = temp;
	memset(temp, 0, sizeof(temp));
	sprintf(temp, "%i", fat_size + ext + swap);
	swap_str = temp;
	if (ext + swap > total_size) {
		LOGE("EXT + Swap size is larger than sdcard size.\n");
		return false;
	}
	ui_print("Removing partition table...\n");
	Command = "parted -s " + Device + " mklabel msdos";
	LOGI("Command is: '%s'\n", Command.c_str());
	if (system(Command.c_str()) != 0) {
		LOGE("Unable to remove partition table.\n");
		Update_System_Details();
		return false;
	}
	ui_print("Creating FAT32 partition...\n");
	Command = "parted " + Device + " mkpartfs primary fat32 0 " + fat_str + "MB";
	LOGI("Command is: '%s'\n", Command.c_str());
	if (system(Command.c_str()) != 0) {
		LOGE("Unable to create FAT32 partition.\n");
		return false;
	}
	if (ext > 0) {
		ui_print("Creating EXT partition...\n");
		Command = "parted " + Device + " mkpartfs primary ext2 " + fat_str + "MB " + ext_str + "MB";
		LOGI("Command is: '%s'\n", Command.c_str());
		if (system(Command.c_str()) != 0) {
			LOGE("Unable to create EXT partition.\n");
			Update_System_Details();
			return false;
		}
	}
	if (swap > 0) {
		ui_print("Creating swap partition...\n");
		Command = "parted " + Device + " mkpartfs primary linux-swap " + ext_str + "MB " + swap_str + "MB";
		LOGI("Command is: '%s'\n", Command.c_str());
		if (system(Command.c_str()) != 0) {
			LOGE("Unable to create swap partition.\n");
			Update_System_Details();
			return false;
		}
	}
	// recreate TWRP folder and rewrite settings - these will be gone after sdcard is partitioned
#ifdef TW_EXTERNAL_STORAGE_PATH
	Mount_By_Path(EXPAND(TW_EXTERNAL_STORAGE_PATH), 1);
	DataManager::GetValue(TW_EXTERNAL_PATH, sd_path);
	memset(mkdir_path, 0, sizeof(mkdir_path));
	sprintf(mkdir_path, "%s/TWRP", sd_path.c_str());
#else
	Mount_By_Path("/sdcard", 1);
	strcpy(mkdir_path, "/sdcard/TWRP");
#endif
	mkdir(mkdir_path, 0777);
	DataManager::Flush();
#ifdef TW_EXTERNAL_STORAGE_PATH
	DataManager::SetValue(TW_ZIP_EXTERNAL_VAR, EXPAND(TW_EXTERNAL_STORAGE_PATH));
	if (DataManager::GetIntValue(TW_USE_EXTERNAL_STORAGE) == 1)
		DataManager::SetValue(TW_ZIP_LOCATION_VAR, EXPAND(TW_EXTERNAL_STORAGE_PATH));
#else
	DataManager::SetValue(TW_ZIP_EXTERNAL_VAR, "/sdcard");
	if (DataManager::GetIntValue(TW_USE_EXTERNAL_STORAGE) == 1)
		DataManager::SetValue(TW_ZIP_LOCATION_VAR, "/sdcard");
#endif
	if (ext > 0) {
		if (SDext == NULL) {
			LOGE("Unable to locate sd-ext partition.\n");
			return false;
		}
		Command = "mke2fs -t " + ext_format + " -m 0 " + SDext->Actual_Block_Device;
		ui_print("Formatting sd-ext as %s...\n", ext_format.c_str());
		LOGI("Formatting sd-ext after partitioning, command: '%s'\n", Command.c_str());
		system(Command.c_str());
	}

	Update_System_Details();
	ui_print("Partitioning complete.\n");
	return true;
}
