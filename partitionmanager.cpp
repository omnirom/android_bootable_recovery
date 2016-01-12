/*
	Copyright 2014 TeamWin
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
#include <sys/wait.h>
#include <linux/fs.h>
#include <sys/mount.h>
#include "variables.h"
#include "twcommon.h"
#include "partitions.hpp"
#include "data.hpp"
#include "twrp-functions.hpp"
#include "fixPermissions.hpp"
#include "twrpDigest.hpp"
#include "twrpDU.hpp"
#include "set_metadata.h"
#include "tw_atomic.hpp"
#include "gui/gui.hpp"

#ifdef TW_HAS_MTP
#include "mtp/mtp_MtpServer.hpp"
#include "mtp/twrpMtp.hpp"
#include "mtp/MtpMessage.hpp"
#endif

extern "C" {
	#include "cutils/properties.h"
	#include "gui/gui.h"
}

#ifdef TW_INCLUDE_CRYPTO
	#include "crypto/lollipop/cryptfs.h"
#endif

extern bool datamedia;

TWPartitionManager::TWPartitionManager(void) {
	mtp_was_enabled = false;
	mtp_write_fd = -1;
	stop_backup.set_value(0);
	tar_fork_pid = 0;
}

int TWPartitionManager::Process_Fstab(string Fstab_Filename, bool Display_Error) {
	FILE *fstabFile;
	char fstab_line[MAX_FSTAB_LINE_LENGTH];
	TWPartition* settings_partition = NULL;
	TWPartition* andsec_partition = NULL;
	unsigned int storageid = 1 << 16;	// upper 16 bits are for physical storage device, we pretend to have only one

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
			if (partition->Is_Storage) {
				++storageid;
				partition->MTP_Storage_ID = storageid;
			}
			if (!settings_partition && partition->Is_Settings_Storage && partition->Is_Present) {
				settings_partition = partition;
			} else {
				partition->Is_Settings_Storage = false;
			}
			if (!andsec_partition && partition->Has_Android_Secure && partition->Is_Present) {
				andsec_partition = partition;
			} else {
				partition->Has_Android_Secure = false;
			}
			Partitions.push_back(partition);
		} else {
			delete partition;
		}
	}
	fclose(fstabFile);
	if (!datamedia && !settings_partition && Find_Partition_By_Path("/sdcard") == NULL && Find_Partition_By_Path("/internal_sd") == NULL && Find_Partition_By_Path("/internal_sdcard") == NULL && Find_Partition_By_Path("/emmc") == NULL) {
		// Attempt to automatically identify /data/media emulated storage devices
		TWPartition* Dat = Find_Partition_By_Path("/data");
		if (Dat) {
			LOGINFO("Using automatic handling for /data/media emulated storage device.\n");
			datamedia = true;
			Dat->Setup_Data_Media();
			settings_partition = Dat;
			// Since /data was not considered a storage partition earlier, we still need to assign an MTP ID
			++storageid;
			Dat->MTP_Storage_ID = storageid;
		}
	}
	if (!settings_partition) {
		std::vector<TWPartition*>::iterator iter;
		for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
			if ((*iter)->Is_Storage) {
				settings_partition = (*iter);
				break;
			}
		}
		if (!settings_partition)
			LOGERR("Unable to locate storage partition for storing settings file.\n");
	}
	if (!Write_Fstab()) {
		if (Display_Error)
			LOGERR("Error creating fstab\n");
		else
			LOGINFO("Error creating fstab\n");
	}

	if (andsec_partition) {
		Setup_Android_Secure_Location(andsec_partition);
	} else if (settings_partition) {
		Setup_Android_Secure_Location(settings_partition);
	}
	if (settings_partition) {
		Setup_Settings_Storage_Partition(settings_partition);
	}
#ifdef TW_INCLUDE_CRYPTO
	TWPartition* Decrypt_Data = Find_Partition_By_Path("/data");
	if (Decrypt_Data && Decrypt_Data->Is_Encrypted && !Decrypt_Data->Is_Decrypted) {
		int password_type = cryptfs_get_password_type();
		if (password_type == CRYPT_TYPE_DEFAULT) {
			LOGINFO("Device is encrypted with the default password, attempting to decrypt.\n");
			if (Decrypt_Device("default_password") == 0) {
				gui_msg("decrypt_success=Successfully decrypted with default password.");
				DataManager::SetValue(TW_IS_ENCRYPTED, 0);
			} else {
				gui_err("unable_to_decrypt=Unable to decrypt with default password.");
			}
		} else {
			DataManager::SetValue("TW_CRYPTO_TYPE", password_type);
		}
	}
#endif
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
			Line = (*iter)->Actual_Block_Device + " " + (*iter)->Mount_Point + " " + (*iter)->Current_File_System + " rw 0 0\n";
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
	DataManager::SetValue("tw_settings_path", Part->Storage_Path);
	DataManager::SetValue("tw_storage_path", Part->Storage_Path);
	LOGINFO("Settings storage is '%s'\n", Part->Storage_Path.c_str());
}

void TWPartitionManager::Setup_Android_Secure_Location(TWPartition* Part) {
	if (Part->Has_Android_Secure)
		Part->Setup_AndSec();
	else if (!datamedia)
		Part->Setup_AndSec();
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
	if (Part->Mount_To_Decrypt)
		printf("Mount_To_Decrypt ");
	if (Part->Can_Flash_Img)
		printf("Can_Flash_Img ");
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
	if (!Part->Crypto_Key_Location.empty() && Part->Crypto_Key_Location != "footer")
		printf("   Crypto_Key_Location: %s\n", Part->Crypto_Key_Location.c_str());
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
		printf("   Format_Block_Size: %lu\n", Part->Format_Block_Size);
	if (!Part->MTD_Name.empty())
		printf("   MTD_Name: %s\n", Part->MTD_Name.c_str());
	string back_meth = Part->Backup_Method_By_Name();
	printf("   Backup_Method: %s\n", back_meth.c_str());
	if (Part->Mount_Flags || !Part->Mount_Options.empty())
		printf("   Mount_Flags=0x%8x, Mount_Options=%s\n", Part->Mount_Flags, Part->Mount_Options.c_str());
	if (Part->MTP_Storage_ID)
		printf("   MTP_Storage_ID: %i\n", Part->MTP_Storage_ID);
	printf("\n");
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
		gui_msg(Msg(msg::kError, "unable_find_part_path=Unable to find partition for path '{1}'")(Local_Path));
	} else {
		LOGINFO("Mount: Unable to find partition for path '%s'\n", Local_Path.c_str());
	}
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
		gui_msg(Msg(msg::kError, "unable_find_part_path=Unable to find partition for path '{1}'")(Local_Path));
	} else {
		LOGINFO("UnMount: Unable to find partition for path '%s'\n", Local_Path.c_str());
	}
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
			gui_err("backup_name_len=Backup name is too long.");
		return -2;
	}

	// Check each character
	strncpy(backup_name, Backup_Name.c_str(), copy_size);
	if (copy_size == 1 && strncmp(backup_name, "0", 1) == 0)
		return 0; // A "0" (zero) means to use the current timestamp for the backup name
	for (index=0; index<copy_size; index++) {
		cur_char = (int)backup_name[index];
		if (cur_char == 32 || (cur_char >= 48 && cur_char <= 57) || (cur_char >= 65 && cur_char <= 91) || cur_char == 93 || cur_char == 95 || (cur_char >= 97 && cur_char <= 123) || cur_char == 125 || cur_char == 45 || cur_char == 46) {
			// These are valid characters
			// Numbers
			// Upper case letters
			// Lower case letters
			// Space
			// and -_.{}[]
		} else {
			if (Display_Error)
				gui_msg(Msg(msg::kError, "backup_name_invalid=Backup name '{1}' contains invalid character: '{1}'")(backup_name)((char)cur_char));
			return -3;
		}
	}

	// Check to make sure that a backup with this name doesn't already exist
	DataManager::GetValue(TW_BACKUPS_FOLDER_VAR, Backup_Loc);
	strcpy(backup_loc, Backup_Loc.c_str());
	sprintf(tw_image_dir,"%s/%s", backup_loc, Backup_Name.c_str());
	if (TWFunc::Path_Exists(tw_image_dir)) {
		if (Display_Error)
			gui_err("backup_name_exists=A backup with this name already exists.");
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

	TWFunc::GUI_Operation_Text(TW_GENERATE_MD5_TEXT, gui_parse_text("{@generating_md51}"));
	gui_msg("generating_md52= * Generating md5...");

	if (TWFunc::Path_Exists(Full_File)) {
		md5sum.setfn(Backup_Folder + Backup_Filename);
		if (md5sum.computeMD5() == 0)
			if (md5sum.write_md5digest() == 0)
				gui_msg("md5_created= * MD5 Created.");
			else
				return -1;
		else
			gui_err("md5_error= * MD5 Error!");
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
						gui_err("md5_error= * MD5 Error!");
						return false;
					}
				} else {
					gui_err("md5_compute_error= * Error computing MD5.");
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
		gui_msg("md5_created= * MD5 Created.");
	}
	return true;
}

bool TWPartitionManager::Backup_Partition(TWPartition* Part, string Backup_Folder, bool generate_md5, unsigned long long* img_bytes_remaining, unsigned long long* file_bytes_remaining, unsigned long *img_time, unsigned long *file_time, unsigned long long *img_bytes, unsigned long long *file_bytes) {
	time_t start, stop;
	int use_compression;
	float pos;
	unsigned long long total_size, current_size;

	string backup_log = Backup_Folder + "recovery.log";

	if (Part == NULL)
		return true;

	DataManager::GetValue(TW_USE_COMPRESSION_VAR, use_compression);

	total_size = *file_bytes + *img_bytes;
	current_size = *file_bytes + *img_bytes - *file_bytes_remaining - *img_bytes_remaining;
	// Set the position
	pos = ((float)(current_size) / (float)(total_size));
	DataManager::SetProgress(pos);

	TWFunc::SetPerformanceMode(true);
	time(&start);

	if (Part->Backup(Backup_Folder, &total_size, &current_size, tar_fork_pid)) {
		bool md5Success = false;
		current_size += Part->Backup_Size;
		pos = (float)((float)(current_size) / (float)(total_size));
		DataManager::SetProgress(pos);
		if (Part->Has_SubPartition) {
			std::vector<TWPartition*>::iterator subpart;

			for (subpart = Partitions.begin(); subpart != Partitions.end(); subpart++) {
				if ((*subpart)->Can_Be_Backed_Up && (*subpart)->Is_SubPartition && (*subpart)->SubPartition_Of == Part->Mount_Point) {
					if (!(*subpart)->Backup(Backup_Folder, &total_size, &current_size, tar_fork_pid)) {
						TWFunc::SetPerformanceMode(false);
						Clean_Backup_Folder(Backup_Folder);
						TWFunc::copy_file("/tmp/recovery.log", backup_log, 0644);
						tw_set_default_metadata(backup_log.c_str());
						return false;
					}
					sync();
					sync();
					if (!Make_MD5(generate_md5, Backup_Folder, (*subpart)->Backup_FileName)) {
						TWFunc::SetPerformanceMode(false);
						return false;
					}
					if (Part->Backup_Method == 1) {
						*file_bytes_remaining -= (*subpart)->Backup_Size;
					} else {
						*img_bytes_remaining -= (*subpart)->Backup_Size;
					}
					current_size += Part->Backup_Size;
					pos = (float)(current_size / total_size);
					DataManager::SetProgress(pos);
				}
			}
		}
		time(&stop);
		int backup_time = (int) difftime(stop, start);
		LOGINFO("Partition Backup time: %d\n", backup_time);
		if (Part->Backup_Method == 1) {
			*file_bytes_remaining -= Part->Backup_Size;
			*file_time += backup_time;
		} else {
			*img_bytes_remaining -= Part->Backup_Size;
			*img_time += backup_time;
		}

		md5Success = Make_MD5(generate_md5, Backup_Folder, Part->Backup_FileName);
		TWFunc::SetPerformanceMode(false);
		return md5Success;
	} else {
		Clean_Backup_Folder(Backup_Folder);
		TWFunc::copy_file("/tmp/recovery.log", backup_log, 0644);
		tw_set_default_metadata(backup_log.c_str());
		TWFunc::SetPerformanceMode(false);
		return false;
	}
	return 0;
}

void TWPartitionManager::Clean_Backup_Folder(string Backup_Folder) {
	DIR *d = opendir(Backup_Folder.c_str());
	struct dirent *p;
	int r;

	gui_msg("backup_clean=Backup Failed. Cleaning Backup Folder.");

	if (d == NULL) {
		gui_msg(Msg(msg::kError, "error_opening_strerr=Error opening: '{1}' ({2})")(Backup_Folder)(strerror(errno)));
		return;
	}

	while ((p = readdir(d))) {
		if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, ".."))
			continue;

		string path = Backup_Folder + p->d_name;

		size_t dot = path.find_last_of(".") + 1;
		if (path.substr(dot) == "win" || path.substr(dot) == "md5" || path.substr(dot) == "info") {
			r = unlink(path.c_str());
			if (r != 0) {
				LOGINFO("Unable to unlink '%s: %s'\n", path.c_str(), strerror(errno));
			}
		}
	}
	closedir(d);
}

int TWPartitionManager::Cancel_Backup() {
	string Backup_Folder, Backup_Name, Full_Backup_Path;

	stop_backup.set_value(1);

	if (tar_fork_pid != 0) {
		DataManager::GetValue(TW_BACKUP_NAME, Backup_Name);
		DataManager::GetValue(TW_BACKUPS_FOLDER_VAR, Backup_Folder);
		Full_Backup_Path = Backup_Folder + "/" + Backup_Name + "/";
		LOGINFO("Killing pid: %d\n", tar_fork_pid);
		kill(tar_fork_pid, SIGUSR2);
		while (kill(tar_fork_pid, 0) == 0) {
			usleep(1000);
		}
		LOGINFO("Backup_Run stopped and returning false, backup cancelled.\n");
		LOGINFO("Removing directory %s\n", Full_Backup_Path.c_str());
		TWFunc::removeDir(Full_Backup_Path, false);
		tar_fork_pid = 0;
	}

	return 0;
}

int TWPartitionManager::Run_Backup(void) {
	int check, do_md5, partition_count = 0, disable_free_space_check = 0;
	string Backup_Folder, Backup_Name, Full_Backup_Path, Backup_List, backup_path;
	unsigned long long total_bytes = 0, file_bytes = 0, img_bytes = 0, free_space = 0, img_bytes_remaining, file_bytes_remaining, subpart_size;
	unsigned long img_time = 0, file_time = 0;
	TWPartition* backup_part = NULL;
	TWPartition* storage = NULL;
	std::vector<TWPartition*>::iterator subpart;
	struct tm *t;
	time_t start, stop, seconds, total_start, total_stop;
	size_t start_pos = 0, end_pos = 0;
	stop_backup.set_value(0);
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
	if (Backup_Name == gui_parse_text("{@current_date}")) {
		Backup_Name = TWFunc::Get_Current_Date();
	} else if (Backup_Name == gui_parse_text("{@auto_generate}") || Backup_Name == "0" || Backup_Name.empty()) {
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
				gui_msg(Msg(msg::kError, "unable_to_locate_partition=Unable to locate '{1}' partition for backup calculations.")(backup_path));
			}
			start_pos = end_pos + 1;
			end_pos = Backup_List.find(";", start_pos);
		}
	}

	if (partition_count == 0) {
		gui_msg("no_partition_selected=No partitions selected for backup.");
		return false;
	}
	total_bytes = file_bytes + img_bytes;
	gui_msg(Msg("total_partitions_backup= * Total number of partitions to back up: {1}")(partition_count));
	gui_msg(Msg("total_backup_size= * Total size of all data: {1}MB")(total_bytes / 1024 / 1024));
	storage = Find_Partition_By_Path(DataManager::GetCurrentStoragePath());
	if (storage != NULL) {
		free_space = storage->Free;
		gui_msg(Msg("available_space= * Available space: {1}MB")(free_space / 1024 / 1024));
	} else {
		gui_err("unable_locate_storage=Unable to locate storage device.");
		return false;
	}

	DataManager::GetValue("tw_disable_free_space", disable_free_space_check);
	if (!disable_free_space_check) {
		if (free_space - (32 * 1024 * 1024) < total_bytes) {
			// We require an extra 32MB just in case
			gui_err("no_space=Not enough free space on storage.");
			return false;
		}
	}
	img_bytes_remaining = img_bytes;
	file_bytes_remaining = file_bytes;

	gui_msg("backup_started=[BACKUP STARTED]");
	gui_msg(Msg("backup_folder= * Backup Folder: {1}")(Full_Backup_Path));
	if (!TWFunc::Recursive_Mkdir(Full_Backup_Path)) {
		gui_err("fail_backup_folder=Failed to make backup folder.");
		return false;
	}

	DataManager::SetProgress(0.0);

	start_pos = 0;
	end_pos = Backup_List.find(";", start_pos);
	while (end_pos != string::npos && start_pos < Backup_List.size()) {
		if (stop_backup.get_value() != 0)
			return -1;
		backup_path = Backup_List.substr(start_pos, end_pos - start_pos);
		backup_part = Find_Partition_By_Path(backup_path);
		if (backup_part != NULL) {
			if (!Backup_Partition(backup_part, Full_Backup_Path, do_md5, &img_bytes_remaining, &file_bytes_remaining, &img_time, &file_time, &img_bytes, &file_bytes))
				return false;
		} else {
			gui_msg(Msg(msg::kError, "unable_to_locate_partition=Unable to locate '{1}' partition for backup calculations.")(backup_path));
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

	gui_msg(Msg("avg_backup_fs=Average backup rate for file systems: {1} MB/sec")(file_bps / (1024 * 1024)));
	gui_msg(Msg("avg_backup_img=Average backup rate for imaged drives: {1} MB/sec")(img_bps / (1024 * 1024)));

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

	gui_msg(Msg("total_backed_size=[{1} MB TOTAL BACKED UP]")(actual_backup_size));
	Update_System_Details();
	UnMount_Main_Partitions();
	gui_msg(Msg(msg::kHighlight, "backup_completed=[BACKUP COMPLETED IN {1} SECONDS]")(total_time)); // the end
	string backup_log = Full_Backup_Path + "recovery.log";
	TWFunc::copy_file("/tmp/recovery.log", backup_log, 0644);
	tw_set_default_metadata(backup_log.c_str());
	return true;
}

bool TWPartitionManager::Restore_Partition(TWPartition* Part, string Restore_Name, int partition_count, const unsigned long long *total_restore_size, unsigned long long *already_restored_size) {
	time_t Start, Stop;
	TWFunc::SetPerformanceMode(true);
	time(&Start);
	//DataManager::ShowProgress(1.0 / (float)partition_count, 150);
	if (!Part->Restore(Restore_Name, total_restore_size, already_restored_size)) {
		TWFunc::SetPerformanceMode(false);
		return false;
	}
	if (Part->Has_SubPartition) {
		std::vector<TWPartition*>::iterator subpart;

		for (subpart = Partitions.begin(); subpart != Partitions.end(); subpart++) {
			if ((*subpart)->Is_SubPartition && (*subpart)->SubPartition_Of == Part->Mount_Point) {
				if (!(*subpart)->Restore(Restore_Name, total_restore_size, already_restored_size)) {
					TWFunc::SetPerformanceMode(false);
					return false;
				}
			}
		}
	}
	time(&Stop);
	TWFunc::SetPerformanceMode(false);
	gui_msg(Msg("restort_part_done=[{1} done ({2} seconds)]")(Part->Backup_Display_Name)((int)difftime(Stop, Start)));
	return true;
}

int TWPartitionManager::Run_Restore(string Restore_Name) {
	int check_md5, check, partition_count = 0;
	TWPartition* restore_part = NULL;
	time_t rStart, rStop;
	time(&rStart);
	string Restore_List, restore_path;
	size_t start_pos = 0, end_pos;
	unsigned long long total_restore_size = 0, already_restored_size = 0;

	gui_msg("restore_started=[RESTORE STARTED]");
	gui_msg(Msg("restore_folder=Restore folder: '{1}'")(Restore_Name));

	if (!Mount_Current_Storage(true))
		return false;

	DataManager::GetValue(TW_SKIP_MD5_CHECK_VAR, check_md5);
	if (check_md5 > 0) {
		// Check MD5 files first before restoring to ensure that all of them match before starting a restore
		TWFunc::GUI_Operation_Text(TW_VERIFY_MD5_TEXT, gui_parse_text("{@verifying_md5}"));
		gui_msg("verifying_md5=Verifying MD5");
	} else {
		gui_msg("skip_md5=Skipping MD5 check based on user setting.");
	}
	gui_msg("calc_restore=Calculating restore details...");
	DataManager::GetValue("tw_restore_selected", Restore_List);
	if (!Restore_List.empty()) {
		end_pos = Restore_List.find(";", start_pos);
		while (end_pos != string::npos && start_pos < Restore_List.size()) {
			restore_path = Restore_List.substr(start_pos, end_pos - start_pos);
			restore_part = Find_Partition_By_Path(restore_path);
			if (restore_part != NULL) {
				if (restore_part->Mount_Read_Only) {
					gui_msg(Msg(msg::kError, "restore_read_only=Cannot restore {1} -- mounted read only.")(restore_part->Backup_Display_Name));
					return false;
				}
				if (check_md5 > 0 && !restore_part->Check_MD5(Restore_Name))
					return false;
				partition_count++;
				total_restore_size += restore_part->Get_Restore_Size(Restore_Name);
				if (restore_part->Has_SubPartition) {
					std::vector<TWPartition*>::iterator subpart;

					for (subpart = Partitions.begin(); subpart != Partitions.end(); subpart++) {
						if ((*subpart)->Is_SubPartition && (*subpart)->SubPartition_Of == restore_part->Mount_Point) {
							if (check_md5 > 0 && !(*subpart)->Check_MD5(Restore_Name))
								return false;
							total_restore_size += (*subpart)->Get_Restore_Size(Restore_Name);
						}
					}
				}
			} else {
				gui_msg(Msg(msg::kError, "restore_unable_locate=Unable to locate '{1}' partition for restoring.")(restore_path));
			}
			start_pos = end_pos + 1;
			end_pos = Restore_List.find(";", start_pos);
		}
	}

	if (partition_count == 0) {
		gui_err("no_part_restore=No partitions selected for restore.");
		return false;
	}

	gui_msg(Msg("restore_part_count=Restoring {1} partitions...")(partition_count));
	gui_msg(Msg("total_restore_size=Total restore size is {1}MB")(total_restore_size / 1048576));
	DataManager::SetProgress(0.0);

	start_pos = 0;
	if (!Restore_List.empty()) {
		end_pos = Restore_List.find(";", start_pos);
		while (end_pos != string::npos && start_pos < Restore_List.size()) {
			restore_path = Restore_List.substr(start_pos, end_pos - start_pos);
			restore_part = Find_Partition_By_Path(restore_path);
			if (restore_part != NULL) {
				partition_count++;
				if (!Restore_Partition(restore_part, Restore_Name, partition_count, &total_restore_size, &already_restored_size))
					return false;
			} else {
				gui_msg(Msg(msg::kError, "restore_unable_locate=Unable to locate '{1}' partition for restoring.")(restore_path));
			}
			start_pos = end_pos + 1;
			end_pos = Restore_List.find(";", start_pos);
		}
	}
	TWFunc::GUI_Operation_Text(TW_UPDATE_SYSTEM_DETAILS_TEXT, gui_parse_text("{@updating_system_details}"));
	Update_System_Details();
	UnMount_Main_Partitions();
	time(&rStop);
	gui_msg(Msg(msg::kHighlight, "restore_complete=[RESTORE COMPLETED IN {1} SECONDS]")((int)difftime(rStop,rStart)));
	DataManager::SetValue("tw_file_progress", "");
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
		gui_msg(Msg(msg::kError, "error_opening_strerr=Error opening: '{1}' ({2})")(Restore_Name)(strerror(errno)));
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
			gui_msg(Msg(msg::kError, "unable_locate_part_backup_name=Unable to locate partition by backup name: '{1}'")(label));
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
		gui_msg(Msg(msg::kError, "unable_find_part_path=Unable to find partition for path '{1}'")(Local_Path));
	return false;
}

int TWPartitionManager::Wipe_By_Path(string Path, string New_File_System) {
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
				ret = (*iter)->Wipe(New_File_System);
			found = true;
		} else if ((*iter)->Is_SubPartition && (*iter)->SubPartition_Of == Local_Path) {
			(*iter)->Wipe(New_File_System);
		}
	}
	if (found) {
		return ret;
	} else
		gui_msg(Msg(msg::kError, "unable_find_part_path=Unable to find partition for path '{1}'")(Local_Path));
	return false;
}

int TWPartitionManager::Factory_Reset(void) {
	std::vector<TWPartition*>::iterator iter;
	int ret = true;

	for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		if ((*iter)->Wipe_During_Factory_Reset && (*iter)->Is_Present) {
#ifdef TW_OEM_BUILD
			if ((*iter)->Mount_Point == "/data") {
				if (!(*iter)->Wipe_Encryption())
					ret = false;
			} else {
#endif
				if (!(*iter)->Wipe())
					ret = false;
#ifdef TW_OEM_BUILD
			}
#endif
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
	gui_msg("wiping_dalvik=Wiping Dalvik Cache Directories...");
	for (unsigned i = 0; i < dir.size(); ++i) {
		if (stat(dir.at(i).c_str(), &st) == 0) {
			TWFunc::removeDir(dir.at(i), false);
			gui_msg(Msg("cleaned=Cleaned: {1}...")(dir.at(i)));
		}
	}
	TWPartition* sdext = Find_Partition_By_Path("/sd-ext");
	if (sdext && sdext->Is_Present && sdext->Mount(false))
	{
		if (stat("/sd-ext/dalvik-cache", &st) == 0)
		{
			TWFunc::removeDir("/sd-ext/dalvik-cache", false);
			gui_msg(Msg("cleaned=Cleaned: {1}...")("/sd-ext/dalvik-cache"));
		}
	}
	gui_msg("dalvik_done=-- Dalvik Cache Directories Wipe Complete!");
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
		gui_err("no_andsec=No android secure partitions found.");
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
		gui_msg(Msg(msg::kError, "unable_to_locate=Unable to locate {1].")("/data"));
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

		gui_msg("wiping_datamedia=Wiping internal storage -- /data/media...");
		Remove_MTP_Storage(dat->MTP_Storage_ID);
		TWFunc::removeDir("/data/media", false);
		dat->Recreate_Media_Folder();
		Add_MTP_Storage(dat->MTP_Storage_ID);
		return true;
	} else {
		gui_msg(Msg(msg::kError, "unable_to_locate=Unable to locate {1].")("/data"));
		return false;
	}
	return false;
}

int TWPartitionManager::Repair_By_Path(string Path, bool Display_Error) {
	std::vector<TWPartition*>::iterator iter;
	int ret = false;
	bool found = false;
	string Local_Path = TWFunc::Get_Root_Path(Path);

	if (Local_Path == "/tmp" || Local_Path == "/")
		return true;

	// Iterate through all partitions
	for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		if ((*iter)->Mount_Point == Local_Path || (!(*iter)->Symlink_Mount_Point.empty() && (*iter)->Symlink_Mount_Point == Local_Path)) {
			ret = (*iter)->Repair();
			found = true;
		} else if ((*iter)->Is_SubPartition && (*iter)->SubPartition_Of == Local_Path) {
			(*iter)->Repair();
		}
	}
	if (found) {
		return ret;
	} else if (Display_Error) {
		gui_msg(Msg(msg::kError, "unable_find_part_path=Unable to find partition for path '{1}'")(Local_Path));
	} else {
		LOGINFO("Repair: Unable to find partition for path '%s'\n", Local_Path.c_str());
	}
	return false;
}

int TWPartitionManager::Resize_By_Path(string Path, bool Display_Error) {
	std::vector<TWPartition*>::iterator iter;
	int ret = false;
	bool found = false;
	string Local_Path = TWFunc::Get_Root_Path(Path);

	if (Local_Path == "/tmp" || Local_Path == "/")
		return true;

	// Iterate through all partitions
	for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		if ((*iter)->Mount_Point == Local_Path || (!(*iter)->Symlink_Mount_Point.empty() && (*iter)->Symlink_Mount_Point == Local_Path)) {
			ret = (*iter)->Resize();
			found = true;
		} else if ((*iter)->Is_SubPartition && (*iter)->SubPartition_Of == Local_Path) {
			(*iter)->Resize();
		}
	}
	if (found) {
		return ret;
	} else if (Display_Error) {
		gui_msg(Msg(msg::kError, "unable_find_part_path=Unable to find partition for path '{1}'")(Local_Path));
	} else {
		LOGINFO("Resize: Unable to find partition for path '%s'\n", Local_Path.c_str());
	}
	return false;
}

void TWPartitionManager::Update_System_Details(void) {
	std::vector<TWPartition*>::iterator iter;
	int data_size = 0;

	gui_msg("update_part_details=Updating partition details...");
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
	gui_msg("update_part_details_done=...done");
	DataManager::SetValue(TW_BACKUP_DATA_SIZE, data_size);
	string current_storage_path = DataManager::GetCurrentStoragePath();
	TWPartition* FreeStorage = Find_Partition_By_Path(current_storage_path);
	if (FreeStorage != NULL) {
		// Attempt to mount storage
		if (!FreeStorage->Mount(false)) {
			gui_msg(Msg(msg::kError, "unable_to_mount_storage=Unable to mount storage"));
			DataManager::SetValue(TW_STORAGE_FREE_SIZE, 0);
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
	char crypto_state[PROPERTY_VALUE_MAX], crypto_blkdev[PROPERTY_VALUE_MAX], cPassword[255];
	size_t result;
	std::vector<TWPartition*>::iterator iter;

	// Mount any partitions that need to be mounted for decrypt
	for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		if ((*iter)->Mount_To_Decrypt) {
			(*iter)->Mount(true);
		}
	}

	property_get("ro.crypto.state", crypto_state, "error");
	if (strcmp(crypto_state, "error") == 0) {
		property_set("ro.crypto.state", "encrypted");
		// Sleep for a bit so that services can start if needed
		sleep(1);
	}

	strcpy(cPassword, Password.c_str());
	int pwret = cryptfs_check_passwd(cPassword);

	// Unmount any partitions that were needed for decrypt
	for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		if ((*iter)->Mount_To_Decrypt) {
			(*iter)->UnMount(false);
		}
	}

	if (pwret != 0) {
		gui_err("fail_decrypt=Failed to decrypt data.");
		return -1;
	}

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
			gui_msg(Msg("decrypt_success=Data successfully decrypted, new block device: '{1}'")(crypto_blkdev));

			// Sleep for a bit so that the device will be ready
			sleep(1);
			if (dat->Has_Data_Media && dat->Mount(false) && TWFunc::Path_Exists("/data/media/0")) {
				dat->Storage_Path = "/data/media/0";
				dat->Symlink_Path = dat->Storage_Path;
				DataManager::SetValue("tw_storage_path", "/data/media/0");
				DataManager::SetValue("tw_settings_path", "/data/media/0");
				dat->UnMount(false);
				Output_Partition(dat);
			}
			Update_System_Details();
			UnMount_Main_Partitions();
		} else
			LOGERR("Unable to locate data partition.\n");
	}
	return 0;
#else
	gui_err("no_crypto_support=No crypto support was compiled into this build.");
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
#ifdef HAVE_SELINUX
	if (result == 0 && DataManager::GetIntValue("tw_fixperms_restorecon") == 1)
		result = perms.fixContexts();
#endif
	UnMount_Main_Partitions();
	gui_msg("done=Done.");
	return result;
}

TWPartition* TWPartitionManager::Find_Next_Storage(string Path, string Exclude) {
	std::vector<TWPartition*>::iterator iter = Partitions.begin();

	if (!Path.empty()) {
		string Search_Path = TWFunc::Get_Root_Path(Path);
		for (; iter != Partitions.end(); iter++) {
			if ((*iter)->Mount_Point == Search_Path) {
				iter++;
				break;
			}
		}
	}

	for (; iter != Partitions.end(); iter++) {
		if ((*iter)->Is_Storage && (*iter)->Is_Present && (*iter)->Mount_Point != Exclude) {
			return (*iter);
		}
	}

	return NULL;
}

int TWPartitionManager::Open_Lun_File(string Partition_Path, string Lun_File) {
	TWPartition* Part = Find_Partition_By_Path(Partition_Path);

	if (Part == NULL) {
		LOGINFO("Unable to locate '%s' for USB storage mode.", Partition_Path.c_str());
		gui_msg(Msg(msg::kError, "unable_find_part_path=Unable to find partition for path '{1}'")(Partition_Path));
		return false;
	}
	LOGINFO("USB mount '%s', '%s' > '%s'\n", Partition_Path.c_str(), Part->Actual_Block_Device.c_str(), Lun_File.c_str());
	if (!Part->UnMount(true) || !Part->Is_Present)
		return false;

	if (TWFunc::write_file(Lun_File, Part->Actual_Block_Device)) {
		LOGERR("Unable to write to ums lunfile '%s': (%s)\n", Lun_File.c_str(), strerror(errno));
		return false;
	}
	return true;
}

int TWPartitionManager::usb_storage_enable(void) {
	int has_dual, has_data_media;
	char lun_file[255];
	bool has_multiple_lun = false;

	DataManager::GetValue(TW_HAS_DATA_MEDIA, has_data_media);
	string Lun_File_str = CUSTOM_LUN_FILE;
	size_t found = Lun_File_str.find("%");
	if (found != string::npos) {
		sprintf(lun_file, CUSTOM_LUN_FILE, 1);
		if (TWFunc::Path_Exists(lun_file))
			has_multiple_lun = true;
	}
	mtp_was_enabled = TWFunc::Toggle_MTP(false); // Must disable MTP for USB Storage
	if (!has_multiple_lun) {
		LOGINFO("Device doesn't have multiple lun files, mount current storage\n");
		sprintf(lun_file, CUSTOM_LUN_FILE, 0);
		if (TWFunc::Get_Root_Path(DataManager::GetCurrentStoragePath()) == "/data") {
			TWPartition* Mount = Find_Next_Storage("", "/data");
			if (Mount) {
				if (!Open_Lun_File(Mount->Mount_Point, lun_file)) {
					goto error_handle;
				}
			} else {
				gui_err("unable_locate_storage=Unable to locate storage device.");
				goto error_handle;
			}
		} else if (!Open_Lun_File(DataManager::GetCurrentStoragePath(), lun_file)) {
			goto error_handle;
		}
	} else {
		LOGINFO("Device has multiple lun files\n");
		TWPartition* Mount1;
		TWPartition* Mount2;
		sprintf(lun_file, CUSTOM_LUN_FILE, 0);
		Mount1 = Find_Next_Storage("", "/data");
		if (Mount1) {
			if (!Open_Lun_File(Mount1->Mount_Point, lun_file)) {
				goto error_handle;
			}
			sprintf(lun_file, CUSTOM_LUN_FILE, 1);
			Mount2 = Find_Next_Storage(Mount1->Mount_Point, "/data");
			if (Mount2) {
				Open_Lun_File(Mount2->Mount_Point, lun_file);
			}
		} else {
			gui_err("unable_locate_storage=Unable to locate storage device.");
			goto error_handle;
		}
	}
	property_set("sys.storage.ums_enabled", "1");
	property_set("sys.usb.config", "mass_storage,adb");
	return true;
error_handle:
	if (mtp_was_enabled)
		if (!Enable_MTP())
			Disable_MTP();
	return false;
}

int TWPartitionManager::usb_storage_disable(void) {
	int index, ret;
	char lun_file[255], ch[2] = {0, 0};
	string str = ch;

	for (index=0; index<2; index++) {
		sprintf(lun_file, CUSTOM_LUN_FILE, index);
		ret = TWFunc::write_file(lun_file, str);
		if (ret < 0) {
			break;
		}
	}
	Mount_All_Storage();
	Update_System_Details();
	UnMount_Main_Partitions();
	property_set("sys.storage.ums_enabled", "0");
	property_set("sys.usb.config", "adb");
	if (mtp_was_enabled)
		if (!Enable_MTP())
			Disable_MTP();
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
	if (!datamedia)
		UnMount_By_Path("/data", true);

	if (Boot_Partition != NULL && Boot_Partition->Can_Be_Mounted)
		Boot_Partition->UnMount(true);
}

int TWPartitionManager::Partition_SDCard(void) {
	char mkdir_path[255], temp[255], line[512];
	string Storage_Path, Command, Device, fat_str, ext_str, start_loc, end_loc, ext_format, sd_path, tmpdevice;
	int ext, swap, total_size = 0, fat_size;

	gui_msg("start_partition_sd=Partitioning SD Card...");

	// Locate and validate device to partition
	TWPartition* SDCard = Find_Partition_By_Path(DataManager::GetCurrentStoragePath());

	if (SDCard == NULL || !SDCard->Removable || SDCard->Has_Data_Media) {
		gui_err("partition_sd_locate=Unable to locate device to partition.");
		return false;
	}

	// Unmount everything
	if (!SDCard->UnMount(true))
		return false;
	TWPartition* SDext = Find_Partition_By_Path("/sd-ext");
	if (SDext != NULL) {
		if (!SDext->UnMount(true))
			return false;
	}
	char* swappath = getenv("SWAPPATH");
	if (swappath != NULL) {
		LOGINFO("Unmounting swap at '%s'\n", swappath);
		umount(swappath);
	}

	// Determine block device
	if (SDCard->Alternate_Block_Device.empty()) {
		SDCard->Find_Actual_Block_Device();
		Device = SDCard->Actual_Block_Device;
		// Just use the root block device
		Device.resize(strlen("/dev/block/mmcblkX"));
	} else {
		Device = SDCard->Alternate_Block_Device;
	}

	// Find the size of the block device:
	total_size = (int)(TWFunc::IOCTL_Get_Block_Size(Device.c_str()) / (1048576));

	DataManager::GetValue("tw_sdext_size", ext);
	DataManager::GetValue("tw_swap_size", swap);
	DataManager::GetValue("tw_sdpart_file_system", ext_format);
	fat_size = total_size - ext - swap;
	LOGINFO("sd card mount point %s block device is '%s', sdcard size is: %iMB, fat size: %iMB, ext size: %iMB, ext system: '%s', swap size: %iMB\n", DataManager::GetCurrentStoragePath().c_str(), Device.c_str(), total_size, fat_size, ext, ext_format.c_str(), swap);

	// Determine partition sizes
	if (swap == 0 && ext == 0) {
		fat_str = "-0";
	} else {
		memset(temp, 0, sizeof(temp));
		sprintf(temp, "%i", fat_size);
		fat_str = temp;
		fat_str += "MB";
	}
	if (swap == 0) {
		ext_str = "-0";
	} else {
		memset(temp, 0, sizeof(temp));
		sprintf(temp, "%i", ext);
		ext_str = "+";
		ext_str += temp;
		ext_str += "MB";
	}

	if (ext + swap > total_size) {
		gui_err("ext_swap_size=EXT + Swap size is larger than sdcard size.");
		return false;
	}

	gui_msg("remove_part_table=Removing partition table...");
	Command = "sgdisk --zap-all " + Device;
	LOGINFO("Command is: '%s'\n", Command.c_str());
	if (TWFunc::Exec_Cmd(Command) != 0) {
		gui_err("unable_rm_part=Unable to remove partition table.");
		Update_System_Details();
		return false;
	}
	gui_msg(Msg("create_part=Creating {1} partition...")("FAT32"));
	Command = "sgdisk  --new=0:0:" + fat_str + " --change-name=0:\"Microsoft basic data\" " + Device;
	LOGINFO("Command is: '%s'\n", Command.c_str());
	if (TWFunc::Exec_Cmd(Command) != 0) {
		gui_msg(Msg(msg::kError, "unable_to_create_part=Unable to create {1} partition.")("FAT32"));
		return false;
	}
	if (ext > 0) {
		gui_msg(Msg("create_part=Creating {1} partition...")("EXT"));
		Command = "sgdisk --new=0:0:" + ext_str + " --change-name=0:\"Linux filesystem\" " + Device;
		LOGINFO("Command is: '%s'\n", Command.c_str());
		if (TWFunc::Exec_Cmd(Command) != 0) {
			gui_msg(Msg(msg::kError, "unable_to_create_part=Unable to create {1} partition.")("EXT"));
			Update_System_Details();
			return false;
		}
	}
	if (swap > 0) {
		gui_msg(Msg("create_part=Creating {1} partition...")("swap"));
		Command = "sgdisk --new=0:0:-0 --change-name=0:\"Linux swap\" --typecode=0:0657FD6D-A4AB-43C4-84E5-0933C84B4F4F " + Device;
		LOGINFO("Command is: '%s'\n", Command.c_str());
		if (TWFunc::Exec_Cmd(Command) != 0) {
			gui_msg(Msg(msg::kError, "unable_to_create_part=Unable to create {1} partition.")("swap"));
			Update_System_Details();
			return false;
		}
	}

	// Convert GPT to MBR
	Command = "sgdisk --gpttombr " + Device;
	if (TWFunc::Exec_Cmd(Command) != 0)
		LOGINFO("Failed to covert partition GPT to MBR\n");

	// Tell the kernel to rescan the partition table
	int fd = open(Device.c_str(), O_RDONLY);
	ioctl(fd, BLKRRPART, 0);
	close(fd);

	string format_device = Device;
	if (Device.substr(0, 17) == "/dev/block/mmcblk")
		format_device += "p";

	// Format new partitions to proper file system
	if (fat_size > 0) {
		Command = "mkfs.fat " + format_device + "1";
		TWFunc::Exec_Cmd(Command);
	}
	if (ext > 0) {
		if (SDext == NULL) {
			Command = "mke2fs -t " + ext_format + " -m 0 " + format_device + "2";
			gui_msg(Msg("format_sdext_as=Formatting sd-ext as {1}...")(ext_format));
			LOGINFO("Formatting sd-ext after partitioning, command: '%s'\n", Command.c_str());
			TWFunc::Exec_Cmd(Command);
		} else {
			SDext->Wipe(ext_format);
		}
	}
	if (swap > 0) {
		Command = "mkswap " + format_device;
		if (ext > 0)
			Command += "3";
		else
			Command += "2";
		TWFunc::Exec_Cmd(Command);
	}

	// recreate TWRP folder and rewrite settings - these will be gone after sdcard is partitioned
	if (SDCard->Mount(true)) {
		string TWRP_Folder = SDCard->Mount_Point + "/TWRP";
		mkdir(TWRP_Folder.c_str(), 0777);
		DataManager::Flush();
	}

	Update_System_Details();
	gui_msg("part_complete=Partitioning complete.");
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
					gui_msg(Msg(msg::kError, "restore_unable_locate=Unable to locate '{1}' partition for restoring.")(restore_path));
				}
				start_pos = end_pos + 1;
				end_pos = Restore_List.find(";", start_pos);
			}
		}
	} else if (ListType == "wipe") {
		struct PartitionList dalvik;
		dalvik.Display_Name = gui_parse_text("{@dalvik}");
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
	} else if (ListType == "flashimg") {
		for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
			if ((*iter)->Can_Flash_Img && (*iter)->Is_Present) {
				struct PartitionList part;
				part.Display_Name = (*iter)->Backup_Display_Name;
				part.Mount_Point = (*iter)->Backup_Path;
				part.selected = 0;
				Partition_List->push_back(part);
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
		gui_msg(Msg(msg::kError, "unable_to_open=Unable to open '{1}'.")("/cache/recovery/storage.fstab"));
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

TWPartition *TWPartitionManager::Get_Default_Storage_Partition()
{
	TWPartition *res = NULL;
	for (std::vector<TWPartition*>::iterator iter = Partitions.begin(); iter != Partitions.end(); ++iter) {
		if(!(*iter)->Is_Storage)
			continue;

		if((*iter)->Is_Settings_Storage)
			return *iter;

		if(!res)
			res = *iter;
	}
	return res;
}

bool TWPartitionManager::Enable_MTP(void) {
#ifdef TW_HAS_MTP
	if (mtppid) {
		gui_err("mtp_already_enabled=MTP already enabled");
		return true;
	}
	//Launch MTP Responder
	LOGINFO("Starting MTP\n");

	int mtppipe[2];

	if (pipe(mtppipe) < 0) {
		LOGERR("Error creating MTP pipe\n");
		return false;
	}

	char old_value[PROPERTY_VALUE_MAX];
	property_get("sys.usb.config", old_value, "error");
	if (strcmp(old_value, "error") != 0 && strcmp(old_value, "mtp,adb") != 0) {
		char vendor[PROPERTY_VALUE_MAX];
		char product[PROPERTY_VALUE_MAX];
		property_set("sys.usb.config", "none");
		property_get("usb.vendor", vendor, "18D1");
		property_get("usb.product.mtpadb", product, "4EE2");
		string vendorstr = vendor;
		string productstr = product;
		TWFunc::write_file("/sys/class/android_usb/android0/idVendor", vendorstr);
		TWFunc::write_file("/sys/class/android_usb/android0/idProduct", productstr);
		property_set("sys.usb.config", "mtp,adb");
	}
	/* To enable MTP debug, use the twrp command line feature to
	 * twrp set tw_mtp_debug 1
	 */
	twrpMtp *mtp = new twrpMtp(DataManager::GetIntValue("tw_mtp_debug"));
	mtppid = mtp->forkserver(mtppipe);
	if (mtppid) {
		close(mtppipe[0]); // Host closes read side
		mtp_write_fd = mtppipe[1];
		DataManager::SetValue("tw_mtp_enabled", 1);
		Add_All_MTP_Storage();
		return true;
	} else {
		close(mtppipe[0]);
		close(mtppipe[1]);
		gui_err("mtp_fail=Failed to enable MTP");
		return false;
	}
#else
	gui_err("no_mtp=MTP support not included");
#endif
	DataManager::SetValue("tw_mtp_enabled", 0);
	return false;
}

void TWPartitionManager::Add_All_MTP_Storage(void) {
#ifdef TW_HAS_MTP
	std::vector<TWPartition*>::iterator iter;

	if (!mtppid)
		return; // MTP is not enabled

	for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		if ((*iter)->Is_Storage && (*iter)->Is_Present && (*iter)->Mount(false))
			Add_Remove_MTP_Storage((*iter), MTP_MESSAGE_ADD_STORAGE);
	}
#else
	return;
#endif
}

bool TWPartitionManager::Disable_MTP(void) {
	char old_value[PROPERTY_VALUE_MAX];
	property_get("sys.usb.config", old_value, "error");
	if (strcmp(old_value, "adb") != 0) {
		char vendor[PROPERTY_VALUE_MAX];
		char product[PROPERTY_VALUE_MAX];
		property_set("sys.usb.config", "none");
		property_get("usb.vendor", vendor, "18D1");
		property_get("usb.product.adb", product, "D002");
		string vendorstr = vendor;
		string productstr = product;
		TWFunc::write_file("/sys/class/android_usb/android0/idVendor", vendorstr);
		TWFunc::write_file("/sys/class/android_usb/android0/idProduct", productstr);
		usleep(2000);
	}
#ifdef TW_HAS_MTP
	if (mtppid) {
		LOGINFO("Disabling MTP\n");
		int status;
		kill(mtppid, SIGKILL);
		mtppid = 0;
		// We don't care about the exit value, but this prevents a zombie process
		waitpid(mtppid, &status, 0);
		close(mtp_write_fd);
		mtp_write_fd = -1;
	}
#endif
	property_set("sys.usb.config", "adb");
#ifdef TW_HAS_MTP
	DataManager::SetValue("tw_mtp_enabled", 0);
	return true;
#endif
	return false;
}

TWPartition* TWPartitionManager::Find_Partition_By_MTP_Storage_ID(unsigned int Storage_ID) {
	std::vector<TWPartition*>::iterator iter;

	for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		if ((*iter)->MTP_Storage_ID == Storage_ID)
			return (*iter);
	}
	return NULL;
}

bool TWPartitionManager::Add_Remove_MTP_Storage(TWPartition* Part, int message_type) {
#ifdef TW_HAS_MTP
	struct mtpmsg mtp_message;

	if (!mtppid)
		return false; // MTP is disabled

	if (mtp_write_fd < 0) {
		LOGINFO("MTP: mtp_write_fd is not set\n");
		return false;
	}

	if (Part) {
		if (Part->MTP_Storage_ID == 0)
			return false;
		if (message_type == MTP_MESSAGE_REMOVE_STORAGE) {
			mtp_message.message_type = MTP_MESSAGE_REMOVE_STORAGE; // Remove
			LOGINFO("sending message to remove %i\n", Part->MTP_Storage_ID);
			mtp_message.storage_id = Part->MTP_Storage_ID;
			if (write(mtp_write_fd, &mtp_message, sizeof(mtp_message)) <= 0) {
				LOGINFO("error sending message to remove storage %i\n", Part->MTP_Storage_ID);
				return false;
			} else {
				LOGINFO("Message sent, remove storage ID: %i\n", Part->MTP_Storage_ID);
				return true;
			}
		} else if (message_type == MTP_MESSAGE_ADD_STORAGE && Part->Is_Mounted()) {
			mtp_message.message_type = MTP_MESSAGE_ADD_STORAGE; // Add
			mtp_message.storage_id = Part->MTP_Storage_ID;
			mtp_message.path = Part->Storage_Path.c_str();
			mtp_message.display = Part->Storage_Name.c_str();
			mtp_message.maxFileSize = Part->Get_Max_FileSize();
			LOGINFO("sending message to add %i '%s' '%s'\n", mtp_message.storage_id, mtp_message.path, mtp_message.display);
			if (write(mtp_write_fd, &mtp_message, sizeof(mtp_message)) <= 0) {
				LOGINFO("error sending message to add storage %i\n", Part->MTP_Storage_ID);
				return false;
			} else {
				LOGINFO("Message sent, add storage ID: %i\n", Part->MTP_Storage_ID);
				return true;
			}
		} else {
			LOGERR("Unknown MTP message type: %i\n", message_type);
		}
	} else {
		// This hopefully never happens as the error handling should
		// occur in the calling function.
		LOGINFO("TWPartitionManager::Add_Remove_MTP_Storage NULL partition given\n");
	}
	return true;
#else
	gui_err("no_mtp=MTP support not included");
	DataManager::SetValue("tw_mtp_enabled", 0);
	return false;
#endif
}

bool TWPartitionManager::Add_MTP_Storage(string Mount_Point) {
#ifdef TW_HAS_MTP
	TWPartition* Part = PartitionManager.Find_Partition_By_Path(Mount_Point);
	if (Part) {
		return PartitionManager.Add_Remove_MTP_Storage(Part, MTP_MESSAGE_ADD_STORAGE);
	} else {
		LOGINFO("TWFunc::Add_MTP_Storage unable to locate partition for '%s'\n", Mount_Point.c_str());
	}
#endif
	return false;
}

bool TWPartitionManager::Add_MTP_Storage(unsigned int Storage_ID) {
#ifdef TW_HAS_MTP
	TWPartition* Part = PartitionManager.Find_Partition_By_MTP_Storage_ID(Storage_ID);
	if (Part) {
		return PartitionManager.Add_Remove_MTP_Storage(Part, MTP_MESSAGE_ADD_STORAGE);
	} else {
		LOGINFO("TWFunc::Add_MTP_Storage unable to locate partition for %i\n", Storage_ID);
	}
#endif
	return false;
}

bool TWPartitionManager::Remove_MTP_Storage(string Mount_Point) {
#ifdef TW_HAS_MTP
	TWPartition* Part = PartitionManager.Find_Partition_By_Path(Mount_Point);
	if (Part) {
		return PartitionManager.Add_Remove_MTP_Storage(Part, MTP_MESSAGE_REMOVE_STORAGE);
	} else {
		LOGINFO("TWFunc::Remove_MTP_Storage unable to locate partition for '%s'\n", Mount_Point.c_str());
	}
#endif
	return false;
}

bool TWPartitionManager::Remove_MTP_Storage(unsigned int Storage_ID) {
#ifdef TW_HAS_MTP
	TWPartition* Part = PartitionManager.Find_Partition_By_MTP_Storage_ID(Storage_ID);
	if (Part) {
		return PartitionManager.Add_Remove_MTP_Storage(Part, MTP_MESSAGE_REMOVE_STORAGE);
	} else {
		LOGINFO("TWFunc::Remove_MTP_Storage unable to locate partition for %i\n", Storage_ID);
	}
#endif
	return false;
}

bool TWPartitionManager::Flash_Image(string Filename) {
	int check, partition_count = 0;
	TWPartition* flash_part = NULL;
	string Flash_List, flash_path;
	size_t start_pos = 0, end_pos = 0;

	gui_msg("image_flash_start=[IMAGE FLASH STARTED]");
	gui_msg(Msg("img_to_flash=Image to flash: '{1}'")(Filename));

	if (!Mount_Current_Storage(true))
		return false;

	gui_msg("calc_restore=Calculating restore details...");
	DataManager::GetValue("tw_flash_partition", Flash_List);
	if (!Flash_List.empty()) {
		end_pos = Flash_List.find(";", start_pos);
		while (end_pos != string::npos && start_pos < Flash_List.size()) {
			flash_path = Flash_List.substr(start_pos, end_pos - start_pos);
			flash_part = Find_Partition_By_Path(flash_path);
			if (flash_part != NULL) {
				partition_count++;
				if (partition_count > 1) {
					gui_err("too_many_flash=Too many partitions selected for flashing.");
					return false;
				}
			} else {
				gui_msg(Msg(msg::kError, "flash_unable_locate=Unable to locate '{1}' partition for flashing.")(flash_path));
				return false;
			}
			start_pos = end_pos + 1;
			end_pos = Flash_List.find(";", start_pos);
		}
	}

	if (partition_count == 0) {
		gui_err("no_part_flash=No partitions selected for flashing.");
		return false;
	}

	DataManager::SetProgress(0.0);
	if (flash_part) {
		if (!flash_part->Flash_Image(Filename))
			return false;
	} else {
		gui_err("invalid_flash=Invalid flash partition specified.");
		return false;
	}
	gui_highlight("flash_done=IMAGE FLASH COMPLETED]");
	return true;
}

void TWPartitionManager::Translate_Partition(const char* path, const char* resource_name, const char* default_value) {
	TWPartition* part = PartitionManager.Find_Partition_By_Path(path);
	if (part) {
		part->Display_Name = gui_lookup(resource_name, default_value);
		part->Backup_Display_Name = part->Display_Name;
	}
}

void TWPartitionManager::Translate_Partition(const char* path, const char* resource_name, const char* default_value, const char* storage_resource_name, const char* storage_default_value) {
	TWPartition* part = PartitionManager.Find_Partition_By_Path(path);
	if (part) {
		part->Display_Name = gui_lookup(resource_name, default_value);
		part->Backup_Display_Name = part->Display_Name;
		if (part->Is_Storage)
			part->Storage_Name = gui_lookup(storage_resource_name, storage_default_value);
	}
}

void TWPartitionManager::Translate_Partition_Display_Names() {
	Translate_Partition("/system", "system", "System");
	Translate_Partition("/system_image", "system_image", "System Image");
	Translate_Partition("/vendor", "vendor", "Vendor");
	Translate_Partition("/vendor_image", "vendor_image", "Vendor Image");
	Translate_Partition("/cache", "cache", "Cache");
	Translate_Partition("/data", "data", "Data", "internal", "Internal Storage");
	Translate_Partition("/boot", "boot", "Boot");
	Translate_Partition("/recovery", "recovery", "Recovery");
	if (!datamedia) {
		Translate_Partition("/sdcard", "sdcard", "SDCard", "sdcard", "SDCard");
		Translate_Partition("/internal_sd", "sdcard", "SDCard", "sdcard", "SDCard");
		Translate_Partition("/internal_sdcard", "sdcard", "SDCard", "sdcard", "SDCard");
		Translate_Partition("/emmc", "sdcard", "SDCard", "sdcard", "SDCard");
	}
	Translate_Partition("/external_sd", "microsd", "Micro SDCard", "microsd", "Micro SDCard");
	Translate_Partition("/external_sdcard", "microsd", "Micro SDCard", "microsd", "Micro SDCard");
	Translate_Partition("/usb-otg", "usbotg", "USB OTG", "usbotg", "USB OTG");
	Translate_Partition("/sd-ext", "sdext", "SD-EXT");

	// Android secure is a special case
	TWPartition* part = PartitionManager.Find_Partition_By_Path("/and-sec");
	if (part)
		part->Backup_Display_Name = gui_lookup("android_secure", "Android Secure");

	// This updates the text on all of the storage selection buttons in the GUI
	DataManager::SetBackupFolder();
}
