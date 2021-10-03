/*
	Copyright 2013 to 2020 TeamWin
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

#include <string>

#include "data.hpp"
#include "partitions.hpp"
#include "twrp-functions.hpp"
#include "twrpRepacker.hpp"
#include "twcommon.h"
#include "variables.h"
#include "gui/gui.hpp"

bool twrpRepacker::Prepare_Empty_Folder(const std::string& Folder) {
	if (TWFunc::Path_Exists(Folder))
		TWFunc::removeDir(Folder, false);
	return TWFunc::Recursive_Mkdir(Folder);
}

bool twrpRepacker::Backup_Image_For_Repack(TWPartition* Part, const std::string& Temp_Folder_Destination,
										 const bool Create_Backup, const std::string& Backup_Name) {
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
		if (PartitionManager.Check_Backup_Name(Backup_Name, true, false) != 0)
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
	backed_up_image += Part->Get_Backup_FileName();
	target_image = Temp_Folder_Destination + "boot.img";
	if (Create_Backup) {
		std::string source = part_settings.Backup_Folder + Part->Get_Backup_FileName();
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
	original_ramdisk_format = Unpack_Image(target_image, Temp_Folder_Destination, false, false);
	return !original_ramdisk_format.empty();
}

std::string twrpRepacker::Unpack_Image(const std::string& Source_Path, const std::string& Temp_Folder_Destination,
										const bool Copy_Source, const bool Create_Destination) {
	std::string txt_to_find = "RAMDISK_FMT";
	if (Create_Destination) {
		if (!Prepare_Empty_Folder(Temp_Folder_Destination))
			return std::string();
	}
	if (Copy_Source) {
		std::string destination = Temp_Folder_Destination + "/boot.img";
		if (TWFunc::copy_file(Source_Path, destination, 0644))
			return std::string();
	}
	std::string command = "cd " + Temp_Folder_Destination + " && /sbin/magiskboot unpack -h ";
	command = command + "'" + Source_Path +"'";

	std::string magisk_unpack_output;
	int ret;
	if ((ret = TWFunc::Exec_Cmd(command, magisk_unpack_output, true)) != 0) {
		LOGINFO("Error unpacking %s, ret: %d!\n", Source_Path.c_str(), ret);
		gui_msg(Msg(msg::kError, "unpack_error=Error unpacking image."));
		return std::string();
	}
	size_t pos = magisk_unpack_output.find(txt_to_find) + txt_to_find.size();
	std::string ramdisk_format = magisk_unpack_output.substr(pos, magisk_unpack_output.size() - 1);
	ramdisk_format.erase(std::remove(ramdisk_format.begin(), ramdisk_format.end(), '['), ramdisk_format.end());
	ramdisk_format.erase(std::remove(ramdisk_format.begin(), ramdisk_format.end(), ']'), ramdisk_format.end());
	ramdisk_format.erase(std::remove(ramdisk_format.begin(), ramdisk_format.end(), ' '), ramdisk_format.end());
	ramdisk_format.erase(std::remove(ramdisk_format.begin(), ramdisk_format.end(), '\n'), ramdisk_format.end());
	return ramdisk_format;
}

bool twrpRepacker::Repack_Image_And_Flash(const std::string& Target_Image, const struct Repack_Options_struct& Repack_Options) {
	bool recompress = false;

	if (!TWFunc::Path_Exists("/sbin/magiskboot")) {
		LOGERR("Image repacking tool not present in this TWRP build!");
		return false;
	}
	DataManager::SetProgress(0);
	PartitionManager.Update_System_Details();
	TWPartition* part = PartitionManager.Find_Partition_By_Path("/boot");
	if (part)
		gui_msg(Msg("unpacking_image=Unpacking {1}...")(part->Get_Display_Name()));
	else {
		gui_msg(Msg(msg::kError, "unable_to_locate=Unable to locate {1}.")("/boot"));
		return false;
	}
	if (!Backup_Image_For_Repack(part, REPACK_ORIG_DIR, Repack_Options.Backup_First, gui_lookup("repack", "Repack")))
		return false;
	DataManager::SetProgress(.25);
	if (Repack_Options.Type == REPLACE_RAMDISK_UNPACKED) {
		if (!Prepare_Empty_Folder(REPACK_NEW_DIR))
			return false;
		image_ramdisk_format = "gzip";
	} else {
		gui_msg(Msg("unpacking_image=Unpacking {1}...")(Target_Image));
		image_ramdisk_format = Unpack_Image(Target_Image, REPACK_NEW_DIR, true);
	}
	if (image_ramdisk_format.empty())
		return false;
	DataManager::SetProgress(.5);
	gui_msg(Msg("repacking_image=Repacking {1}...")(part->Get_Display_Name()));
	std::string path = REPACK_NEW_DIR;
	if (Repack_Options.Type == REPLACE_KERNEL) {
		// When we replace the kernel, what we really do is copy the boot partition ramdisk into the new image's folder
		if (TWFunc::copy_file(REPACK_ORIG_DIR "ramdisk.cpio", REPACK_NEW_DIR "ramdisk.cpio", 0644)) {
			LOGERR("Failed to copy ramdisk\n");
			return false;
		}
	} else if (Repack_Options.Type == REPLACE_RAMDISK_UNPACKED) {
			if (TWFunc::copy_file(Target_Image, REPACK_ORIG_DIR "ramdisk.cpio", 0644)) {
				LOGERR("Failed to copy ramdisk\n");
				return false;
			}
			if (TWFunc::copy_file(Target_Image, REPACK_NEW_DIR "ramdisk.cpio", 0644)) {
				LOGERR("Failed to copy ramdisk\n");
				return false;
			}
		path = REPACK_ORIG_DIR;
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
	std::string command = "cd " + path + " && /sbin/magiskboot repack ";
	if (original_ramdisk_format != image_ramdisk_format) {
		recompress = true;
	}

	command += path + "boot.img";

	std::string orig_compressed_image(REPACK_ORIG_DIR);
	orig_compressed_image += "ramdisk.cpio";
	std::string copy_compressed_image(REPACK_ORIG_DIR);
	copy_compressed_image += "ramdisk-1.cpio";

	if (recompress) {
		std::string decompress_cmd = "/sbin/magiskboot decompress " + orig_compressed_image + " " + copy_compressed_image;
		if (TWFunc::Exec_Cmd(decompress_cmd) != 0) {
			gui_msg(Msg(msg::kError, "repack_error=Error repacking image."));
			return false;
		}
		std::rename(copy_compressed_image.c_str(), orig_compressed_image.c_str());
	}

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
	if (part->Is_SlotSelect()) { if (Repack_Options.Type == REPLACE_RAMDISK || Repack_Options.Type == REPLACE_RAMDISK_UNPACKED) {
		LOGINFO("Switching slots to flash ramdisk to both partitions\n");
		string Current_Slot = PartitionManager.Get_Active_Slot_Display();
		if (Current_Slot == "A")
			PartitionManager.Override_Active_Slot("B");
		else
			PartitionManager.Override_Active_Slot("A");
		DataManager::SetProgress(.25);
		if (!Backup_Image_For_Repack(part, REPACK_ORIG_DIR, Repack_Options.Backup_First, gui_lookup("repack", "Repack")))
			return false;
		if (TWFunc::copy_file(REPACK_NEW_DIR "ramdisk.cpio", REPACK_ORIG_DIR "ramdisk.cpio", 0644)) {
			LOGERR("Failed to copy ramdisk\n");
			return false;
		}
		path = REPACK_ORIG_DIR;
		std::string command = "cd " + path + " && /sbin/magiskboot repack ";

		if (original_ramdisk_format != image_ramdisk_format) {
			recompress = true;
		}
		command += path + "boot.img";

		if (recompress) {
			std::string decompress_cmd = "/sbin/magiskboot decompress " + orig_compressed_image + " " + copy_compressed_image;
			if (TWFunc::Exec_Cmd(decompress_cmd) != 0) {
				gui_msg(Msg(msg::kError, "repack_error=Error repacking image."));
				return false;
			}
			std::rename(copy_compressed_image.c_str(), orig_compressed_image.c_str());
		}

		if (TWFunc::Exec_Cmd(command) != 0) {
			gui_msg(Msg(msg::kError, "repack_error=Error repacking image."));
			return false;
		}

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
	}}
	TWFunc::removeDir(REPACK_NEW_DIR, false);
	gui_msg(Msg(msg::kWarning, "repack_overwrite_warning=If device was previously rooted, then root has been overwritten and will need to be reinstalled."));
	return true;
}

bool twrpRepacker::Flash_Current_Twrp() {
if (!TWFunc::Path_Exists("/ramdisk-files.txt")) {
			LOGERR("can not find ramdisk-files.txt");
			return false;
		}
		Repack_Options_struct Repack_Options;
		Repack_Options.Disable_Verity = false;
		Repack_Options.Disable_Force_Encrypt = false;
		Repack_Options.Type = REPLACE_RAMDISK_UNPACKED;
		Repack_Options.Backup_First = DataManager::GetIntValue("tw_repack_backup_first") != 0;
		std::string verifyfiles = "cd / && sha256sum -c ramdisk-files.sha256sum";
		if (TWFunc::Exec_Cmd(verifyfiles) != 0) {
		gui_msg(Msg(msg::kError, "modified_ramdisk_error=ramdisk files have been modified, unable to create ramdisk to flash, fastboot boot twrp and try this option again or use the Install Recovery Ramdisk option."));
			return false;
		}
		std::string command = "cd / && /sbin/cpio -H newc -o < ramdisk-files.txt > /tmp/currentramdisk.cpio && /sbin/gzip -f /tmp/currentramdisk.cpio";
		if (TWFunc::Exec_Cmd(command) != 0) {
			gui_msg(Msg(msg::kError, "create_ramdisk_error=failed to create ramdisk to flash."));
			return false;
		}
		if (!Repack_Image_And_Flash("/tmp/currentramdisk.cpio.gz", Repack_Options))
			return false;
       else
       return true;
}
