/*
	Copyright 2012 bigbiff/Dees_Troy TeamWin
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
#include <unistd.h>
#include <vector>
#include <dirent.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <iomanip>
#include "variables.h"
#include "twcommon.h"
#include "partitions.hpp"
#include "data.hpp"
#include "twrp-functions.hpp"
#include "fixPermissions.hpp"
#include "twrpDigest.hpp"
#include "twrpDU.hpp"

extern "C" {
	#include "cutils/properties.h"
}

#ifdef TW_INCLUDE_CRYPTO
	#ifdef TW_INCLUDE_JB_CRYPTO
		#include "crypto/jb/cryptfs.h"
	#else
		#include "crypto/ics/cryptfs.h"
	#endif
#endif

TWPartitionManager::TWPartitionManager(void) {
}

int TWPartitionManager::Process_Fstab(string Fstab_Filename, bool Display_Error) {
	FILE *fstabFile;
	char fstab_line[MAX_FSTAB_LINE_LENGTH];
	bool Found_Settings_Storage = false;

	fstabFile = fopen(Fstab_Filename.c_str(), "rt");
	if (fstabFile == NULL) {
		LOGERR("Critical Error: Unable to open fstab at '%s'.\n", Fstab_Filename.c_str());
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
			if (!Found_Settings_Storage && partition->Is_Settings_Storage) {
				Found_Settings_Storage = true;
				Setup_Settings_Storage_Partition(partition);
			} else {
				partition->Is_Settings_Storage = false;
			}
			Partitions.push_back(partition);
		} else {
			delete partition;
		}
	}
	fclose(fstabFile);
	if (!Found_Settings_Storage) {
		std::vector<TWPartition*>::iterator iter;
		for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
			if ((*iter)->Is_Storage) {
				Setup_Settings_Storage_Partition((*iter));
				break;
			}
		}
		if (!Found_Settings_Storage)
			LOGERR("Unable to locate storage partition for storing settings file.\n");
	}
	if (!Write_Fstab()) {
		if (Display_Error)
			LOGERR("Error creating fstab\n");
		else
			LOGINFO("Error creating fstab\n");
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
		LOGINFO("Can not open /etc/fstab.\n");
		return false;
	}
	for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		if ((*iter)->Can_Be_Mounted) {
			Line = (*iter)->Actual_Block_Device + " " + (*iter)->Mount_Point + " " + (*iter)->Current_File_System + " rw\n";
			fputs(Line.c_str(), fp);
		}
		// Handle subpartition tracking
		if ((*iter)->Is_SubPartition) {
			TWPartition* ParentPartition = Find_Partition_By_Path((*iter)->SubPartition_Of);
			if (ParentPartition)
				ParentPartition->Has_SubPartition = true;
			else
				LOGERR("Unable to locate parent partition '%s' of '%s'\n", (*iter)->SubPartition_Of.c_str(), (*iter)->Mount_Point.c_str());
		}
	}
	fclose(fp);
	return true;
}

void TWPartitionManager::Setup_Settings_Storage_Partition(TWPartition* Part) {
#ifndef RECOVERY_SDCARD_ON_DATA
	Part->Setup_AndSec();
#endif
	DataManager::SetValue("tw_settings_path", Part->Storage_Path);
	DataManager::SetValue("tw_storage_path", Part->Storage_Path);
	LOGINFO("Settings storage is '%s'\n", Part->Storage_Path.c_str());
}

void TWPartitionManager::Output_Partition_Logging(void) {
	std::vector<TWPartition*>::iterator iter;

	printf("\n\nPartition Logs:\n");
	for (iter = Partitions.begin(); iter != Partitions.end(); iter++)
		Output_Partition((*iter));
}

void TWPartitionManager::Output_Partition(TWPartition* Part) {
	unsigned long long mb = 1048576;

	printf("%s | %s | Size: %iMB", Part->Mount_Point.c_str(), Part->Actual_Block_Device.c_str(), (int)(Part->Size / mb));
	if (Part->Can_Be_Mounted) {
		printf(" Used: %iMB Free: %iMB Backup Size: %iMB", (int)(Part->Used / mb), (int)(Part->Free / mb), (int)(Part->Backup_Size / mb));
	}
	printf("\n   Flags: ");
	if (Part->Can_Be_Mounted)
		printf("Can_Be_Mounted ");
	if (Part->Can_Be_Wiped)
		printf("Can_Be_Wiped ");
	if (Part->Use_Rm_Rf)
		printf("Use_Rm_Rf ");
	if (Part->Can_Be_Backed_Up)
		printf("Can_Be_Backed_Up ");
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
	if (Part->Can_Encrypt_Backup)
		printf("Can_Encrypt_Backup ");
	if (Part->Use_Userdata_Encryption)
		printf("Use_Userdata_Encryption ");
	if (Part->Has_Android_Secure)
		printf("Has_Android_Secure ");
	if (Part->Is_Storage)
		printf("Is_Storage ");
	if (Part->Is_Settings_Storage)
		printf("Is_Settings_Storage ");
	if (Part->Ignore_Blkid)
		printf("Ignore_Blkid ");
	if (Part->Retain_Layout_Version)
		printf("Retain_Layout_Version ");
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
	if (!Part->Storage_Name.empty())
		printf("   Storage_Name: %s\n", Part->Storage_Name.c_str());
	if (!Part->Backup_Path.empty())
		printf("   Backup_Path: %s\n", Part->Backup_Path.c_str());
	if (!Part->Backup_Name.empty())
		printf("   Backup_Name: %s\n", Part->Backup_Name.c_str());
	if (!Part->Backup_Display_Name.empty())
		printf("   Backup_Display_Name: %s\n", Part->Backup_Display_Name.c_str());
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
	if (!Part->MTD_Name.empty())
		printf("   MTD_Name: %s\n", Part->MTD_Name.c_str());
	string back_meth = Part->Backup_Method_By_Name();
	printf("   Backup_Method: %s\n\n", back_meth.c_str());
	if (Part->Mount_Flags || !Part->Mount_Options.empty())
		printf("   Mount_Flags=0x%8x, Mount_Options=%s\n", Part->Mount_Flags, Part->Mount_Options.c_str());
}

int TWPartitionManager::Mount_By_Path(string Path, bool Display_Error) {
	std::vector<TWPartition*>::iterator iter;
	int ret = false;
	bool found = false;
	string Local_Path = TWFunc::Get_Root_Path(Path);

	if (Local_Path == "/tmp" || Local_Path == "/")
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
		LOGERR("Mount: Unable to find partition for path '%s'\n", Local_Path.c_str());
	} else {
		LOGINFO("Mount: Unable to find partition for path '%s'\n", Local_Path.c_str());
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
		LOGERR("Mount: Unable to find partition for block '%s'\n", Block.c_str());
	else
		LOGINFO("Mount: Unable to find partition for block '%s'\n", Block.c_str());
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
		LOGERR("Mount: Unable to find partition for name '%s'\n", Name.c_str());
	else
		LOGINFO("Mount: Unable to find partition for name '%s'\n", Name.c_str());
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
		LOGERR("UnMount: Unable to find partition for path '%s'\n", Local_Path.c_str());
	} else {
		LOGINFO("UnMount: Unable to find partition for path '%s'\n", Local_Path.c_str());
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
		LOGERR("UnMount: Unable to find partition for block '%s'\n", Block.c_str());
	else
		LOGINFO("UnMount: Unable to find partition for block '%s'\n", Block.c_str());
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
		LOGERR("UnMount: Unable to find partition for name '%s'\n", Name.c_str());
	else
		LOGINFO("UnMount: Unable to find partition for name '%s'\n", Name.c_str());
	return false;
}

int TWPartitionManager::Is_Mounted_By_Path(string Path) {
	TWPartition* Part = Find_Partition_By_Path(Path);

	if (Part)
		return Part->Is_Mounted();
	else
		LOGINFO("Is_Mounted: Unable to find partition for path '%s'\n", Path.c_str());
	return false;
}

int TWPartitionManager::Is_Mounted_By_Block(string Block) {
	TWPartition* Part = Find_Partition_By_Block(Block);

	if (Part)
		return Part->Is_Mounted();
	else
		LOGINFO("Is_Mounted: Unable to find partition for block '%s'\n", Block.c_str());
	return false;
}

int TWPartitionManager::Is_Mounted_By_Name(string Name) {
	TWPartition* Part = Find_Partition_By_Name(Name);

	if (Part)
		return Part->Is_Mounted();
	else
		LOGINFO("Is_Mounted: Unable to find partition for name '%s'\n", Name.c_str());
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
			LOGERR("Backup name is too long.\n");
		return -2;
	}

	// Check each character
	strncpy(backup_name, Backup_Name.c_str(), copy_size);
	if (copy_size == 1 && strncmp(backup_name, "0", 1) == 0)
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
				LOGERR("Backup name '%s' contains invalid character: '%c'\n", backup_name, (char)cur_char);
			return -3;
		}
	}

	// Check to make sure that a backup with this name doesn't already exist
	DataManager::GetValue(TW_BACKUPS_FOLDER_VAR, Backup_Loc);
	strcpy(backup_loc, Backup_Loc.c_str());
	sprintf(tw_image_dir,"%s/%s", backup_loc, Backup_Name.c_str());
    if (TWFunc::Path_Exists(tw_image_dir)) {
		if (Display_Error)
			LOGERR("A backup with this name already exists.\n");
		return -4;
	}
	// No problems found, return 0
	return 0;
}

bool TWPartitionManager::Make_MD5(bool generate_md5, string Backup_Folder, string Backup_Filename)
{
	string command;
	string Full_File = Backup_Folder + Backup_Filename;
	string result;
	twrpDigest md5sum;

	if (!generate_md5) 
		return true;

	TWFunc::GUI_Operation_Text(TW_GENERATE_MD5_TEXT, "Generating MD5");
	gui_print(" * Generating md5...\n");

	if (TWFunc::Path_Exists(Full_File)) {
		md5sum.setfn(Backup_Folder + Backup_Filename);
		if (md5sum.computeMD5() == 0)
			if (md5sum.write_md5digest() == 0)
				gui_print(" * MD5 Created.\n");
			else
				return -1;
		else
			gui_print(" * MD5 Error!\n");
	} else {
		char filename[512];
		int index = 0;
		string strfn;
		sprintf(filename, "%s%03i", Full_File.c_str(), index);
		strfn = filename;
		while (index < 1000) {
			md5sum.setfn(filename);
			if (TWFunc::Path_Exists(filename)) {
				if (md5sum.computeMD5() == 0) {
					if (md5sum.write_md5digest() != 0)
					{
						gui_print(" * MD5 Error.\n");
						return false;
					}
				} else {
					gui_print(" * Error computing MD5.\n");
					return false;
				}
			}
			index++;
			sprintf(filename, "%s%03i", Full_File.c_str(), index);
			strfn = filename;
		}
		if (index == 0) {
			LOGERR("Backup file: '%s' not found!\n", filename);
			return false;
		}
		gui_print(" * MD5 Created.\n");
	}
	return true;
}

bool TWPartitionManager::Backup_Partition(TWPartition* Part, string Backup_Folder, bool generate_md5, unsigned long long* img_bytes_remaining, unsigned long long* file_bytes_remaining, unsigned long *img_time, unsigned long *file_time, unsigned long long *img_bytes, unsigned long long *file_bytes) {
	time_t start, stop;
	int img_bps;
	unsigned long long file_bps;
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
	DataManager::SetProgress(pos);

	LOGINFO("Estimated Total time: %lu  Estimated remaining time: %lu\n", total_time, remain_time);

	// And get the time
	if (Part->Backup_Method == 1)
		section_time = Part->Backup_Size / file_bps;
	else
		section_time = Part->Backup_Size / img_bps;

	// Set the position
	pos = section_time / (float) total_time;
	DataManager::ShowProgress(pos, section_time);

	time(&start);

	if (Part->Backup(Backup_Folder)) {
		if (Part->Has_SubPartition) {
			std::vector<TWPartition*>::iterator subpart;

			for (subpart = Partitions.begin(); subpart != Partitions.end(); subpart++) {
				if ((*subpart)->Can_Be_Backed_Up && (*subpart)->Is_SubPartition && (*subpart)->SubPartition_Of == Part->Mount_Point) {
					if (!(*subpart)->Backup(Backup_Folder))
						return false;
					sync();
					sync();
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
		LOGINFO("Partition Backup time: %d\n", backup_time);
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
	string Backup_Folder, Backup_Name, Full_Backup_Path, Backup_List, backup_path;
	unsigned long long total_bytes = 0, file_bytes = 0, img_bytes = 0, free_space = 0, img_bytes_remaining, file_bytes_remaining, subpart_size;
	unsigned long img_time = 0, file_time = 0;
	TWPartition* backup_part = NULL;
	TWPartition* storage = NULL;
	std::vector<TWPartition*>::iterator subpart;
	struct tm *t;
	time_t start, stop, seconds, total_start, total_stop;
	size_t start_pos = 0, end_pos = 0;
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
	if (Backup_Name == "(Current Date)") {
		Backup_Name = TWFunc::Get_Current_Date();
	} else if (Backup_Name == "(Auto Generate)" || Backup_Name == "0" || Backup_Name.empty()) {
		TWFunc::Auto_Generate_Backup_Name();
		DataManager::GetValue(TW_BACKUP_NAME, Backup_Name);
	}
	LOGINFO("Backup Name is: '%s'\n", Backup_Name.c_str());
	Full_Backup_Path = Backup_Folder + "/" + Backup_Name + "/";
	LOGINFO("Full_Backup_Path is: '%s'\n", Full_Backup_Path.c_str());

	LOGINFO("Calculating backup details...\n");
	DataManager::GetValue("tw_backup_list", Backup_List);
	if (!Backup_List.empty()) {
		end_pos = Backup_List.find(";", start_pos);
		while (end_pos != string::npos && start_pos < Backup_List.size()) {
			backup_path = Backup_List.substr(start_pos, end_pos - start_pos);
			backup_part = Find_Partition_By_Path(backup_path);
			if (backup_part != NULL) {
				partition_count++;
				if (backup_part->Backup_Method == 1)
					file_bytes += backup_part->Backup_Size;
				else
					img_bytes += backup_part->Backup_Size;
				if (backup_part->Has_SubPartition) {
					std::vector<TWPartition*>::iterator subpart;

					for (subpart = Partitions.begin(); subpart != Partitions.end(); subpart++) {
						if ((*subpart)->Can_Be_Backed_Up && (*subpart)->Is_Present && (*subpart)->Is_SubPartition && (*subpart)->SubPartition_Of == backup_part->Mount_Point) {
							partition_count++;
							if ((*subpart)->Backup_Method == 1)
								file_bytes += (*subpart)->Backup_Size;
							else
								img_bytes += (*subpart)->Backup_Size;
						}
					}
				}
			} else {
				LOGERR("Unable to locate '%s' partition for backup calculations.\n", backup_path.c_str());
			}
			start_pos = end_pos + 1;
			end_pos = Backup_List.find(";", start_pos);
		}
	}

	if (partition_count == 0) {
		gui_print("No partitions selected for backup.\n");
		return false;
	}
	total_bytes = file_bytes + img_bytes;
	gui_print(" * Total number of partitions to back up: %d\n", partition_count);
    gui_print(" * Total size of all data: %lluMB\n", total_bytes / 1024 / 1024);
	storage = Find_Partition_By_Path(DataManager::GetCurrentStoragePath());
	if (storage != NULL) {
		free_space = storage->Free;
		gui_print(" * Available space: %lluMB\n", free_space / 1024 / 1024);
	} else {
		LOGERR("Unable to locate storage device.\n");
		return false;
	}
	if (free_space - (32 * 1024 * 1024) < total_bytes) {
		// We require an extra 32MB just in case
		LOGERR("Not enough free space on storage.\n");
		return false;
	}
	img_bytes_remaining = img_bytes;
    file_bytes_remaining = file_bytes;

	gui_print("\n[BACKUP STARTED]\n");
	gui_print(" * Backup Folder: %s\n", Full_Backup_Path.c_str());
	if (!TWFunc::Recursive_Mkdir(Full_Backup_Path)) {
		LOGERR("Failed to make backup folder.\n");
		return false;
	}

	DataManager::SetProgress(0.0);

	start_pos = 0;
	end_pos = Backup_List.find(";", start_pos);
	while (end_pos != string::npos && start_pos < Backup_List.size()) {
		backup_path = Backup_List.substr(start_pos, end_pos - start_pos);
		backup_part = Find_Partition_By_Path(backup_path);
		if (backup_part != NULL) {
			if (!Backup_Partition(backup_part, Full_Backup_Path, do_md5, &img_bytes_remaining, &file_bytes_remaining, &img_time, &file_time, &img_bytes, &file_bytes))
				return false;
		} else {
			LOGERR("Unable to locate '%s' partition for backup process.\n", backup_path.c_str());
		}
		start_pos = end_pos + 1;
		end_pos = Backup_List.find(";", start_pos);
	}

	// Average BPS
	if (img_time == 0)
		img_time = 1;
	if (file_time == 0)
		file_time = 1;
	int img_bps = (int)img_bytes / (int)img_time;
	unsigned long long file_bps = file_bytes / (int)file_time;

	gui_print("Average backup rate for file systems: %llu MB/sec\n", (file_bps / (1024 * 1024)));
	gui_print("Average backup rate for imaged drives: %lu MB/sec\n", (img_bps / (1024 * 1024)));

	time(&total_stop);
	int total_time = (int) difftime(total_stop, total_start);
	uint64_t actual_backup_size = du.Get_Folder_Size(Full_Backup_Path);
    actual_backup_size /= (1024LLU * 1024LLU);

	int prev_img_bps, use_compression;
	unsigned long long prev_file_bps;
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

	gui_print("[%llu MB TOTAL BACKED UP]\n", actual_backup_size);
	Update_System_Details();
	UnMount_Main_Partitions();
	gui_print("[BACKUP COMPLETED IN %d SECONDS]\n\n", total_time); // the end
	string backup_log = Full_Backup_Path + "recovery.log";
	TWFunc::copy_file("/tmp/recovery.log", backup_log, 0644);
	return true;
}

bool TWPartitionManager::Restore_Partition(TWPartition* Part, string Restore_Name, int partition_count) {
	time_t Start, Stop;
	time(&Start);
	DataManager::ShowProgress(1.0 / (float)partition_count, 150);
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
	gui_print("[%s done (%d seconds)]\n\n", Part->Backup_Display_Name.c_str(), (int)difftime(Stop, Start));
	return true;
}

int TWPartitionManager::Run_Restore(string Restore_Name) {
	int check_md5, check, partition_count = 0;
	TWPartition* restore_part = NULL;
	time_t rStart, rStop;
	time(&rStart);
	string Restore_List, restore_path;
	size_t start_pos = 0, end_pos;

	gui_print("\n[RESTORE STARTED]\n\n");
	gui_print("Restore folder: '%s'\n", Restore_Name.c_str());

	if (!Mount_Current_Storage(true))
		return false;

	DataManager::GetValue(TW_SKIP_MD5_CHECK_VAR, check_md5);
	if (check_md5 > 0) {
		// Check MD5 files first before restoring to ensure that all of them match before starting a restore
		TWFunc::GUI_Operation_Text(TW_VERIFY_MD5_TEXT, "Verifying MD5");
		gui_print("Verifying MD5...\n");
	} else {
		gui_print("Skipping MD5 check based on user setting.\n");
	}
	DataManager::GetValue("tw_restore_selected", Restore_List);
	if (!Restore_List.empty()) {
		end_pos = Restore_List.find(";", start_pos);
		while (end_pos != string::npos && start_pos < Restore_List.size()) {
			restore_path = Restore_List.substr(start_pos, end_pos - start_pos);
			restore_part = Find_Partition_By_Path(restore_path);
			if (restore_part != NULL) {
				partition_count++;
				if (check_md5 > 0 && !restore_part->Check_MD5(Restore_Name))
					return false;
				if (restore_part->Has_SubPartition) {
					std::vector<TWPartition*>::iterator subpart;

					for (subpart = Partitions.begin(); subpart != Partitions.end(); subpart++) {
						if ((*subpart)->Is_SubPartition && (*subpart)->SubPartition_Of == restore_part->Mount_Point) {
							if (!(*subpart)->Check_MD5(Restore_Name))
								return false;
						}
					}
				}
			} else {
				LOGERR("Unable to locate '%s' partition for restoring (restore list).\n", restore_path.c_str());
			}
			start_pos = end_pos + 1;
			end_pos = Restore_List.find(";", start_pos);
		}
	}

	if (partition_count == 0) {
		LOGERR("No partitions selected for restore.\n");
		return false;
	}

	gui_print("Restoring %i partitions...\n", partition_count);
	DataManager::SetProgress(0.0);
	start_pos = 0;
	if (!Restore_List.empty()) {
		end_pos = Restore_List.find(";", start_pos);
		while (end_pos != string::npos && start_pos < Restore_List.size()) {
			restore_path = Restore_List.substr(start_pos, end_pos - start_pos);
			restore_part = Find_Partition_By_Path(restore_path);
			if (restore_part != NULL) {
				partition_count++;
				if (!Restore_Partition(restore_part, Restore_Name, partition_count))
					return false;
			} else {
				LOGERR("Unable to locate '%s' partition for restoring.\n", restore_path.c_str());
			}
			start_pos = end_pos + 1;
			end_pos = Restore_List.find(";", start_pos);
		}
	}
	TWFunc::GUI_Operation_Text(TW_UPDATE_SYSTEM_DETAILS_TEXT, "Updating System Details");
	Update_System_Details();
	UnMount_Main_Partitions();
	time(&rStop);
	gui_print("[RESTORE COMPLETED IN %d SECONDS]\n\n",(int)difftime(rStop,rStart));
	return true;
}

void TWPartitionManager::Set_Restore_Files(string Restore_Name) {
	// Start with the default values
	string Restore_List;
	bool get_date = true, check_encryption = true;

	DataManager::SetValue("tw_restore_encrypted", 0);

	DIR* d;
	d = opendir(Restore_Name.c_str());
	if (d == NULL)
	{
		LOGERR("Error opening %s\n", Restore_Name.c_str());
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

		if (fstype == NULL || extn == NULL || strcmp(fstype, "log") == 0) continue;
		int extnlength = strlen(extn);
		if (extnlength != 3 && extnlength != 6) continue;
		if (extnlength >= 3 && strncmp(extn, "win", 3) != 0) continue;
		//if (extnlength == 6 && strncmp(extn, "win000", 6) != 0) continue;

		if (check_encryption) {
			string filename = Restore_Name + "/";
			filename += de->d_name;
			if (TWFunc::Get_File_Type(filename) == 2) {
				LOGINFO("'%s' is encrypted\n", filename.c_str());
				DataManager::SetValue("tw_restore_encrypted", 1);
			}
		}
		if (extnlength == 6 && strncmp(extn, "win000", 6) != 0) continue;

		TWPartition* Part = Find_Partition_By_Path(label);
		if (Part == NULL)
		{
			LOGERR(" Unable to locate partition by backup name: '%s'\n", label);
			continue;
		}

		Part->Backup_FileName = de->d_name;
		if (strlen(extn) > 3) {
			Part->Backup_FileName.resize(Part->Backup_FileName.size() - strlen(extn) + 3);
		}

		Restore_List += Part->Backup_Path + ";";
	}
	closedir(d);

	// Set the final value
	DataManager::SetValue("tw_restore_list", Restore_List);
	DataManager::SetValue("tw_restore_selected", Restore_List);
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
		LOGERR("Wipe: Unable to find partition for path '%s'\n", Local_Path.c_str());
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
	LOGERR("Wipe: Unable to find partition for block '%s'\n", Block.c_str());
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
	LOGERR("Wipe: Unable to find partition for name '%s'\n", Name.c_str());
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
	vector <string> dir;

	if (!Mount_By_Path("/data", true))
		return false;

	if (!Mount_By_Path("/cache", true))
		return false;

	dir.push_back("/data/dalvik-cache");
	dir.push_back("/cache/dalvik-cache");
	dir.push_back("/cache/dc");
	gui_print("\nWiping Dalvik Cache Directories...\n");
	for (unsigned i = 0; i < dir.size(); ++i) {
		if (stat(dir.at(i).c_str(), &st) == 0) {
			TWFunc::removeDir(dir.at(i), false);
			gui_print("Cleaned: %s...\n", dir.at(i).c_str());
		}
	}
	TWPartition* sdext = Find_Partition_By_Path("/sd-ext");
	if (sdext && sdext->Is_Present && sdext->Mount(false))
	{
		if (stat("/sd-ext/dalvik-cache", &st) == 0)
		{
			TWFunc::removeDir("/sd-ext/dalvik-cache", false);
	   	    gui_print("Cleaned: /sd-ext/dalvik-cache...\n");
		}
	}
	gui_print("-- Dalvik Cache Directories Wipe Complete!\n\n");
	return true;
}

int TWPartitionManager::Wipe_Rotate_Data(void) {
	if (!Mount_By_Path("/data", true))
		return false;

	unlink("/data/misc/akmd*");
	unlink("/data/misc/rild*");
	gui_print("Rotation data wiped.\n");
	return true;
}

int TWPartitionManager::Wipe_Battery_Stats(void) {
	struct stat st;

	if (!Mount_By_Path("/data", true))
		return false;

	if (0 != stat("/data/system/batterystats.bin", &st)) {
		gui_print("No Battery Stats Found. No Need To Wipe.\n");
	} else {
		remove("/data/system/batterystats.bin");
		gui_print("Cleared battery stats.\n");
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
		LOGERR("No android secure partitions found.\n");
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
		LOGERR("Unable to locate /data.\n");
		return false;
	}
	return false;
}

int TWPartitionManager::Wipe_Media_From_Data(void) {
	TWPartition* dat = Find_Partition_By_Path("/data");

	if (dat != NULL) {
		if (!dat->Has_Data_Media) {
			LOGERR("This device does not have /data/media\n");
			return false;
		}
		if (!dat->Mount(true))
			return false;

		gui_print("Wiping internal storage -- /data/media...\n");
		TWFunc::removeDir("/data/media", false);
		if (mkdir("/data/media", S_IRWXU | S_IRWXG | S_IWGRP | S_IXGRP) != 0)
			return -1;
		if (dat->Has_Data_Media) {
			dat->Recreate_Media_Folder();
			// Unmount and remount - slightly hackish way to ensure that the "/sdcard" folder is still mounted properly after wiping
			dat->UnMount(false);
			dat->Mount(false);
		}
		return true;
	} else {
		LOGERR("Unable to locate /data.\n");
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

	gui_print("Updating partition details...\n");
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
		} else {
			// Handle unmountable partitions in case we reset defaults
			if ((*iter)->Mount_Point == "/boot") {
				int backup_display_size = (int)((*iter)->Backup_Size / 1048576LLU);
				DataManager::SetValue(TW_BACKUP_BOOT_SIZE, backup_display_size);
				if ((*iter)->Backup_Size == 0) {
					DataManager::SetValue(TW_HAS_BOOT_PARTITION, 0);
					DataManager::SetValue(TW_BACKUP_BOOT_VAR, 0);
				} else
					DataManager::SetValue(TW_HAS_BOOT_PARTITION, 1);
			} else if ((*iter)->Mount_Point == "/recovery") {
				int backup_display_size = (int)((*iter)->Backup_Size / 1048576LLU);
				DataManager::SetValue(TW_BACKUP_RECOVERY_SIZE, backup_display_size);
				if ((*iter)->Backup_Size == 0) {
					DataManager::SetValue(TW_HAS_RECOVERY_PARTITION, 0);
					DataManager::SetValue(TW_BACKUP_RECOVERY_VAR, 0);
				} else
					DataManager::SetValue(TW_HAS_RECOVERY_PARTITION, 1);
			} else if ((*iter)->Mount_Point == "/data") {
				data_size += (int)((*iter)->Backup_Size / 1048576LLU);
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
						LOGERR("Unable to mount internal storage.\n");
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
						LOGERR("Unable to locate internal storage partition.\n");
						DataManager::SetValue(TW_STORAGE_FREE_SIZE, 0);
					}
				}
			} else {
				// No dual storage and unable to mount storage, error!
				LOGERR("Unable to mount storage.\n");
				DataManager::SetValue(TW_STORAGE_FREE_SIZE, 0);
			}
		} else {
			DataManager::SetValue(TW_STORAGE_FREE_SIZE, (int)(FreeStorage->Free / 1048576LLU));
		}
	} else {
		LOGINFO("Unable to find storage partition '%s'.\n", current_storage_path.c_str());
	}
	if (!Write_Fstab())
		LOGERR("Error creating fstab\n");
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

#ifdef CRYPTO_SD_FS_TYPE
	property_set("ro.crypto.sd_fs_type", CRYPTO_SD_FS_TYPE);
	property_set("ro.crypto.sd_fs_real_blkdev", CRYPTO_SD_REAL_BLKDEV);
	property_set("ro.crypto.sd_fs_mnt_point", EXPAND(TW_INTERNAL_STORAGE_PATH));
#endif

    property_set("rw.km_fips_status", "ready");

#endif

	// some samsung devices store "footer" on efs partition
	TWPartition *efs = Find_Partition_By_Path("/efs");
	if(efs && !efs->Is_Mounted())
		efs->Mount(false);
	else
		efs = 0;
#ifdef TW_EXTERNAL_STORAGE_PATH
#ifdef TW_INCLUDE_CRYPTO_SAMSUNG
	TWPartition* sdcard = Find_Partition_By_Path(EXPAND(TW_EXTERNAL_STORAGE_PATH));
	if (sdcard && sdcard->Mount(false)) {
		property_set("ro.crypto.external_encrypted", "1");
		property_set("ro.crypto.external_blkdev", sdcard->Actual_Block_Device.c_str());
	} else {
		property_set("ro.crypto.external_encrypted", "0");
	}
#endif
#endif

	strcpy(cPassword, Password.c_str());
	int pwret = cryptfs_check_passwd(cPassword);

	if (pwret != 0) {
		LOGERR("Failed to decrypt data.\n");
		return -1;
	}

	if(efs)
		efs->UnMount(false);

	property_get("ro.crypto.fs_crypto_blkdev", crypto_blkdev, "error");
	if (strcmp(crypto_blkdev, "error") == 0) {
		LOGERR("Error retrieving decrypted data block device.\n");
	} else {
		TWPartition* dat = Find_Partition_By_Path("/data");
		if (dat != NULL) {
			DataManager::SetValue(TW_DATA_BLK_DEVICE, dat->Primary_Block_Device);
			DataManager::SetValue(TW_IS_DECRYPTED, 1);
			dat->Is_Decrypted = true;
			dat->Decrypted_Block_Device = crypto_blkdev;
			dat->Setup_File_System(false);
			dat->Current_File_System = dat->Fstab_File_System; // Needed if we're ignoring blkid because encrypted devices start out as emmc
			gui_print("Data successfully decrypted, new block device: '%s'\n", crypto_blkdev);

#ifdef CRYPTO_SD_FS_TYPE
			char crypto_blkdev_sd[255];
			property_get("ro.crypto.sd_fs_crypto_blkdev", crypto_blkdev_sd, "error");
			if (strcmp(crypto_blkdev_sd, "error") == 0) {
				LOGERR("Error retrieving decrypted data block device.\n");
			} else if(TWPartition* emmc = Find_Partition_By_Path(EXPAND(TW_INTERNAL_STORAGE_PATH))){
				emmc->Is_Decrypted = true;
				emmc->Decrypted_Block_Device = crypto_blkdev_sd;
				emmc->Setup_File_System(false);
				gui_print("Internal SD successfully decrypted, new block device: '%s'\n", crypto_blkdev_sd);
			}
#endif //ifdef CRYPTO_SD_FS_TYPE
#ifdef TW_EXTERNAL_STORAGE_PATH
#ifdef TW_INCLUDE_CRYPTO_SAMSUNG
			char is_external_decrypted[255];
			property_get("ro.crypto.external_use_ecryptfs", is_external_decrypted, "0");
			if (strcmp(is_external_decrypted, "1") == 0) {
				sdcard->Is_Decrypted = true;
				sdcard->EcryptFS_Password = Password;
				sdcard->Decrypted_Block_Device = sdcard->Actual_Block_Device;
				string MetaEcfsFile = EXPAND(TW_EXTERNAL_STORAGE_PATH);
				MetaEcfsFile += "/.MetaEcfsFile";
				if (!TWFunc::Path_Exists(MetaEcfsFile)) {
					// External storage isn't actually encrypted so unmount and remount without ecryptfs
					sdcard->UnMount(false);
					sdcard->Mount(false);
				}
			} else {
				LOGINFO("External storage '%s' is not encrypted.\n", sdcard->Mount_Point.c_str());
				sdcard->Is_Decrypted = false;
				sdcard->Decrypted_Block_Device = "";
			}
#endif
#endif //ifdef TW_EXTERNAL_STORAGE_PATH

			// Sleep for a bit so that the device will be ready
			sleep(1);
#ifdef RECOVERY_SDCARD_ON_DATA
			if (dat->Mount(false) && TWFunc::Path_Exists("/data/media/0")) {
				dat->Storage_Path = "/data/media/0";
				dat->Symlink_Path = dat->Storage_Path;
				DataManager::SetValue("tw_storage_path", "/data/media/0");
				dat->UnMount(false);
				Output_Partition(dat);
			}
#endif
			Update_System_Details();
			UnMount_Main_Partitions();
		} else
			LOGERR("Unable to locate data partition.\n");
	}
	return 0;
#else
	LOGERR("No crypto support was compiled into this build.\n");
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
	UnMount_Main_Partitions();
	gui_print("Done.\n\n");
	return result;
}

int TWPartitionManager::Open_Lun_File(string Partition_Path, string Lun_File) {
	TWPartition* Part = Find_Partition_By_Path(Partition_Path);

	if (Part == NULL) {
		LOGERR("Unable to locate volume information for USB storage mode.");
		return false;
	}
	if (!Part->UnMount(true))
		return false;

	if (TWFunc::write_file(Lun_File, Part->Actual_Block_Device)) {
		LOGERR("Unable to write to ums lunfile '%s': (%s)\n", Lun_File.c_str(), strerror(errno));
		return false;
	}
	property_set("sys.storage.ums_enabled", "1");
	return true;
}

int TWPartitionManager::usb_storage_enable(void) {
	int has_dual, has_data_media;
	char lun_file[255];
	string ext_path;
	bool has_multiple_lun = false;

	DataManager::GetValue(TW_HAS_DUAL_STORAGE, has_dual);
	DataManager::GetValue(TW_HAS_DATA_MEDIA, has_data_media);
	if (has_dual == 1 && has_data_media == 0) {
		string Lun_File_str = CUSTOM_LUN_FILE;
		size_t found = Lun_File_str.find("%");
		if (found != string::npos) {
			sprintf(lun_file, CUSTOM_LUN_FILE, 1);
			if (TWFunc::Path_Exists(lun_file))
				has_multiple_lun = true;
		}
		if (!has_multiple_lun) {
			// Device doesn't have multiple lun files, mount current storage
			sprintf(lun_file, CUSTOM_LUN_FILE, 0);
			return Open_Lun_File(DataManager::GetCurrentStoragePath(), lun_file);
		} else {
			// Device has multiple lun files
			sprintf(lun_file, CUSTOM_LUN_FILE, 0);
			if (!Open_Lun_File(DataManager::GetSettingsStoragePath(), lun_file))
				return false;
			DataManager::GetValue(TW_EXTERNAL_PATH, ext_path);
			sprintf(lun_file, CUSTOM_LUN_FILE, 1);
			return Open_Lun_File(ext_path, lun_file);
		}
	} else {
		if (has_data_media == 0)
			ext_path = DataManager::GetCurrentStoragePath();
		else
			DataManager::GetValue(TW_EXTERNAL_PATH, ext_path);
		sprintf(lun_file, CUSTOM_LUN_FILE, 0);
		return Open_Lun_File(ext_path, lun_file);
	}
	return true;
}

int TWPartitionManager::usb_storage_disable(void) {
	int index, ret;
	char lun_file[255], ch[2] = {0, 0};
	string str = ch;

	for (index=0; index<2; index++) {
		sprintf(lun_file, CUSTOM_LUN_FILE, index);
		ret = TWFunc::write_file(lun_file, str);
		Mount_All_Storage();
		Update_System_Details();
		if (ret < 0) {
			break;
		}
	}
	Mount_All_Storage();
	Update_System_Details();
	UnMount_Main_Partitions();
	property_set("sys.storage.ums_enabled", "0");
	if (ret < 0 && index == 0) {
		LOGERR("Unable to write to ums lunfile '%s'.", lun_file);
		return false;
	} else {
		return true;
	}
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
	LOGINFO("Unmounting main partitions...\n");

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

	gui_print("Partitioning SD Card...\n");
#ifdef TW_EXTERNAL_STORAGE_PATH
	TWPartition* SDCard = Find_Partition_By_Path(EXPAND(TW_EXTERNAL_STORAGE_PATH));
#else
	TWPartition* SDCard = Find_Partition_By_Path("/sdcard");
#endif
	if (SDCard == NULL) {
		LOGERR("Unable to locate device to partition.\n");
		return false;
	}
	if (!SDCard->UnMount(true))
		return false;
	TWPartition* SDext = Find_Partition_By_Path("/sd-ext");
	if (SDext != NULL) {
		if (!SDext->UnMount(true))
			return false;
	}

	TWFunc::Exec_Cmd("umount \"$SWAPPATH\"");
	Device = SDCard->Actual_Block_Device;
	// Just use the root block device
	Device.resize(strlen("/dev/block/mmcblkX"));

	// Find the size of the block device:
	fp = fopen("/proc/partitions", "rt");
	if (fp == NULL) {
		LOGERR("Unable to open /proc/partitions\n");
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
	LOGINFO("sd card block device is '%s', sdcard size is: %iMB, fat size: %iMB, ext size: %iMB, ext system: '%s', swap size: %iMB\n", Device.c_str(), total_size, fat_size, ext, ext_format.c_str(), swap);
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
		LOGERR("EXT + Swap size is larger than sdcard size.\n");
		return false;
	}
	gui_print("Removing partition table...\n");
	Command = "parted -s " + Device + " mklabel msdos";
	LOGINFO("Command is: '%s'\n", Command.c_str());
	if (TWFunc::Exec_Cmd(Command) != 0) {
		LOGERR("Unable to remove partition table.\n");
		Update_System_Details();
		return false;
	}
	gui_print("Creating FAT32 partition...\n");
	Command = "parted " + Device + " mkpartfs primary fat32 0 " + fat_str + "MB";
	LOGINFO("Command is: '%s'\n", Command.c_str());
	if (TWFunc::Exec_Cmd(Command) != 0) {
		LOGERR("Unable to create FAT32 partition.\n");
		return false;
	}
	if (ext > 0) {
		gui_print("Creating EXT partition...\n");
		Command = "parted " + Device + " mkpartfs primary ext2 " + fat_str + "MB " + ext_str + "MB";
		LOGINFO("Command is: '%s'\n", Command.c_str());
		if (TWFunc::Exec_Cmd(Command) != 0) {
			LOGERR("Unable to create EXT partition.\n");
			Update_System_Details();
			return false;
		}
	}
	if (swap > 0) {
		gui_print("Creating swap partition...\n");
		Command = "parted " + Device + " mkpartfs primary linux-swap " + ext_str + "MB " + swap_str + "MB";
		LOGINFO("Command is: '%s'\n", Command.c_str());
		if (TWFunc::Exec_Cmd(Command) != 0) {
			LOGERR("Unable to create swap partition.\n");
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
			LOGERR("Unable to locate sd-ext partition.\n");
			return false;
		}
		Command = "mke2fs -t " + ext_format + " -m 0 " + SDext->Actual_Block_Device;
		gui_print("Formatting sd-ext as %s...\n", ext_format.c_str());
		LOGINFO("Formatting sd-ext after partitioning, command: '%s'\n", Command.c_str());
		TWFunc::Exec_Cmd(Command);
	}

	Update_System_Details();
	gui_print("Partitioning complete.\n");
	return true;
}

void TWPartitionManager::Get_Partition_List(string ListType, std::vector<PartitionList> *Partition_List) {
	std::vector<TWPartition*>::iterator iter;
	if (ListType == "mount") {
		for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
			if ((*iter)->Can_Be_Mounted && !(*iter)->Is_SubPartition) {
				struct PartitionList part;
				part.Display_Name = (*iter)->Display_Name;
				part.Mount_Point = (*iter)->Mount_Point;
				part.selected = (*iter)->Is_Mounted();
				Partition_List->push_back(part);
			}
		}
	} else if (ListType == "storage") {
		char free_space[255];
		string Current_Storage = DataManager::GetCurrentStoragePath();
		for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
			if ((*iter)->Is_Storage) {
				struct PartitionList part;
				sprintf(free_space, "%llu", (*iter)->Free / 1024 / 1024);
				part.Display_Name = (*iter)->Storage_Name + " (";
				part.Display_Name += free_space;
				part.Display_Name += "MB)";
				part.Mount_Point = (*iter)->Storage_Path;
				if ((*iter)->Storage_Path == Current_Storage)
					part.selected = 1;
				else
					part.selected = 0;
				Partition_List->push_back(part);
			}
		}
	} else if (ListType == "backup") {
		char backup_size[255];
		unsigned long long Backup_Size;
		for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
			if ((*iter)->Can_Be_Backed_Up && !(*iter)->Is_SubPartition && (*iter)->Is_Present) {
				struct PartitionList part;
				Backup_Size = (*iter)->Backup_Size;
				if ((*iter)->Has_SubPartition) {
					std::vector<TWPartition*>::iterator subpart;

					for (subpart = Partitions.begin(); subpart != Partitions.end(); subpart++) {
						if ((*subpart)->Is_SubPartition && (*subpart)->Can_Be_Backed_Up && (*subpart)->Is_Present && (*subpart)->SubPartition_Of == (*iter)->Mount_Point)
							Backup_Size += (*subpart)->Backup_Size;
					}
				}
				sprintf(backup_size, "%llu", Backup_Size / 1024 / 1024);
				part.Display_Name = (*iter)->Backup_Display_Name + " (";
				part.Display_Name += backup_size;
				part.Display_Name += "MB)";
				part.Mount_Point = (*iter)->Backup_Path;
				part.selected = 0;
				Partition_List->push_back(part);
			}
		}
	} else if (ListType == "restore") {
		string Restore_List, restore_path;
		TWPartition* restore_part = NULL;

		DataManager::GetValue("tw_restore_list", Restore_List);
		if (!Restore_List.empty()) {
			size_t start_pos = 0, end_pos = Restore_List.find(";", start_pos);
			while (end_pos != string::npos && start_pos < Restore_List.size()) {
				restore_path = Restore_List.substr(start_pos, end_pos - start_pos);
				if ((restore_part = Find_Partition_By_Path(restore_path)) != NULL) {
					if ((restore_part->Backup_Name == "recovery" && !restore_part->Can_Be_Backed_Up) || restore_part->Is_SubPartition) {
						// Don't allow restore of recovery (causes problems on some devices)
						// Don't add subpartitions to the list of items
					} else {
						struct PartitionList part;
						part.Display_Name = restore_part->Backup_Display_Name;
						part.Mount_Point = restore_part->Backup_Path;
						part.selected = 1;
						Partition_List->push_back(part);
					}
				} else {
					LOGERR("Unable to locate '%s' partition for restore.\n", restore_path.c_str());
				}
				start_pos = end_pos + 1;
				end_pos = Restore_List.find(";", start_pos);
			}
		}
	} else if (ListType == "wipe") {
		struct PartitionList dalvik;
		dalvik.Display_Name = "Dalvik Cache";
		dalvik.Mount_Point = "DALVIK";
		dalvik.selected = 0;
		Partition_List->push_back(dalvik);
		for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
			if ((*iter)->Wipe_Available_in_GUI && !(*iter)->Is_SubPartition) {
				struct PartitionList part;
				part.Display_Name = (*iter)->Display_Name;
				part.Mount_Point = (*iter)->Mount_Point;
				part.selected = 0;
				Partition_List->push_back(part);
			}
			if ((*iter)->Has_Android_Secure) {
				struct PartitionList part;
				part.Display_Name = (*iter)->Backup_Display_Name;
				part.Mount_Point = (*iter)->Backup_Path;
				part.selected = 0;
				Partition_List->push_back(part);
			}
			if ((*iter)->Has_Data_Media) {
				struct PartitionList datamedia;
				datamedia.Display_Name = (*iter)->Storage_Name;
				datamedia.Mount_Point = "INTERNAL";
				datamedia.selected = 0;
				Partition_List->push_back(datamedia);
			}
		}
	} else {
		LOGERR("Unknown list type '%s' requested for TWPartitionManager::Get_Partition_List\n", ListType.c_str());
	}
}

int TWPartitionManager::Fstab_Processed(void) {
	return Partitions.size();
}

void TWPartitionManager::Output_Storage_Fstab(void) {
	std::vector<TWPartition*>::iterator iter;
	char storage_partition[255];
	string Temp;
	FILE *fp = fopen("/cache/recovery/storage.fstab", "w");

	if (fp == NULL) {
		LOGERR("Unable to open '/cache/recovery/storage.fstab'.\n");
		return;
	}

	// Iterate through all partitions
	for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		if ((*iter)->Is_Storage) {
			Temp = (*iter)->Storage_Path + ";" + (*iter)->Storage_Name + ";\n";
			strcpy(storage_partition, Temp.c_str());
			fwrite(storage_partition, sizeof(storage_partition[0]), strlen(storage_partition) / sizeof(storage_partition[0]), fp);
		}
	}
	fclose(fp);
}
