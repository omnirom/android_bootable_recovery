/*
	Copyright 2014 to 2017 TeamWin
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
#include <map>
#include <vector>
#include <dirent.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <zlib.h>
#include <iostream>
#include <iomanip>
#include <sys/wait.h>
#include <linux/fs.h>
#include <sys/mount.h>

#include <sys/poll.h>
#include <sys/socket.h>
#include <linux/types.h>
#include <linux/netlink.h>

#include "variables.h"
#include "twcommon.h"
#include "partitions.hpp"
#include "data.hpp"
#include "twrp-functions.hpp"
#include "fixContexts.hpp"
#include "exclude.hpp"
#include "set_metadata.h"
#include "tw_atomic.hpp"
#include "gui/gui.hpp"
#include "progresstracking.hpp"
#include "twrpDigestDriver.hpp"
#include "adbbu/libtwadbbu.hpp"

#ifdef TW_HAS_MTP
#ifdef TW_HAS_LEGACY_MTP
#include "mtp/legacy/mtp_MtpServer.hpp"
#include "mtp/legacy/twrpMtp.hpp"
#include "mtp/legacy/MtpMessage.hpp"
#else
#include "mtp/ffs/mtp_MtpServer.hpp"
#include "mtp/ffs/twrpMtp.hpp"
#include "mtp/ffs/MtpMessage.hpp"
#endif
#endif

extern "C" {
	#include "cutils/properties.h"
	#include "gui/gui.h"
}

#ifdef TW_INCLUDE_CRYPTO
	#include "crypto/fde/cryptfs.h"
	#include "gui/rapidxml.hpp"
	#include "gui/pages.hpp"
	#ifdef TW_INCLUDE_FBE
		#include "crypto/ext4crypt/Decrypt.h"
		#ifdef TW_INCLUDE_FBE_METADATA_DECRYPT
			#include "crypto/ext4crypt/MetadataCrypt.h"
		#endif
	#endif
	#ifdef TW_CRYPTO_USE_SYSTEM_VOLD
		#include "crypto/vold_decrypt/vold_decrypt.h"
	#endif
#endif

#ifdef AB_OTA_UPDATER
#include <hardware/hardware.h>
#include <hardware/boot_control.h>
#endif

extern bool datamedia;

TWPartitionManager::TWPartitionManager(void) {
	mtp_was_enabled = false;
	mtp_write_fd = -1;
	uevent_pfd.fd = -1;
	stop_backup.set_value(0);
#ifdef AB_OTA_UPDATER
	char slot_suffix[PROPERTY_VALUE_MAX];
	property_get("ro.boot.slot_suffix", slot_suffix, "error");
	if (strcmp(slot_suffix, "error") == 0)
		property_get("ro.boot.slot", slot_suffix, "error");
	Active_Slot_Display = "";
	if (strcmp(slot_suffix, "_a") == 0 || strcmp(slot_suffix, "a") == 0)
		Set_Active_Slot("A");
	else
		Set_Active_Slot("B");
#endif
}

int TWPartitionManager::Process_Fstab(string Fstab_Filename, bool Display_Error) {
	FILE *fstabFile;
	char fstab_line[MAX_FSTAB_LINE_LENGTH];
	TWPartition* settings_partition = NULL;
	TWPartition* andsec_partition = NULL;
	unsigned int storageid = 1 << 16;	// upper 16 bits are for physical storage device, we pretend to have only one
	std::map<string, Flags_Map> twrp_flags;

	fstabFile = fopen("/etc/twrp.flags", "rt");
	if (fstabFile != NULL) {
		LOGINFO("reading /etc/twrp.flags\n");
		while (fgets(fstab_line, sizeof(fstab_line), fstabFile) != NULL) {
			if (fstab_line[0] != '/')
				continue;

			size_t line_size = strlen(fstab_line);
			if (fstab_line[line_size - 1] != '\n')
				fstab_line[line_size] = '\n';
			Flags_Map line_flags;
			line_flags.Primary_Block_Device = "";
			line_flags.Alternate_Block_Device = "";
			line_flags.fstab_line = (char*)malloc(MAX_FSTAB_LINE_LENGTH);
			if (!line_flags.fstab_line) {
				LOGERR("malloc error on line_flags.fstab_line\n");
				return false;
			}
			memcpy(line_flags.fstab_line, fstab_line, MAX_FSTAB_LINE_LENGTH);
			bool found_separator = false;
			char *fs_loc = NULL;
			char *block_loc = NULL;
			char *flags_loc = NULL;
			size_t index, item_index = 0;
			for (index = 0; index < line_size; index++) {
				if (fstab_line[index] <= 32) {
					fstab_line[index] = '\0';
					found_separator = true;
				} else if (found_separator) {
					if (item_index == 0) {
						fs_loc = fstab_line + index;
					} else if (item_index == 1) {
						block_loc = fstab_line + index;
					} else if (item_index > 1) {
						char *ptr = fstab_line + index;
						if (*ptr == '/') {
							line_flags.Alternate_Block_Device = ptr;
						} else if (strlen(ptr) > strlen("flags=") && strncmp(ptr, "flags=", strlen("flags=")) == 0) {
							flags_loc = ptr;
							// Once we find the flags=, we're done scanning the line
							break;
						}
					}
					found_separator = false;
					item_index++;
				}
			}
			if (block_loc)
				line_flags.Primary_Block_Device = block_loc;
			if (fs_loc)
				line_flags.File_System = fs_loc;
			if (flags_loc)
				line_flags.Flags = flags_loc;
			string Mount_Point = fstab_line;
			twrp_flags[Mount_Point] = line_flags;
			memset(fstab_line, 0, sizeof(fstab_line));
		}
		fclose(fstabFile);
	}

	fstabFile = fopen(Fstab_Filename.c_str(), "rt");
	if (fstabFile == NULL) {
		LOGERR("Critical Error: Unable to open fstab at '%s'.\n", Fstab_Filename.c_str());
		return false;
	} else
		LOGINFO("Reading %s\n", Fstab_Filename.c_str());

	while (fgets(fstab_line, sizeof(fstab_line), fstabFile) != NULL) {
		if (fstab_line[0] != '/')
			continue;

		if (strstr(fstab_line, "swap"))
			continue; // Skip swap in recovery

		size_t line_size = strlen(fstab_line);
		if (fstab_line[line_size - 1] != '\n')
			fstab_line[line_size] = '\n';

		TWPartition* partition = new TWPartition();
		if (partition->Process_Fstab_Line(fstab_line, Display_Error, &twrp_flags))
			Partitions.push_back(partition);
		else
			delete partition;

		memset(fstab_line, 0, sizeof(fstab_line));
	}
	fclose(fstabFile);

	if (twrp_flags.size() > 0) {
		LOGINFO("Processing remaining twrp.flags\n");
		// Add any items from twrp.flags that did not exist in the recovery.fstab
		for (std::map<string, Flags_Map>::iterator mapit=twrp_flags.begin(); mapit!=twrp_flags.end(); mapit++) {
			if (Find_Partition_By_Path(mapit->first) == NULL) {
				TWPartition* partition = new TWPartition();
				if (partition->Process_Fstab_Line(mapit->second.fstab_line, Display_Error, NULL))
					Partitions.push_back(partition);
				else
					delete partition;
			}
			if (mapit->second.fstab_line)
				free(mapit->second.fstab_line);
			mapit->second.fstab_line = NULL;
		}
	}
	LOGINFO("Done processing fstab files\n");

	std::vector<TWPartition*>::iterator iter;
	for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		(*iter)->Partition_Post_Processing(Display_Error);

		if ((*iter)->Is_Storage) {
			++storageid;
			(*iter)->MTP_Storage_ID = storageid;
		}

		if (!settings_partition && (*iter)->Is_Settings_Storage && (*iter)->Is_Present)
			settings_partition = (*iter);
		else
			(*iter)->Is_Settings_Storage = false;

		if (!andsec_partition && (*iter)->Has_Android_Secure && (*iter)->Is_Present)
			andsec_partition = (*iter);
		else
			(*iter)->Has_Android_Secure = false;
	}

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
		if (!Decrypt_Data->Key_Directory.empty() && Mount_By_Path(Decrypt_Data->Key_Directory, false)) {
#ifdef TW_INCLUDE_FBE_METADATA_DECRYPT
			if (e4crypt_mount_metadata_encrypted(Decrypt_Data->Mount_Point, false, Decrypt_Data->Key_Directory, Decrypt_Data->Actual_Block_Device, &Decrypt_Data->Decrypted_Block_Device)) {
				LOGINFO("Successfully decrypted metadata encrypted data partition with new block device: '%s'\n", Decrypt_Data->Decrypted_Block_Device.c_str());
				property_set("ro.crypto.state", "encrypted");
				Decrypt_Data->Is_Decrypted = true; // Needed to make the mount function work correctly
				int retry_count = 10;
				while (!Decrypt_Data->Mount(false) && --retry_count)
					usleep(500);
				if (Decrypt_Data->Mount(false)) {
					if (!Decrypt_Data->Decrypt_FBE_DE()) {
						LOGINFO("Trying wrapped key.\n");
						property_set("fbe.data.wrappedkey", "true");
						if (!Decrypt_Data->Decrypt_FBE_DE()) {
							LOGERR("Unable to decrypt FBE device\n");
						}
					}

				} else {
					LOGINFO("Failed to mount data after metadata decrypt\n");
				}
			} else {
				LOGINFO("Unable to decrypt metadata encryption\n");
			}
#else
			LOGERR("Metadata FBE decrypt support not present in this TWRP\n");
#endif
		}
		if (Decrypt_Data->Is_FBE) {
			if (DataManager::GetIntValue(TW_CRYPTO_PWTYPE) == 0) {
				if (Decrypt_Device("!") == 0) {
					gui_msg("decrypt_success=Successfully decrypted with default password.");
					DataManager::SetValue(TW_IS_ENCRYPTED, 0);
				} else {
					gui_err("unable_to_decrypt=Unable to decrypt with default password.");
				}
			}
		} else {
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
	}
	if (Decrypt_Data && (!Decrypt_Data->Is_Encrypted || Decrypt_Data->Is_Decrypted) && Decrypt_Data->Mount(false)) {
		Decrypt_Adopted();
	}
#endif
	Update_System_Details();
	UnMount_Main_Partitions();
#ifdef AB_OTA_UPDATER
	DataManager::SetValue("tw_active_slot", Get_Active_Slot_Display());
#endif
	setup_uevent();
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
	if (Part->Is_Adopted_Storage)
		printf("Is_Adopted_Storage ");
	if (Part->SlotSelect)
		printf("SlotSelect ");
	if (Part->Mount_Read_Only)
		printf("Mount_Read_Only ");
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
	if (!Part->Crypto_Key_Location.empty())
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
	printf("   Backup_Method: %s\n", Part->Backup_Method_By_Name().c_str());
	if (Part->Mount_Flags || !Part->Mount_Options.empty())
		printf("   Mount_Flags: %i, Mount_Options: %s\n", Part->Mount_Flags, Part->Mount_Options.c_str());
	if (Part->MTP_Storage_ID)
		printf("   MTP_Storage_ID: %i\n", Part->MTP_Storage_ID);
	if (!Part->Key_Directory.empty())
		printf("   Metadata Key Directory: %s\n", Part->Key_Directory.c_str());
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

TWPartition* TWPartitionManager::Find_Partition_By_Path(const string& Path) {
	std::vector<TWPartition*>::iterator iter;
	string Local_Path = TWFunc::Get_Root_Path(Path);

	for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		if ((*iter)->Mount_Point == Local_Path || (!(*iter)->Symlink_Mount_Point.empty() && (*iter)->Symlink_Mount_Point == Local_Path))
			return (*iter);
	}
	return NULL;
}

TWPartition* TWPartitionManager::Find_Partition_By_Block_Device(const string& Block_Device) {
	std::vector<TWPartition*>::iterator iter;

	for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		if ((*iter)->Primary_Block_Device == Block_Device || (!(*iter)->Actual_Block_Device.empty() && (*iter)->Actual_Block_Device == Block_Device))
			return (*iter);
	}
	return NULL;
}

int TWPartitionManager::Check_Backup_Name(const std::string& Backup_Name, bool Display_Error, bool Must_Be_Unique) {
	// Check the backup name to ensure that it is the correct size and contains only valid characters
	// and that a backup with that name doesn't already exist
	char backup_name[MAX_BACKUP_NAME_LEN];
	char backup_loc[255], tw_image_dir[255];
	int copy_size;
	int index, cur_char;
	string Backup_Loc;

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
				gui_msg(Msg(msg::kError, "backup_name_invalid=Backup name '{1}' contains invalid character: '{1}'")(Backup_Name)((char)cur_char));
			return -3;
		}
	}

	if (Must_Be_Unique) {
		// Check to make sure that a backup with this name doesn't already exist
		DataManager::GetValue(TW_BACKUPS_FOLDER_VAR, Backup_Loc);
		strcpy(backup_loc, Backup_Loc.c_str());
		sprintf(tw_image_dir,"%s/%s", backup_loc, Backup_Name.c_str());
		if (TWFunc::Path_Exists(tw_image_dir)) {
			if (Display_Error)
				gui_err("backup_name_exists=A backup with that name already exists!");

			return -4;
		}
		// Backup is unique
	}
	// No problems found
	return 0;
}

bool TWPartitionManager::Backup_Partition(PartitionSettings *part_settings) {
	time_t start, stop;
	int use_compression;
	string backup_log = part_settings->Backup_Folder + "/recovery.log";

	if (part_settings->Part == NULL)
		return true;

	DataManager::GetValue(TW_USE_COMPRESSION_VAR, use_compression);

	TWFunc::SetPerformanceMode(true);
	time(&start);

	if (part_settings->Part->Backup(part_settings, &tar_fork_pid)) {
		sync();
		sync();
		string Full_Filename = part_settings->Backup_Folder + "/" + part_settings->Part->Backup_FileName;
		if (!part_settings->adbbackup && part_settings->generate_digest) {
			if (!twrpDigestDriver::Make_Digest(Full_Filename))
				goto backup_error;
		}

		if (part_settings->Part->Has_SubPartition) {
			std::vector<TWPartition*>::iterator subpart;
			TWPartition *parentPart = part_settings->Part;

			for (subpart = Partitions.begin(); subpart != Partitions.end(); subpart++) {
				if ((*subpart)->Can_Be_Backed_Up && (*subpart)->Is_SubPartition && (*subpart)->SubPartition_Of == parentPart->Mount_Point) {
					part_settings->Part = *subpart;
					if (!(*subpart)->Backup(part_settings, &tar_fork_pid)) {
						goto backup_error;
					}
					sync();
					sync();
					string Full_Filename = part_settings->Backup_Folder + "/" + part_settings->Part->Backup_FileName;
					if (!part_settings->adbbackup && part_settings->generate_digest) {
						if (!twrpDigestDriver::Make_Digest(Full_Filename)) {
							goto backup_error;
						}
					}
				}
			}
		}

		time(&stop);
		int backup_time = (int) difftime(stop, start);
		LOGINFO("Partition Backup time: %d\n", backup_time);

		if (part_settings->Part->Backup_Method == BM_FILES) {
			part_settings->file_time += backup_time;
		} else {
			part_settings->img_time += backup_time;

		}

		TWFunc::SetPerformanceMode(false);
		return true;
	}
backup_error:
	Clean_Backup_Folder(part_settings->Backup_Folder);
	TWFunc::copy_file("/tmp/recovery.log", backup_log, 0644);
	tw_set_default_metadata(backup_log.c_str());
	TWFunc::SetPerformanceMode(false);
	return false;
}

void TWPartitionManager::Clean_Backup_Folder(string Backup_Folder) {
	DIR *d = opendir(Backup_Folder.c_str());
	struct dirent *p;
	int r;
	vector<string> ext;

	//extensions we should delete when cleaning
	ext.push_back("win");
	ext.push_back("md5");
	ext.push_back("sha2");
	ext.push_back("info");

	gui_msg("backup_clean=Backup Failed. Cleaning Backup Folder.");

	if (d == NULL) {
		gui_msg(Msg(msg::kError, "error_opening_strerr=Error opening: '{1}' ({2})")(Backup_Folder)(strerror(errno)));
		return;
	}

	while ((p = readdir(d))) {
		if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, ".."))
			continue;

		string path = Backup_Folder + "/" + p->d_name;

		size_t dot = path.find_last_of(".") + 1;
		for (vector<string>::const_iterator i = ext.begin(); i != ext.end(); ++i) {
			if (path.substr(dot) == *i) {
				r = unlink(path.c_str());
				if (r != 0)
					LOGINFO("Unable to unlink '%s: %s'\n", path.c_str(), strerror(errno));
			}
		}
	}
	closedir(d);
}

int TWPartitionManager::Check_Backup_Cancel() {
	return stop_backup.get_value();
}

int TWPartitionManager::Cancel_Backup() {
	string Backup_Folder, Backup_Name, Full_Backup_Path;

	stop_backup.set_value(1);

	if (tar_fork_pid != 0) {
		DataManager::GetValue(TW_BACKUP_NAME, Backup_Name);
		DataManager::GetValue(TW_BACKUPS_FOLDER_VAR, Backup_Folder);
		Full_Backup_Path = Backup_Folder + "/" + Backup_Name;
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

int TWPartitionManager::Run_Backup(bool adbbackup) {
	PartitionSettings part_settings;
	int partition_count = 0, disable_free_space_check = 0, skip_digest = 0;
	string Backup_Name, Backup_List, backup_path;
	unsigned long long total_bytes = 0, free_space = 0;
	TWPartition* storage = NULL;
	std::vector<TWPartition*>::iterator subpart;
	struct tm *t;
	time_t seconds, total_start, total_stop;
	size_t start_pos = 0, end_pos = 0;
	stop_backup.set_value(0);
	seconds = time(0);
	t = localtime(&seconds);

	part_settings.img_bytes_remaining = 0;
	part_settings.file_bytes_remaining = 0;
	part_settings.img_time = 0;
	part_settings.file_time = 0;
	part_settings.img_bytes = 0;
	part_settings.file_bytes = 0;
	part_settings.PM_Method = PM_BACKUP;

	part_settings.adbbackup = adbbackup;
	time(&total_start);

	Update_System_Details();

	if (!Mount_Current_Storage(true))
		return false;

	DataManager::GetValue(TW_SKIP_DIGEST_GENERATE_VAR, skip_digest);
	if (skip_digest == 0)
		part_settings.generate_digest = true;
	else
		part_settings.generate_digest = false;

	DataManager::GetValue(TW_BACKUPS_FOLDER_VAR, part_settings.Backup_Folder);
	DataManager::GetValue(TW_BACKUP_NAME, Backup_Name);
	if (Backup_Name == gui_lookup("curr_date", "(Current Date)")) {
		Backup_Name = TWFunc::Get_Current_Date();
	} else if (Backup_Name == gui_lookup("auto_generate", "(Auto Generate)") || Backup_Name == "0" || Backup_Name.empty()) {
		TWFunc::Auto_Generate_Backup_Name();
		DataManager::GetValue(TW_BACKUP_NAME, Backup_Name);
	}

	LOGINFO("Backup Name is: '%s'\n", Backup_Name.c_str());
	part_settings.Backup_Folder = part_settings.Backup_Folder + "/" + Backup_Name;

	LOGINFO("Backup_Folder is: '%s'\n", part_settings.Backup_Folder.c_str());

	LOGINFO("Calculating backup details...\n");
	DataManager::GetValue("tw_backup_list", Backup_List);
	if (!Backup_List.empty()) {
		end_pos = Backup_List.find(";", start_pos);
		while (end_pos != string::npos && start_pos < Backup_List.size()) {
			backup_path = Backup_List.substr(start_pos, end_pos - start_pos);
			part_settings.Part = Find_Partition_By_Path(backup_path);
			if (part_settings.Part != NULL) {
				partition_count++;
				if (part_settings.Part->Backup_Method == BM_FILES)
					part_settings.file_bytes += part_settings.Part->Backup_Size;
				else
					part_settings.img_bytes += part_settings.Part->Backup_Size;
				if (part_settings.Part->Has_SubPartition) {
					std::vector<TWPartition*>::iterator subpart;

					for (subpart = Partitions.begin(); subpart != Partitions.end(); subpart++) {
						if ((*subpart)->Can_Be_Backed_Up && (*subpart)->Is_Present && (*subpart)->Is_SubPartition && (*subpart)->SubPartition_Of == part_settings.Part->Mount_Point) {
							partition_count++;
							if ((*subpart)->Backup_Method == BM_FILES)
								part_settings.file_bytes += (*subpart)->Backup_Size;
							else
								part_settings.img_bytes += (*subpart)->Backup_Size;
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
	if (adbbackup) {
		if (twadbbu::Write_ADB_Stream_Header(partition_count) == false) {
			return false;
		}
	}
	total_bytes = part_settings.file_bytes + part_settings.img_bytes;
	ProgressTracking progress(total_bytes);
	part_settings.progress = &progress;

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

	DataManager::GetValue(TW_DISABLE_FREE_SPACE_VAR, disable_free_space_check);

	if (adbbackup)
		disable_free_space_check = true;

	if (!disable_free_space_check) {
		if (free_space - (32 * 1024 * 1024) < total_bytes) {
			// We require an extra 32MB just in case
			gui_err("no_space=Not enough free space on storage.");
			return false;
		}
	}
	part_settings.img_bytes_remaining = part_settings.img_bytes;
	part_settings.file_bytes_remaining = part_settings.file_bytes;

	gui_msg("backup_started=[BACKUP STARTED]");

	int is_decrypted = 0;
	int is_encrypted = 0;

	DataManager::GetValue(TW_IS_DECRYPTED, is_decrypted);
	DataManager::GetValue(TW_IS_ENCRYPTED, is_encrypted);
	if (!adbbackup || (!is_encrypted || (is_encrypted && is_decrypted))) {
		gui_msg(Msg("backup_folder= * Backup Folder: {1}")(part_settings.Backup_Folder));
		if (!TWFunc::Recursive_Mkdir(part_settings.Backup_Folder)) {
			gui_err("fail_backup_folder=Failed to make backup folder.");
			return false;
		}
	}

	DataManager::SetProgress(0.0);

	start_pos = 0;
	end_pos = Backup_List.find(";", start_pos);
	while (end_pos != string::npos && start_pos < Backup_List.size()) {
		if (stop_backup.get_value() != 0)
			return -1;
		backup_path = Backup_List.substr(start_pos, end_pos - start_pos);
		part_settings.Part = Find_Partition_By_Path(backup_path);
		if (part_settings.Part != NULL) {
			if (!Backup_Partition(&part_settings))
				return false;
		} else {
			gui_msg(Msg(msg::kError, "unable_to_locate_partition=Unable to locate '{1}' partition for backup calculations.")(backup_path));
		}
		start_pos = end_pos + 1;
		end_pos = Backup_List.find(";", start_pos);
	}

	// Average BPS
	if (part_settings.img_time == 0)
		part_settings.img_time = 1;
	if (part_settings.file_time == 0)
		part_settings.file_time = 1;
	int img_bps = (int)part_settings.img_bytes / (int)part_settings.img_time;
	unsigned long long file_bps = part_settings.file_bytes / (int)part_settings.file_time;

	if (part_settings.file_bytes != 0)
		gui_msg(Msg("avg_backup_fs=Average backup rate for file systems: {1} MB/sec")(file_bps / (1024 * 1024)));
	if (part_settings.img_bytes != 0)
		gui_msg(Msg("avg_backup_img=Average backup rate for imaged drives: {1} MB/sec")(img_bps / (1024 * 1024)));

	time(&total_stop);
	int total_time = (int) difftime(total_stop, total_start);

	uint64_t actual_backup_size;
	if (!adbbackup) {
		TWExclude twe;
		actual_backup_size = twe.Get_Folder_Size(part_settings.Backup_Folder);
	} else
		actual_backup_size = part_settings.file_bytes + part_settings.img_bytes;
	actual_backup_size /= (1024LLU * 1024LLU);

	int prev_img_bps = 0, use_compression = 0;
	unsigned long long prev_file_bps = 0;
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
	string backup_log = part_settings.Backup_Folder + "/recovery.log";
	TWFunc::copy_file("/tmp/recovery.log", backup_log, 0644);
	tw_set_default_metadata(backup_log.c_str());

	if (part_settings.adbbackup) {
		if (twadbbu::Write_ADB_Stream_Trailer() == false) {
			return false;
		}
	}
	part_settings.adbbackup = false;
	DataManager::SetValue("tw_enable_adb_backup", 0);

	return true;
}

bool TWPartitionManager::Restore_Partition(PartitionSettings *part_settings) {
	time_t Start, Stop;

	if (part_settings->adbbackup) {
		std::string partName = part_settings->Part->Backup_Name + "." + part_settings->Part->Current_File_System + ".win";
		LOGINFO("setting backup name: %s\n", partName.c_str());
		part_settings->Part->Set_Backup_FileName(part_settings->Part->Backup_Name + "." + part_settings->Part->Current_File_System + ".win");
	}

	TWFunc::SetPerformanceMode(true);

	time(&Start);

	if (!part_settings->Part->Restore(part_settings)) {
		TWFunc::SetPerformanceMode(false);
		return false;
	}
	if (part_settings->Part->Has_SubPartition && !part_settings->adbbackup) {
		std::vector<TWPartition*>::iterator subpart;
		TWPartition *parentPart = part_settings->Part;

		for (subpart = Partitions.begin(); subpart != Partitions.end(); subpart++) {
			part_settings->Part = *subpart;
			if ((*subpart)->Is_SubPartition && (*subpart)->SubPartition_Of == parentPart->Mount_Point) {
				part_settings->Part = (*subpart);
				part_settings->Part->Set_Backup_FileName(part_settings->Part->Backup_Name + "." + part_settings->Part->Current_File_System + ".win");
				if (!(*subpart)->Restore(part_settings)) {
					TWFunc::SetPerformanceMode(false);
					return false;
				}
			}
		}
	}
	time(&Stop);
	TWFunc::SetPerformanceMode(false);
	gui_msg(Msg("restore_part_done=[{1} done ({2} seconds)]")(part_settings->Part->Backup_Display_Name)((int)difftime(Stop, Start)));

	return true;
}

int TWPartitionManager::Run_Restore(const string& Restore_Name) {
	PartitionSettings part_settings;
	int check_digest;

	time_t rStart, rStop;
	time(&rStart);
	string Restore_List, restore_path;
	size_t start_pos = 0, end_pos;

	part_settings.Backup_Folder = Restore_Name;
	part_settings.Part = NULL;
	part_settings.partition_count = 0;
	part_settings.total_restore_size = 0;
	part_settings.adbbackup = false;
	part_settings.PM_Method = PM_RESTORE;

	gui_msg("restore_started=[RESTORE STARTED]");
	gui_msg(Msg("restore_folder=Restore folder: '{1}'")(Restore_Name));

	if (!Mount_Current_Storage(true))
		return false;

	DataManager::GetValue(TW_SKIP_DIGEST_CHECK_VAR, check_digest);
	if (check_digest > 0) {
		// Check Digest files first before restoring to ensure that all of them match before starting a restore
		TWFunc::GUI_Operation_Text(TW_VERIFY_DIGEST_TEXT, gui_parse_text("{@verifying_digest}"));
		gui_msg("verifying_digest=Verifying Digest");
	} else {
		gui_msg("skip_digest=Skipping Digest check based on user setting.");
	}
	gui_msg("calc_restore=Calculating restore details...");
	DataManager::GetValue("tw_restore_selected", Restore_List);

	if (!Restore_List.empty()) {
		end_pos = Restore_List.find(";", start_pos);
		while (end_pos != string::npos && start_pos < Restore_List.size()) {
			restore_path = Restore_List.substr(start_pos, end_pos - start_pos);
			part_settings.Part = Find_Partition_By_Path(restore_path);
			if (part_settings.Part != NULL) {
				if (part_settings.Part->Mount_Read_Only) {
					gui_msg(Msg(msg::kError, "restore_read_only=Cannot restore {1} -- mounted read only.")(part_settings.Part->Backup_Display_Name));
					return false;
				}

				string Full_Filename = part_settings.Backup_Folder + "/" + part_settings.Part->Backup_FileName;

				if (check_digest > 0 && !twrpDigestDriver::Check_Digest(Full_Filename))
					return false;
				part_settings.partition_count++;
				part_settings.total_restore_size += part_settings.Part->Get_Restore_Size(&part_settings);
				if (part_settings.Part->Has_SubPartition) {
					TWPartition *parentPart = part_settings.Part;
					std::vector<TWPartition*>::iterator subpart;

					for (subpart = Partitions.begin(); subpart != Partitions.end(); subpart++) {
						part_settings.Part = *subpart;
						if ((*subpart)->Is_SubPartition && (*subpart)->SubPartition_Of == parentPart->Mount_Point) {
							if (check_digest > 0 && !twrpDigestDriver::Check_Digest(Full_Filename))
								return false;
							part_settings.total_restore_size += (*subpart)->Get_Restore_Size(&part_settings);
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

	if (part_settings.partition_count == 0) {
		gui_err("no_part_restore=No partitions selected for restore.");
		return false;
	}

	gui_msg(Msg("restore_part_count=Restoring {1} partitions...")(part_settings.partition_count));
	gui_msg(Msg("total_restore_size=Total restore size is {1}MB")(part_settings.total_restore_size / 1048576));
	DataManager::SetProgress(0.0);
	ProgressTracking progress(part_settings.total_restore_size);
	part_settings.progress = &progress;

	start_pos = 0;
	if (!Restore_List.empty()) {
		end_pos = Restore_List.find(";", start_pos);
		while (end_pos != string::npos && start_pos < Restore_List.size()) {
			restore_path = Restore_List.substr(start_pos, end_pos - start_pos);

			part_settings.Part = Find_Partition_By_Path(restore_path);
			if (part_settings.Part != NULL) {
				part_settings.partition_count++;
				if (!Restore_Partition(&part_settings))
					return false;
			} else {
				gui_msg(Msg(msg::kError, "restore_unable_locate=Unable to locate '{1}' partition for restoring.")(restore_path));
			}
			start_pos = end_pos + 1;
			end_pos = Restore_List.find(";", start_pos);
		}
	}
	TWFunc::GUI_Operation_Text(TW_UPDATE_SYSTEM_DETAILS_TEXT, gui_parse_text("{@updating_system_details}"));
	UnMount_By_Path(Get_Android_Root_Path(), false);
	Update_System_Details();
	UnMount_Main_Partitions();
	time(&rStop);
	gui_msg(Msg(msg::kHighlight, "restore_completed=[RESTORE COMPLETED IN {1} SECONDS]")((int)difftime(rStop,rStart)));
	DataManager::SetValue("tw_file_progress", "");

	return true;
}

void TWPartitionManager::Set_Restore_Files(string Restore_Name) {
	// Start with the default values
	string Restore_List;
	bool get_date = true, check_encryption = true;
	bool adbbackup = false;

	DataManager::SetValue("tw_restore_encrypted", 0);
	if (twadbbu::Check_ADB_Backup_File(Restore_Name)) {
		vector<string> adb_files;
		adb_files = twadbbu::Get_ADB_Backup_Files(Restore_Name);
		for (unsigned int i = 0; i < adb_files.size(); ++i) {
			string adb_restore_file = adb_files.at(i);
			std::size_t pos = adb_restore_file.find_first_of(".");
			std::string path = "/" + adb_restore_file.substr(0, pos);
			Restore_List = path + ";";
			TWPartition* Part = Find_Partition_By_Path(path);
			Part->Backup_FileName = TWFunc::Get_Filename(adb_restore_file);
			adbbackup = true;
		}
		DataManager::SetValue("tw_enable_adb_backup", 1);
	}
	else {
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

			if (!Part->Is_SubPartition)
				Restore_List += Part->Backup_Path + ";";
		}
		closedir(d);
	}

	if (adbbackup) {
		Restore_List = "ADB_Backup;";
		adbbackup = false;
	}

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
	TWFunc::check_and_run_script("/sbin/factoryreset.sh", "Factory Reset Script");
	return ret;
}

int TWPartitionManager::Wipe_Dalvik_Cache(void) {
	struct stat st;
	vector <string> dir;

	if (!Mount_By_Path("/data", true))
		return false;

	dir.push_back("/data/dalvik-cache");

	std::string cacheDir = TWFunc::get_cache_dir();
	if (cacheDir == NON_AB_CACHE_DIR) {
		if (!PartitionManager.Mount_By_Path(NON_AB_CACHE_DIR, false)) {
			LOGINFO("Unable to mount %s for wiping cache.\n", NON_AB_CACHE_DIR);
		}
		dir.push_back(cacheDir + "dalvik-cache");
		dir.push_back(cacheDir + "/dc");
	}

	TWPartition* sdext = Find_Partition_By_Path("/sd-ext");
	if (sdext && sdext->Is_Present && sdext->Mount(false))
	{
		if (stat("/sd-ext/dalvik-cache", &st) == 0)
		{
			dir.push_back("/sd-ext/dalvik-cache");
		}
	}

	if (cacheDir == NON_AB_CACHE_DIR) {
		gui_msg("wiping_cache_dalvik=Wiping Dalvik Cache Directories...");
	} else {
		gui_msg("wiping_dalvik=Wiping Dalvik Directory...");
	}
	for (unsigned i = 0; i < dir.size(); ++i) {
		if (stat(dir.at(i).c_str(), &st) == 0) {
			TWFunc::removeDir(dir.at(i), false);
			gui_msg(Msg("cleaned=Cleaned: {1}...")(dir.at(i)));
		}
	}

	if (cacheDir == NON_AB_CACHE_DIR) {
		gui_msg("cache_dalvik_done=-- Dalvik Cache Directories Wipe Complete!");
	} else {
		gui_msg("dalvik_done=-- Dalvik Directory Wipe Complete!");
	}

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
		gui_msg(Msg(msg::kError, "unable_to_locate=Unable to locate {1}.")("/data"));
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
		gui_msg(Msg(msg::kError, "unable_to_locate=Unable to locate {1}.")("/data"));
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
		(*iter)->Update_Size(true);
		if ((*iter)->Can_Be_Mounted) {
			if ((*iter)->Mount_Point == Get_Android_Root_Path()) {
				int backup_display_size = (int)((*iter)->Backup_Size / 1048576LLU);
				DataManager::SetValue(TW_BACKUP_SYSTEM_SIZE, backup_display_size);
				TWFunc::Is_TWRP_App_In_System();
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

void TWPartitionManager::Post_Decrypt(const string& Block_Device) {
	TWPartition* dat = Find_Partition_By_Path("/data");
	if (dat != NULL) {
		DataManager::SetValue(TW_IS_DECRYPTED, 1);
		dat->Is_Decrypted = true;
		if (!Block_Device.empty()) {
			dat->Decrypted_Block_Device = Block_Device;
			gui_msg(Msg("decrypt_success_dev=Data successfully decrypted, new block device: '{1}'")(Block_Device));
		} else {
			gui_msg("decrypt_success_nodev=Data successfully decrypted");
		}
		dat->Setup_File_System(false);
		dat->Current_File_System = dat->Fstab_File_System; // Needed if we're ignoring blkid because encrypted devices start out as emmc

		// Sleep for a bit so that the device will be ready
		sleep(1);
		if (dat->Has_Data_Media && dat->Mount(false) && TWFunc::Path_Exists("/data/media/0")) {
			dat->Storage_Path = "/data/media/0";
			dat->Symlink_Path = dat->Storage_Path;
			DataManager::SetValue("tw_storage_path", "/data/media/0");
			DataManager::SetValue("tw_settings_path", "/data/media/0");
			dat->UnMount(false);
		}
		Update_System_Details();
		Output_Partition(dat);
		UnMount_Main_Partitions();
	} else
		LOGERR("Unable to locate data partition.\n");
}

int TWPartitionManager::Decrypt_Device(string Password) {
#ifdef TW_INCLUDE_CRYPTO
	char crypto_state[PROPERTY_VALUE_MAX], crypto_blkdev[PROPERTY_VALUE_MAX];
	std::vector<TWPartition*>::iterator iter;

	// Mount any partitions that need to be mounted for decrypt
	for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		if ((*iter)->Mount_To_Decrypt) {
			(*iter)->Mount(true);
		}
	}
	property_set("twrp.mount_to_decrypt", "1");

	property_get("ro.crypto.state", crypto_state, "error");
	if (strcmp(crypto_state, "error") == 0) {
		property_set("ro.crypto.state", "encrypted");
		// Sleep for a bit so that services can start if needed
		sleep(1);
	}

	if (DataManager::GetIntValue(TW_IS_FBE)) {
#ifdef TW_INCLUDE_FBE
		if (!Mount_By_Path("/data", true)) // /data has to be mounted for FBE
			return -1;
		int retry_count = 10;
		while (!TWFunc::Path_Exists("/data/system/users/gatekeeper.password.key") && --retry_count)
			usleep(2000); // A small sleep is needed after mounting /data to ensure reliable decrypt... maybe because of DE?
		int user_id = DataManager::GetIntValue("tw_decrypt_user_id");
		LOGINFO("Decrypting FBE for user %i\n", user_id);
		if (Decrypt_User(user_id, Password)) {
			Post_Decrypt("");
			return 0;
		}
#else
		LOGERR("FBE support is not present\n");
#endif
		return -1;
	}

	int pwret = -1;
	pid_t pid = fork();
	if (pid < 0) {
		LOGERR("fork failed\n");
		return -1;
	} else if (pid == 0) {
		// Child process
		char cPassword[255];
		strcpy(cPassword, Password.c_str());
		int ret = cryptfs_check_passwd(cPassword);
		exit(ret);
	} else {
		// Parent
		int status;
		if (TWFunc::Wait_For_Child_Timeout(pid, &status, "Decrypt", 30))
			pwret = -1;
		else
			pwret = WEXITSTATUS(status) ? -1 : 0;
	}

#ifdef TW_CRYPTO_USE_SYSTEM_VOLD
	if (pwret != 0) {
		pwret = vold_decrypt(Password);
		switch (pwret) {
			case VD_SUCCESS:
				break;
			case VD_ERR_MISSING_VDC:
				gui_msg(Msg(msg::kError, "decrypt_data_vold_os_missing=Missing files needed for vold decrypt: {1}")("/system/bin/vdc"));
				break;
			case VD_ERR_MISSING_VOLD:
				gui_msg(Msg(msg::kError, "decrypt_data_vold_os_missing=Missing files needed for vold decrypt: {1}")("/system/bin/vold"));
				break;
		}
	}
#endif // TW_CRYPTO_USE_SYSTEM_VOLD

	// Unmount any partitions that were needed for decrypt
	for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		if ((*iter)->Mount_To_Decrypt) {
			(*iter)->UnMount(false);
		}
	}
	property_set("twrp.mount_to_decrypt", "0");

	if (pwret != 0) {
		gui_err("fail_decrypt=Failed to decrypt data.");
		return -1;
	}

	property_get("ro.crypto.fs_crypto_blkdev", crypto_blkdev, "error");
	if (strcmp(crypto_blkdev, "error") == 0) {
		LOGERR("Error retrieving decrypted data block device.\n");
	} else {
		Post_Decrypt(crypto_blkdev);
	}
	return 0;
#else
	gui_err("no_crypto_support=No crypto support was compiled into this build.");
	return -1;
#endif
	return 1;
}

int TWPartitionManager::Fix_Contexts(void) {
	std::vector<TWPartition*>::iterator iter;
	for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		if ((*iter)->Has_Data_Media) {
			if ((*iter)->Mount(true)) {
				if (fixContexts::fixDataMediaContexts((*iter)->Mount_Point) != 0)
					return -1;
			}
		}
	}
	UnMount_Main_Partitions();
	gui_msg("done=Done.");
	return 0;
}

TWPartition* TWPartitionManager::Find_Next_Storage(string Path, bool Exclude_Data_Media) {
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
		if (Exclude_Data_Media && (*iter)->Has_Data_Media) {
			// do nothing, do not return this type of partition
		} else if ((*iter)->Is_Storage && (*iter)->Is_Present) {
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

	if (TWFunc::write_to_file(Lun_File, Part->Actual_Block_Device)) {
		LOGERR("Unable to write to ums lunfile '%s': (%s)\n", Lun_File.c_str(), strerror(errno));
		return false;
	}
	return true;
}

int TWPartitionManager::usb_storage_enable(void) {
	char lun_file[255];
	bool has_multiple_lun = false;

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
			TWPartition* Mount = Find_Next_Storage("", true);
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
		Mount1 = Find_Next_Storage("", true);
		if (Mount1) {
			if (!Open_Lun_File(Mount1->Mount_Point, lun_file)) {
				goto error_handle;
			}
			sprintf(lun_file, CUSTOM_LUN_FILE, 1);
			Mount2 = Find_Next_Storage(Mount1->Mount_Point, true);
			if (Mount2 && Mount2->Mount_Point != Mount1->Mount_Point) {
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
		ret = TWFunc::write_to_file(lun_file, str);
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

	UnMount_By_Path(Get_Android_Root_Path(), true);
	if (!datamedia)
		UnMount_By_Path("/data", true);

	if (Boot_Partition != NULL && Boot_Partition->Can_Be_Mounted)
		Boot_Partition->UnMount(true);
}

int TWPartitionManager::Partition_SDCard(void) {
	char temp[255];
	string Storage_Path, Command, Device, fat_str, ext_str, start_loc, end_loc, ext_format, sd_path, tmpdevice;
	int ext, swap, total_size = 0, fat_size;

	gui_msg("start_partition_sd=Partitioning SD Card...");

	// Locate and validate device to partition
	TWPartition* SDCard = Find_Partition_By_Path(DataManager::GetCurrentStoragePath());

	if (SDCard->Is_Adopted_Storage)
		SDCard->Revert_Adopted();

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
	Command = "sgdisk  --new=0:0:" + fat_str + " --change-name=0:\"Microsoft basic data\" --typecode=0:EBD0A0A2-B9E5-4433-87C0-68B6B72699C7 " + Device;
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
			if ((*iter)->Can_Be_Mounted) {
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
				struct PartitionList part;
				if (restore_path.compare("ADB_Backup") == 0) {
					part.Display_Name = "ADB Backup";
					part.Mount_Point = "ADB Backup";
					part.selected = 1;
					Partition_List->push_back(part);
					break;
				}
				if ((restore_part = Find_Partition_By_Path(restore_path)) != NULL) {
					if ((restore_part->Backup_Name == "recovery" && !restore_part->Can_Be_Backed_Up) || restore_part->Is_SubPartition) {
						// Don't allow restore of recovery (causes problems on some devices)
						// Don't add subpartitions to the list of items
					} else {
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
		if (DataManager::GetIntValue("tw_has_repack_tools") != 0 && DataManager::GetIntValue("tw_has_boot_slots") != 0) {
			TWPartition* boot = Find_Partition_By_Path("/boot");
			if (boot) {
				// Allow flashing kernels and ramdisks
				struct PartitionList repack_ramdisk;
				repack_ramdisk.Display_Name = gui_lookup("install_twrp_ramdisk", "Install Recovery Ramdisk");
				repack_ramdisk.Mount_Point = "/repack_ramdisk";
				repack_ramdisk.selected = 0;
				Partition_List->push_back(repack_ramdisk);
				/*struct PartitionList repack_kernel; For now let's leave repacking kernels under advanced only
				repack_kernel.Display_Name = gui_lookup("install_kernel", "Install Kernel");
				repack_kernel.Mount_Point = "/repack_kernel";
				repack_kernel.selected = 0;
				Partition_List->push_back(repack_kernel);*/
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
	std::string Temp;
	std::string cacheDir = TWFunc::get_cache_dir();

	if (cacheDir.empty()) {
		LOGINFO("Unable to find cache directory\n");
		return;
	}

	std::string storageFstab = TWFunc::get_cache_dir() + "recovery/storage.fstab";
	FILE *fp = fopen(storageFstab.c_str(), "w");

	if (fp == NULL) {
		gui_msg(Msg(msg::kError, "unable_to_open=Unable to open '{1}'.")(storageFstab));
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
		if (!(*iter)->Is_Storage)
			continue;

		if ((*iter)->Is_Settings_Storage)
			return *iter;

		if (!res)
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

	int mtppipe[2];

	if (pipe(mtppipe) < 0) {
		LOGERR("Error creating MTP pipe\n");
		return false;
	}

	char old_value[PROPERTY_VALUE_MAX];
	property_get("sys.usb.config", old_value, "");
	if (strcmp(old_value, "mtp,adb") != 0) {
		char vendor[PROPERTY_VALUE_MAX];
		char product[PROPERTY_VALUE_MAX];
		property_set("sys.usb.config", "none");
		property_get("usb.vendor", vendor, "18D1");
		property_get("usb.product.mtpadb", product, "4EE2");
		string vendorstr = vendor;
		string productstr = product;
		TWFunc::write_to_file("/sys/class/android_usb/android0/idVendor", vendorstr);
		TWFunc::write_to_file("/sys/class/android_usb/android0/idProduct", productstr);
		property_set("sys.usb.config", "mtp,adb");
	}
	/* To enable MTP debug, use the twrp command line feature:
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
	property_set("sys.usb.ffs.mtp.ready", "0");
	property_get("sys.usb.config", old_value, "");
	if (strcmp(old_value, "adb") != 0) {
		char vendor[PROPERTY_VALUE_MAX];
		char product[PROPERTY_VALUE_MAX];
		property_set("sys.usb.config", "none");
		property_get("usb.vendor", vendor, "18D1");
		property_get("usb.product.adb", product, "D001");
		string vendorstr = vendor;
		string productstr = product;
		TWFunc::write_to_file("/sys/class/android_usb/android0/idVendor", vendorstr);
		TWFunc::write_to_file("/sys/class/android_usb/android0/idProduct", productstr);
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
			if (Part->Storage_Path.size() >= sizeof(mtp_message.path)) {
				LOGERR("Storage path '%s' too large for mtpmsg\n", Part->Storage_Path.c_str());
				return false;
			}
			strcpy(mtp_message.path, Part->Storage_Path.c_str());
			if (Part->Storage_Name.size() >= sizeof(mtp_message.display)) {
				LOGERR("Storage name '%s' too large for mtpmsg\n", Part->Storage_Name.c_str());
				return false;
			}
			strcpy(mtp_message.display, Part->Storage_Name.c_str());
			mtp_message.maxFileSize = Part->Get_Max_FileSize();
			LOGINFO("sending message to add %i '%s' '%s'\n", mtp_message.storage_id, mtp_message.path, mtp_message.display);
			if (write(mtp_write_fd, &mtp_message, sizeof(mtp_message)) <= 0) {
				LOGINFO("error sending message to add storage %i\n", Part->MTP_Storage_ID);
				return false;
			} else {
				LOGINFO("Message sent, add storage ID: %i '%s'\n", Part->MTP_Storage_ID, mtp_message.path);
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

bool TWPartitionManager::Flash_Image(string& path, string& filename) {
	int partition_count = 0;
	TWPartition* flash_part = NULL;
	string Flash_List, flash_path, full_filename;
	size_t start_pos = 0, end_pos = 0;

	full_filename = path + "/" + filename;

	gui_msg("image_flash_start=[IMAGE FLASH STARTED]");
	gui_msg(Msg("img_to_flash=Image to flash: '{1}'")(full_filename));

	if (!TWFunc::Path_Exists(full_filename)) {
		if (!Mount_By_Path(full_filename, true)) {
			return false;
		}
		if (!TWFunc::Path_Exists(full_filename)) {
			gui_msg(Msg(msg::kError, "unable_to_locate=Unable to locate {1}.")(full_filename));
			return false;
		}
	}

	DataManager::GetValue("tw_flash_partition", Flash_List);
	Repack_Type repack = REPLACE_NONE;
	if (Flash_List == "/repack_ramdisk;") {
		repack = REPLACE_RAMDISK;
	} else if (Flash_List == "/repack_kernel;") {
		repack = REPLACE_KERNEL;
	}
	if (repack != REPLACE_NONE) {
		Repack_Options_struct Repack_Options;
		Repack_Options.Type = repack;
		Repack_Options.Disable_Verity = false;
		Repack_Options.Disable_Force_Encrypt = false;
		Repack_Options.Backup_First = DataManager::GetIntValue("tw_repack_backup_first") != 0;
		return Repack_Images(full_filename, Repack_Options);
	}
	PartitionSettings part_settings;
	part_settings.Backup_Folder = path;
	unsigned long long total_bytes = TWFunc::Get_File_Size(full_filename);
	ProgressTracking progress(total_bytes);
	part_settings.progress = &progress;
	part_settings.adbbackup = false;
	part_settings.PM_Method = PM_RESTORE;
	gui_msg("calc_restore=Calculating restore details...");
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
		flash_part->Backup_FileName = filename;
		if (!flash_part->Flash_Image(&part_settings))
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
		if (part->Is_Adopted_Storage) {
			part->Display_Name = part->Display_Name + " - " + gui_lookup("data", "Data");
			part->Backup_Display_Name = part->Display_Name;
			part->Storage_Name = part->Storage_Name + " - " + gui_lookup("adopted_storage", "Adopted Storage");
		} else {
			part->Display_Name = gui_lookup(resource_name, default_value);
			part->Backup_Display_Name = part->Display_Name;
		}
	}
}

void TWPartitionManager::Translate_Partition(const char* path, const char* resource_name, const char* default_value, const char* storage_resource_name, const char* storage_default_value) {
	TWPartition* part = PartitionManager.Find_Partition_By_Path(path);
	if (part) {
		if (part->Is_Adopted_Storage) {
			part->Backup_Display_Name = part->Display_Name + " - " + gui_lookup("data_backup", "Data (excl. storage)");
			part->Display_Name = part->Display_Name + " - " + gui_lookup("data", "Data");
			part->Storage_Name = part->Storage_Name + " - " + gui_lookup("adopted_storage", "Adopted Storage");
		} else {
			part->Display_Name = gui_lookup(resource_name, default_value);
			part->Backup_Display_Name = part->Display_Name;
			if (part->Is_Storage)
				part->Storage_Name = gui_lookup(storage_resource_name, storage_default_value);
		}
	}
}

void TWPartitionManager::Translate_Partition(const char* path, const char* resource_name, const char* default_value, const char* storage_resource_name, const char* storage_default_value, const char* backup_name, const char* backup_default) {
	TWPartition* part = PartitionManager.Find_Partition_By_Path(path);
	if (part) {
		if (part->Is_Adopted_Storage) {
			part->Backup_Display_Name = part->Display_Name + " - " + gui_lookup(backup_name, backup_default);
			part->Display_Name = part->Display_Name + " - " + gui_lookup("data", "Data");
			part->Storage_Name = part->Storage_Name + " - " + gui_lookup("adopted_storage", "Adopted Storage");
		} else {
			part->Display_Name = gui_lookup(resource_name, default_value);
			part->Backup_Display_Name = gui_lookup(backup_name, backup_default);
			if (part->Is_Storage)
				part->Storage_Name = gui_lookup(storage_resource_name, storage_default_value);
		}
	}
}

void TWPartitionManager::Translate_Partition_Display_Names() {
	LOGINFO("Translating partition display names\n");
	Translate_Partition("/system", "system", "System");
	Translate_Partition("/system_image", "system_image", "System Image");
	Translate_Partition("/vendor", "vendor", "Vendor");
	Translate_Partition("/vendor_image", "vendor_image", "Vendor Image");
	Translate_Partition("/cache", "cache", "Cache");
	Translate_Partition("/boot", "boot", "Boot");
	Translate_Partition("/recovery", "recovery", "Recovery");
	if (!datamedia) {
		Translate_Partition("/data", "data", "Data", "internal", "Internal Storage");
		Translate_Partition("/sdcard", "sdcard", "SDCard", "sdcard", "SDCard");
		Translate_Partition("/internal_sd", "sdcard", "SDCard", "sdcard", "SDCard");
		Translate_Partition("/internal_sdcard", "sdcard", "SDCard", "sdcard", "SDCard");
		Translate_Partition("/emmc", "sdcard", "SDCard", "sdcard", "SDCard");
	} else {
		Translate_Partition("/data", "data", "Data", "internal", "Internal Storage", "data_backup", "Data (excl. storage)");
	}
	Translate_Partition("/external_sd", "microsd", "Micro SDCard", "microsd", "Micro SDCard", "data_backup", "Data (excl. storage)");
	Translate_Partition("/external_sdcard", "microsd", "Micro SDCard", "microsd", "Micro SDCard", "data_backup", "Data (excl. storage)");
	Translate_Partition("/usb-otg", "usbotg", "USB OTG", "usbotg", "USB OTG");
	Translate_Partition("/sd-ext", "sdext", "SD-EXT");

	// Android secure is a special case
	TWPartition* part = PartitionManager.Find_Partition_By_Path("/and-sec");
	if (part)
		part->Backup_Display_Name = gui_lookup("android_secure", "Android Secure");

	std::vector<TWPartition*>::iterator sysfs;
	for (sysfs = Partitions.begin(); sysfs != Partitions.end(); sysfs++) {
		if (!(*sysfs)->Sysfs_Entry.empty()) {
			Translate_Partition((*sysfs)->Mount_Point.c_str(), "autostorage", "Storage", "autostorage", "Storage");
		}
	}

	// This updates the text on all of the storage selection buttons in the GUI
	DataManager::SetBackupFolder();
}

bool TWPartitionManager::Decrypt_Adopted() {
#ifdef TW_INCLUDE_CRYPTO
	bool ret = false;
	if (!Mount_By_Path("/data", false)) {
		LOGERR("Cannot decrypt adopted storage because /data will not mount\n");
		return false;
	}
	LOGINFO("Decrypt adopted storage starting\n");
	char* xmlFile = PageManager::LoadFileToBuffer("/data/system/storage.xml", NULL);
	xml_document<> *doc = NULL;
	xml_node<>* volumes = NULL;
	string Primary_Storage_UUID = "";
	if (xmlFile != NULL) {
		LOGINFO("successfully loaded storage.xml\n");
		doc = new xml_document<>();
		doc->parse<0>(xmlFile);
		volumes = doc->first_node("volumes");
		if (volumes) {
			xml_attribute<>* psuuid = volumes->first_attribute("primaryStorageUuid");
			if (psuuid) {
				Primary_Storage_UUID = psuuid->value();
			}
		}
	} else {
		LOGINFO("No /data/system/storage.xml for adopted storage\n");
		return false;
	}
	std::vector<TWPartition*>::iterator adopt;
	for (adopt = Partitions.begin(); adopt != Partitions.end(); adopt++) {
		if ((*adopt)->Removable && (*adopt)->Is_Present) {
			if ((*adopt)->Decrypt_Adopted() == 0) {
				ret = true;
				if (volumes) {
					xml_node<>* volume = volumes->first_node("volume");
					while (volume) {
						xml_attribute<>* guid = volume->first_attribute("partGuid");
						if (guid) {
							string GUID = (*adopt)->Adopted_GUID.c_str();
							GUID.insert(8, "-");
							GUID.insert(13, "-");
							GUID.insert(18, "-");
							GUID.insert(23, "-");

							if (strcasecmp(GUID.c_str(), guid->value()) == 0) {
								xml_attribute<>* attr = volume->first_attribute("nickname");
								if (attr && attr->value() && strlen(attr->value()) > 0) {
									(*adopt)->Storage_Name = attr->value();
									(*adopt)->Display_Name = (*adopt)->Storage_Name;
									(*adopt)->Backup_Display_Name = (*adopt)->Storage_Name;
									LOGINFO("storage name from storage.xml is '%s'\n", attr->value());
								}
								attr = volume->first_attribute("fsUuid");
								if (attr && !Primary_Storage_UUID.empty() && strcmp(Primary_Storage_UUID.c_str(), attr->value()) == 0) {
									TWPartition* Dat = Find_Partition_By_Path("/data");
									if (Dat) {
										LOGINFO("Internal storage is found on adopted storage '%s'\n", (*adopt)->Display_Name.c_str());
										LOGINFO("Changing '%s' to point to '%s'\n", Dat->Symlink_Mount_Point.c_str(), (*adopt)->Storage_Path.c_str());
										(*adopt)->Symlink_Mount_Point = Dat->Symlink_Mount_Point;
										Dat->Symlink_Mount_Point = "";
										// Toggle mounts to ensure that the symlink mount point (probably /sdcard) is mounted to the right location
										Dat->UnMount(false);
										Dat->Mount(false);
										(*adopt)->UnMount(false);
										(*adopt)->Mount(false);
									}
								}
								break;
							}
						}
						volume = volume->next_sibling("volume");
					}
				}
				Update_System_Details();
				Output_Partition((*adopt));
			}
		}
	}
	if (xmlFile) {
		doc->clear();
		delete doc;
		free(xmlFile);
	}
	return ret;
#else
	LOGINFO("Decrypt_Adopted: no crypto support\n");
	return false;
#endif
}

void TWPartitionManager::Remove_Partition_By_Path(string Path) {
	std::vector<TWPartition*>::iterator iter;
	string Local_Path = TWFunc::Get_Root_Path(Path);

	for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		if ((*iter)->Mount_Point == Local_Path || (!(*iter)->Symlink_Mount_Point.empty() && (*iter)->Symlink_Mount_Point == Local_Path)) {
			LOGINFO("Found and erasing '%s' from partition list\n", Local_Path.c_str());
			Partitions.erase(iter);
			return;
		}
	}
}

void TWPartitionManager::Set_Active_Slot(const string& Slot) {
	if (Slot != "A" && Slot != "B") {
		LOGERR("Set_Active_Slot invalid slot '%s'\n", Slot.c_str());
		return;
	}
	if (Active_Slot_Display == Slot)
		return;
	LOGINFO("Setting active slot %s\n", Slot.c_str());
#ifdef AB_OTA_UPDATER
	if (!Active_Slot_Display.empty()) {
		const hw_module_t *hw_module;
		boot_control_module_t *module;
		int ret;
		ret = hw_get_module("bootctrl", &hw_module);
		if (ret != 0) {
			LOGERR("Error getting bootctrl module.\n");
		} else {
			module = (boot_control_module_t*) hw_module;
			module->init(module);
			int slot_number = 0;
			if (Slot == "B")
				slot_number = 1;
			if (module->setActiveBootSlot(module, slot_number))
				gui_msg(Msg(msg::kError, "unable_set_boot_slot=Error changing bootloader boot slot to {1}")(Slot));
		}
		DataManager::SetValue("tw_active_slot", Slot); // Doing this outside of this if block may result in a seg fault because the DataManager may not be ready yet
	}
#else
	LOGERR("Boot slot feature not present\n");
#endif
	Active_Slot_Display = Slot;
	if (Fstab_Processed())
		Update_System_Details();
}
string TWPartitionManager::Get_Active_Slot_Suffix() {
	if (Active_Slot_Display == "A")
		return "_a";
	return "_b";
}
string TWPartitionManager::Get_Active_Slot_Display() {
	return Active_Slot_Display;
}

string TWPartitionManager::Get_Android_Root_Path() {
	std::string Android_Root = getenv("ANDROID_ROOT");
	if (Android_Root == "")
		Android_Root = "/system";
	return Android_Root;
}

void TWPartitionManager::Remove_Uevent_Devices(const string& Mount_Point) {
	std::vector<TWPartition*>::iterator iter;

	for (iter = Partitions.begin(); iter != Partitions.end(); ) {
		if ((*iter)->Is_SubPartition && (*iter)->SubPartition_Of == Mount_Point) {
			TWPartition *part = *iter;
			LOGINFO("%s was removed by uevent data\n", (*iter)->Mount_Point.c_str());
			(*iter)->UnMount(false);
			rmdir((*iter)->Mount_Point.c_str());
			iter = Partitions.erase(iter);
			delete part;
		} else {
			iter++;
		}
	}
}

void TWPartitionManager::Handle_Uevent(const Uevent_Block_Data& uevent_data) {
	std::vector<TWPartition*>::iterator iter;

	for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		if (!(*iter)->Sysfs_Entry.empty()) {
			string device;
			size_t wildcard = (*iter)->Sysfs_Entry.find("*");
			if (wildcard != string::npos) {
				device = (*iter)->Sysfs_Entry.substr(0, wildcard);
			} else {
				device = (*iter)->Sysfs_Entry;
			}
			if (device == uevent_data.sysfs_path.substr(0, device.size())) {
				// Found a match
				if (uevent_data.action == "add") {
					(*iter)->Primary_Block_Device = "/dev/block/" + uevent_data.block_device;
					(*iter)->Alternate_Block_Device = (*iter)->Primary_Block_Device;
					(*iter)->Is_Present = true;
					LOGINFO("Found a match '%s' '%s'\n", uevent_data.block_device.c_str(), device.c_str());
					if (!Decrypt_Adopted()) {
						LOGINFO("No adopted storage so finding actual block device\n");
						(*iter)->Find_Actual_Block_Device();
					}
					return;
				} else if (uevent_data.action == "remove") {
					(*iter)->Is_Present = false;
					(*iter)->Primary_Block_Device = "";
					(*iter)->Actual_Block_Device = "";
					Remove_Uevent_Devices((*iter)->Mount_Point);
					return;
				}
			}
		}
	}
	LOGINFO("Found no matching fstab entry for uevent device '%s' - %s\n", uevent_data.sysfs_path.c_str(), uevent_data.action.c_str());
}

void TWPartitionManager::setup_uevent() {
	struct sockaddr_nl nls;

	if (uevent_pfd.fd >= 0) {
		LOGINFO("uevent already set up\n");
		return;
	}

	// Open hotplug event netlink socket
	memset(&nls,0,sizeof(struct sockaddr_nl));
	nls.nl_family = AF_NETLINK;
	nls.nl_pid = getpid();
	nls.nl_groups = -1;
	uevent_pfd.events = POLLIN;
	uevent_pfd.fd = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
	if (uevent_pfd.fd==-1) {
		LOGERR("uevent not root\n");
		return;
	}

	// Listen to netlink socket
	if (::bind(uevent_pfd.fd, (struct sockaddr *) &nls, sizeof(struct sockaddr_nl)) < 0) {
		LOGERR("Bind failed\n");
		return;
	}
	set_select_fd();
	Coldboot();
}

Uevent_Block_Data TWPartitionManager::get_event_block_values(char *buf, int len) {
	Uevent_Block_Data ret;
	ret.subsystem = "";
	char *ptr = buf;
	const char *end = buf + len;

	buf[len - 1] = '\0';
	while (ptr < end) {
		if (strncmp(ptr, "ACTION=", strlen("ACTION=")) == 0) {
			ptr += strlen("ACTION=");
			ret.action = ptr;
		} else if (strncmp(ptr, "SUBSYSTEM=", strlen("SUBSYSTEM=")) == 0) {
			ptr += strlen("SUBSYSTEM=");
			ret.subsystem = ptr;
		} else if (strncmp(ptr, "DEVTYPE=", strlen("DEVTYPE=")) == 0) {
			ptr += strlen("DEVTYPE=");
			ret.type = ptr;
		} else if (strncmp(ptr, "DEVPATH=", strlen("DEVPATH=")) == 0) {
			ptr += strlen("DEVPATH=");
			ret.sysfs_path += ptr;
		} else if (strncmp(ptr, "DEVNAME=", strlen("DEVNAME=")) == 0) {
			ptr += strlen("DEVNAME=");
			ret.block_device += ptr;
		} else if (strncmp(ptr, "MAJOR=", strlen("MAJOR=")) == 0) {
			ptr += strlen("MAJOR=");
			ret.major = atoi(ptr);
		} else if (strncmp(ptr, "MINOR=", strlen("MINOR=")) == 0) {
			ptr += strlen("MINOR=");
			ret.minor = atoi(ptr);
		}
		ptr += strlen(ptr) + 1;
	}
	return ret;
}

void TWPartitionManager::read_uevent() {
	char buf[1024];

	int len = recv(uevent_pfd.fd, buf, sizeof(buf), MSG_DONTWAIT);
	if (len == -1) {
		LOGERR("recv error on uevent\n");
		return;
	}
	/*int i = 0; // Print all uevent output for test /debug
	while (i<len) {
		printf("%s\n", buf+i);
		i += strlen(buf+i)+1;
	}*/
	Uevent_Block_Data uevent_data = get_event_block_values(buf, len);
	if (uevent_data.subsystem == "block" && uevent_data.type == "disk") {
		PartitionManager.Handle_Uevent(uevent_data);
	}
}

void TWPartitionManager::close_uevent() {
	if (uevent_pfd.fd > 0)
		close(uevent_pfd.fd);
	uevent_pfd.fd = -1;
}

void TWPartitionManager::Add_Partition(TWPartition* Part) {
	Partitions.push_back(Part);
}

void TWPartitionManager::Coldboot_Scan(std::vector<string> *sysfs_entries, const string& Path, int depth) {
	string Real_Path = Path;
	char real_path[PATH_MAX];
	if (realpath(Path.c_str(), &real_path[0])) {
		string Real_Path = real_path;
		std::vector<string>::iterator iter;
		for (iter = sysfs_entries->begin(); iter != sysfs_entries->end(); iter++) {
			if (Real_Path.find((*iter)) != string::npos) {
				string Write_Path = Real_Path + "/uevent";
				if (TWFunc::Path_Exists(Write_Path)) {
					const char* write_val = "add\n";
					TWFunc::write_to_file(Write_Path, write_val);
					break;
				}
			}
		}
	}

	DIR* d = opendir(Path.c_str());
	if (d != NULL) {
		struct dirent* de;
		while ((de = readdir(d)) != NULL) {
			if (de->d_name[0] == '.' || (de->d_type != DT_DIR && depth > 0))
				continue;
			if (strlen(de->d_name) >= 4 && (strncmp(de->d_name, "ram", 3) == 0 || strncmp(de->d_name, "loop", 4) == 0))
				continue;

			string item = Path + "/";
			item.append(de->d_name);
			Coldboot_Scan(sysfs_entries, item, depth + 1);
		}
		closedir(d);
	}
}

void TWPartitionManager::Coldboot() {
	std::vector<TWPartition*>::iterator iter;
	std::vector<string> sysfs_entries;

	for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		if (!(*iter)->Sysfs_Entry.empty()) {
			size_t wildcard_pos = (*iter)->Sysfs_Entry.find("*");
			if (wildcard_pos == string::npos)
				wildcard_pos = (*iter)->Sysfs_Entry.size();
			sysfs_entries.push_back((*iter)->Sysfs_Entry.substr(0, wildcard_pos));
		}
	}

	if (sysfs_entries.size() > 0)
		Coldboot_Scan(&sysfs_entries, "/sys/block", 0);
}

bool TWPartitionManager::Prepare_Empty_Folder(const std::string& Folder) {
	if (TWFunc::Path_Exists(Folder))
		TWFunc::removeDir(Folder, false);
	return TWFunc::Recursive_Mkdir(Folder);
}

bool TWPartitionManager::Prepare_Repack(TWPartition* Part, const std::string& Temp_Folder_Destination, const bool Create_Backup, const std::string& Backup_Name) {
	if (!Part) {
		LOGERR("Partition was null!\n");
		return false;
	}
	if (!Prepare_Empty_Folder(Temp_Folder_Destination))
		return false;
	std::string target_image = Temp_Folder_Destination + "boot.img";
	PartitionSettings part_settings;
	part_settings.Part = Part;
	if (Create_Backup) {
		if (Check_Backup_Name(Backup_Name, true, false) != 0)
			return false;
		DataManager::GetValue(TW_BACKUPS_FOLDER_VAR, part_settings.Backup_Folder);
		part_settings.Backup_Folder = part_settings.Backup_Folder + "/" + TWFunc::Get_Current_Date() + " " + Backup_Name + "/";
		if (!TWFunc::Recursive_Mkdir(part_settings.Backup_Folder))
			return false;
	} else
		part_settings.Backup_Folder = Temp_Folder_Destination;
	part_settings.adbbackup = false;
	part_settings.generate_digest = false;
	part_settings.generate_md5 = false;
	part_settings.PM_Method = PM_BACKUP;
	part_settings.progress = NULL;
	pid_t not_a_pid = 0;
	if (!Part->Backup(&part_settings, &not_a_pid))
		return false;
	std::string backed_up_image = part_settings.Backup_Folder;
	backed_up_image += Part->Backup_FileName;
	target_image = Temp_Folder_Destination + "boot.img";
	if (Create_Backup) {
		std::string source = part_settings.Backup_Folder + Part->Backup_FileName;
		if (TWFunc::copy_file(source, target_image, 0644) != 0) {
			LOGERR("Failed to copy backup file '%s' to temp folder target '%s'\n", source.c_str(), target_image.c_str());
			return false;
		}
	} else {
		if (rename(backed_up_image.c_str(), target_image.c_str()) != 0) {
			LOGERR("Failed to rename '%s' to '%s'\n", backed_up_image.c_str(), target_image.c_str());
			return false;
		}
	}
	return Prepare_Repack(target_image, Temp_Folder_Destination, false, false);
}

bool TWPartitionManager::Prepare_Repack(const std::string& Source_Path, const std::string& Temp_Folder_Destination, const bool Copy_Source, const bool Create_Destination) {
	if (Create_Destination) {
		if (!Prepare_Empty_Folder(Temp_Folder_Destination))
			return false;
	}
	if (Copy_Source) {
		std::string destination = Temp_Folder_Destination + "/boot.img";
		if (TWFunc::copy_file(Source_Path, destination, 0644))
			return false;
	}
	std::string command = "cd " + Temp_Folder_Destination + " && /sbin/magiskboot --unpack -h '" + Source_Path +"'";
	if (TWFunc::Exec_Cmd(command) != 0) {
		LOGINFO("Error unpacking %s!\n", Source_Path.c_str());
		gui_msg(Msg(msg::kError, "unpack_error=Error unpacking image."));
		return false;
	}
	return true;
}

bool TWPartitionManager::Repack_Images(const std::string& Target_Image, const struct Repack_Options_struct& Repack_Options) {
	if (!TWFunc::Path_Exists("/sbin/magiskboot")) {
		LOGERR("Image repacking tool not present in this TWRP build!");
		return false;
	}
	DataManager::SetProgress(0);
	TWPartition* part = PartitionManager.Find_Partition_By_Path("/boot");
	if (part)
		gui_msg(Msg("unpacking_image=Unpacking {1}...")(part->Display_Name));
	else {
		gui_msg(Msg(msg::kError, "unable_to_locate=Unable to locate {1}.")("/boot"));
		return false;
	}
	if (!PartitionManager.Prepare_Repack(part, REPACK_ORIG_DIR, Repack_Options.Backup_First, gui_lookup("repack", "Repack")))
		return false;
	DataManager::SetProgress(.25);
	gui_msg(Msg("unpacking_image=Unpacking {1}...")(Target_Image));
	if (!PartitionManager.Prepare_Repack(Target_Image, REPACK_NEW_DIR, true))
		return false;
	DataManager::SetProgress(.5);
	gui_msg(Msg("repacking_image=Repacking {1}...")(part->Display_Name));
	std::string path = REPACK_NEW_DIR;
	if (Repack_Options.Type == REPLACE_KERNEL) {
		// When we replace the kernel, what we really do is copy the boot partition ramdisk into the new image's folder
		if (TWFunc::copy_file(REPACK_ORIG_DIR "ramdisk.cpio", REPACK_NEW_DIR "ramdisk.cpio", 0644)) {
			LOGERR("Failed to copy ramdisk\n");
			return false;
		}
	} else if (Repack_Options.Type == REPLACE_RAMDISK) {
		// Repack the ramdisk
		if (TWFunc::copy_file(REPACK_NEW_DIR "ramdisk.cpio", REPACK_ORIG_DIR "ramdisk.cpio", 0644)) {
			LOGERR("Failed to copy ramdisk\n");
			return false;
		}
		path = REPACK_ORIG_DIR;
	} else {
		LOGERR("Invalid repacking options specified\n");
		return false;
	}
	if (Repack_Options.Disable_Verity)
		LOGERR("Disabling verity is not implemented yet\n");
	if (Repack_Options.Disable_Force_Encrypt)
		LOGERR("Disabling force encrypt is not implemented yet\n");
	std::string command = "cd " + path + " && /sbin/magiskboot --repack " + path + "boot.img";
	if (TWFunc::Exec_Cmd(command) != 0) {
		gui_msg(Msg(msg::kError, "repack_error=Error repacking image."));
		return false;
	}
	DataManager::SetProgress(.75);
	std::string file = "new-boot.img";
	DataManager::SetValue("tw_flash_partition", "/boot;");
	if (!PartitionManager.Flash_Image(path, file)) {
		LOGINFO("Error flashing new image\n");
		return false;
	}
	DataManager::SetProgress(1);
	TWFunc::removeDir(REPACK_ORIG_DIR, false);
	if (part->SlotSelect && Repack_Options.Type == REPLACE_RAMDISK) {
		LOGINFO("Switching slots to flash ramdisk to both partitions\n");
		string Current_Slot = Get_Active_Slot_Display();
		if (Current_Slot == "A")
			Set_Active_Slot("B");
		else
			Set_Active_Slot("A");
		DataManager::SetProgress(.25);
		if (!PartitionManager.Prepare_Repack(part, REPACK_ORIG_DIR, Repack_Options.Backup_First, gui_lookup("repack", "Repack")))
			return false;
		if (TWFunc::copy_file(REPACK_NEW_DIR "ramdisk.cpio", REPACK_ORIG_DIR "ramdisk.cpio", 0644)) {
			LOGERR("Failed to copy ramdisk\n");
			return false;
		}
		path = REPACK_ORIG_DIR;
		command = "cd " + path + " && /sbin/magiskboot --repack " + path + "boot.img";
		if (TWFunc::Exec_Cmd(command) != 0) {
			gui_msg(Msg(msg::kError, "repack_error=Error repacking image."));
			return false;
		}
		DataManager::SetProgress(.75);
		std::string file = "new-boot.img";
		DataManager::SetValue("tw_flash_partition", "/boot;");
		if (!PartitionManager.Flash_Image(path, file)) {
			LOGINFO("Error flashing new image\n");
			return false;
		}
		DataManager::SetProgress(1);
		TWFunc::removeDir(REPACK_ORIG_DIR, false);
		Set_Active_Slot(Current_Slot);
	}
	TWFunc::removeDir(REPACK_NEW_DIR, false);
	return true;
}
