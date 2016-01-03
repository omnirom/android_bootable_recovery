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
#include <iostream>
#include <fstream>
#include <sys/types.h>
#include <sys/wait.h>

#include "twrp-functions.hpp"
#include "partitions.hpp"
#include "twcommon.h"
#include "openrecoveryscript.hpp"
#include "variables.h"
#include "adb_install.h"
#include "data.hpp"
#include "adb_install.h"
#include "fuse_sideload.h"
#include "gui/gui.hpp"
extern "C" {
	#include "twinstall.h"
	#include "gui/gui.h"
	#include "cutils/properties.h"
	int TWinstall_zip(const char* path, int* wipe_cache);
}

OpenRecoveryScript::VoidFunction OpenRecoveryScript::call_after_cli_command;

#define SCRIPT_COMMAND_SIZE 512

int OpenRecoveryScript::check_for_script_file(void) {
	if (!PartitionManager.Mount_By_Path(SCRIPT_FILE_CACHE, false)) {
		LOGINFO("Unable to mount /cache for OpenRecoveryScript support.\n");
		gui_msg(Msg(msg::kError, "unable_to_mount=Unable to mount {1}")(SCRIPT_FILE_CACHE));
		return 0;
	}
	if (TWFunc::Path_Exists(SCRIPT_FILE_CACHE)) {
		LOGINFO("Script file found: '%s'\n", SCRIPT_FILE_CACHE);
		// Copy script file to /tmp
		TWFunc::copy_file(SCRIPT_FILE_CACHE, SCRIPT_FILE_TMP, 0755);
		// Delete the file from /cache
		unlink(SCRIPT_FILE_CACHE);
		return 1;
	}
	return 0;
}

int OpenRecoveryScript::copy_script_file(string filename) {
	if (TWFunc::Path_Exists(filename)) {
		LOGINFO("Script file found: '%s'\n", filename.c_str());
		if (filename == SCRIPT_FILE_TMP)
			return 1; // file is already in the right place
		// Copy script file to /tmp
		TWFunc::copy_file(filename, SCRIPT_FILE_TMP, 0755);
		// Delete the old file
		unlink(filename.c_str());
		return 1;
	}
	return 0;
}

int OpenRecoveryScript::run_script_file(void) {
	FILE *fp = fopen(SCRIPT_FILE_TMP, "r");
	int ret_val = 0, cindex, line_len, i, remove_nl, install_cmd = 0, sideload = 0;
	char script_line[SCRIPT_COMMAND_SIZE], command[SCRIPT_COMMAND_SIZE],
		 value[SCRIPT_COMMAND_SIZE], mount[SCRIPT_COMMAND_SIZE],
		 value1[SCRIPT_COMMAND_SIZE], value2[SCRIPT_COMMAND_SIZE];
	char *val_start, *tok;

	if (fp != NULL) {
		DataManager::SetValue(TW_SIMULATE_ACTIONS, 0);
		DataManager::SetValue("ui_progress", 0); // Reset the progress bar
		while (fgets(script_line, SCRIPT_COMMAND_SIZE, fp) != NULL && ret_val == 0) {
			cindex = 0;
			line_len = strlen(script_line);
			if (line_len < 2)
				continue; // there's a blank line or line is too short to contain a command
			//gui_print("script line: '%s'\n", script_line);
			for (i=0; i<line_len; i++) {
				if ((int)script_line[i] == 32) {
					cindex = i;
					i = line_len;
				}
			}
			memset(command, 0, sizeof(command));
			memset(value, 0, sizeof(value));
			if ((int)script_line[line_len - 1] == 10)
					remove_nl = 2;
				else
					remove_nl = 1;
			if (cindex != 0) {
				strncpy(command, script_line, cindex);
				LOGINFO("command is: '%s'\n", command);
				val_start = script_line;
				val_start += cindex + 1;
				if ((int) *val_start == 32)
					val_start++; //get rid of space
				if ((int) *val_start == 51)
					val_start++; //get rid of = at the beginning
				if ((int) *val_start == 32)
					val_start++; //get rid of space
				strncpy(value, val_start, line_len - cindex - remove_nl);
				LOGINFO("value is: '%s'\n", value);
			} else {
				strncpy(command, script_line, line_len - remove_nl + 1);
				gui_print("command is: '%s' and there is no value\n", command);
			}
			if (strcmp(command, "install") == 0) {
				// Install Zip
				DataManager::SetValue("tw_action_text2", "Installing Zip");
				PartitionManager.Mount_All_Storage();
				ret_val = Install_Command(value);
				install_cmd = -1;
			} else if (strcmp(command, "wipe") == 0) {
				// Wipe
				if (strcmp(value, "cache") == 0 || strcmp(value, "/cache") == 0) {
					PartitionManager.Wipe_By_Path("/cache");
				} else if (strcmp(value, "dalvik") == 0 || strcmp(value, "dalvick") == 0 || strcmp(value, "dalvikcache") == 0 || strcmp(value, "dalvickcache") == 0) {
					PartitionManager.Wipe_Dalvik_Cache();
				} else if (strcmp(value, "data") == 0 || strcmp(value, "/data") == 0 || strcmp(value, "factory") == 0 || strcmp(value, "factoryreset") == 0) {
					PartitionManager.Factory_Reset();
				} else {
					LOGERR("Error with wipe command value: '%s'\n", value);
					ret_val = 1;
				}
			} else if (strcmp(command, "backup") == 0) {
				// Backup
				DataManager::SetValue("tw_action_text2", gui_parse_text("{@backing}"));
				tok = strtok(value, " ");
				strcpy(value1, tok);
				tok = strtok(NULL, " ");
				if (tok != NULL) {
					memset(value2, 0, sizeof(value2));
					strcpy(value2, tok);
					line_len = strlen(tok);
					if ((int)value2[line_len - 1] == 10 || (int)value2[line_len - 1] == 13) {
						if ((int)value2[line_len - 1] == 10 || (int)value2[line_len - 1] == 13)
							remove_nl = 2;
						else
							remove_nl = 1;
					} else
						remove_nl = 0;
					strncpy(value2, tok, line_len - remove_nl);
					DataManager::SetValue(TW_BACKUP_NAME, value2);
					gui_msg(Msg("backup_folder=Backup folder set to '{1}'")(value2));
					if (PartitionManager.Check_Backup_Name(true) != 0) {
						ret_val = 1;
						continue;
					}
				} else {
					char empt[50];
					strcpy(empt, "(Current Date)");
					DataManager::SetValue(TW_BACKUP_NAME, empt);
				}
				ret_val = Backup_Command(value1);
			} else if (strcmp(command, "restore") == 0) {
				// Restore
				DataManager::SetValue("tw_action_text2", gui_parse_text("{@restore}"));
				PartitionManager.Mount_All_Storage();
				DataManager::SetValue(TW_SKIP_MD5_CHECK_VAR, 0);
				char folder_path[512], partitions[512];

				string val = value, restore_folder, restore_partitions;
				size_t pos = val.find_last_of(" ");
				if (pos == string::npos) {
					restore_folder = value;
					partitions[0] = '\0';
				} else {
					restore_folder = val.substr(0, pos);
					restore_partitions = val.substr(pos + 1, val.size() - pos - 1);
					strcpy(partitions, restore_partitions.c_str());
				}
				strcpy(folder_path, restore_folder.c_str());
				LOGINFO("Restore folder is: '%s' and partitions: '%s'\n", folder_path, partitions);
				gui_msg(Msg("restoring=Restoring {1}...")(folder_path));

				if (folder_path[0] != '/') {
					char backup_folder[512];
					string folder_var;
					std::vector<PartitionList> Storage_List;

					PartitionManager.Get_Partition_List("storage", &Storage_List);
					int listSize = Storage_List.size();
					for (int i = 0; i < listSize; i++) {
						if (PartitionManager.Is_Mounted_By_Path(Storage_List.at(i).Mount_Point)) {
							DataManager::SetValue("tw_storage_path", Storage_List.at(i).Mount_Point);
							DataManager::GetValue(TW_BACKUPS_FOLDER_VAR, folder_var);
							sprintf(backup_folder, "%s/%s", folder_var.c_str(), folder_path);
							if (TWFunc::Path_Exists(backup_folder)) {
								strcpy(folder_path, backup_folder);
								break;
							}
						}
					}
				} else {
					if (folder_path[strlen(folder_path) - 1] == '/')
						strcat(folder_path, ".");
					else
						strcat(folder_path, "/.");
				}
				if (!TWFunc::Path_Exists(folder_path)) {
					gui_msg(Msg(msg::kError, "locate_backup_err=Unable to locate backup '{1}'")(folder_path));
					ret_val = 1;
					continue;
				}
				DataManager::SetValue("tw_restore", folder_path);

				PartitionManager.Set_Restore_Files(folder_path);
				string Partition_List;
				int is_encrypted = 0;
				DataManager::GetValue("tw_restore_encrypted", is_encrypted);
				DataManager::GetValue("tw_restore_list", Partition_List);
				if (strlen(partitions) != 0) {
					string Restore_List;

					memset(value2, 0, sizeof(value2));
					strcpy(value2, partitions);
					gui_msg(Msg("set_restore_opt=Setting restore options: '{1}':")(value2));
					line_len = strlen(value2);
					for (i=0; i<line_len; i++) {
						if ((value2[i] == 'S' || value2[i] == 's') && Partition_List.find("/system;") != string::npos) {
							Restore_List += "/system;";
							gui_msg("system=System");
						} else if ((value2[i] == 'D' || value2[i] == 'd') && Partition_List.find("/data;") != string::npos) {
							Restore_List += "/data;";
							gui_msg("data=Data");
						} else if ((value2[i] == 'C' || value2[i] == 'c') && Partition_List.find("/cache;") != string::npos) {
							Restore_List += "/cache;";
							gui_msg("cache=Cache");
						} else if ((value2[i] == 'R' || value2[i] == 'r') && Partition_List.find("/recovery;") != string::npos) {
							Restore_List += "/recovery;";
							gui_msg("recovery=Recovery");
						} else if (value2[i] == '1' && DataManager::GetIntValue(TW_RESTORE_SP1_VAR) > 0) {
							gui_print("%s\n", "Special1 -- No Longer Supported...");
						} else if (value2[i] == '2' && DataManager::GetIntValue(TW_RESTORE_SP2_VAR) > 0) {
							gui_print("%s\n", "Special2 -- No Longer Supported...");
						} else if (value2[i] == '3' && DataManager::GetIntValue(TW_RESTORE_SP3_VAR) > 0) {
							gui_print("%s\n", "Special3 -- No Longer Supported...");
						} else if ((value2[i] == 'B' || value2[i] == 'b') && Partition_List.find("/boot;") != string::npos) {
							Restore_List += "/boot;";
							gui_msg("boot=Boot");
						} else if ((value2[i] == 'A' || value2[i] == 'a')  && Partition_List.find("/and-sec;") != string::npos) {
							Restore_List += "/and-sec;";
							gui_msg("android_secure=Android Secure");
						} else if ((value2[i] == 'E' || value2[i] == 'e')  && Partition_List.find("/sd-ext;") != string::npos) {
							Restore_List += "/sd-ext;";
							gui_msg("sdext=SD-EXT");
						} else if (value2[i] == 'M' || value2[i] == 'm') {
							DataManager::SetValue(TW_SKIP_MD5_CHECK_VAR, 1);
							gui_msg("md5_check_skip=MD5 check skip is on");
						}
					}

					DataManager::SetValue("tw_restore_selected", Restore_List);
				} else {
					DataManager::SetValue("tw_restore_selected", Partition_List);
				}
				if (is_encrypted) {
					gui_err("ors_encrypt_restore_err=Unable to use OpenRecoveryScript to restore an encrypted backup.");
					ret_val = 1;
				} else if (!PartitionManager.Run_Restore(folder_path))
					ret_val = 1;
				else
					gui_msg("done=Done.");
			} else if (strcmp(command, "mount") == 0) {
				// Mount
				DataManager::SetValue("tw_action_text2", gui_parse_text("{@mounting}"));
				if (value[0] != '/') {
					strcpy(mount, "/");
					strcat(mount, value);
				} else
					strcpy(mount, value);
				if (PartitionManager.Mount_By_Path(mount, true))
					gui_msg(Msg("mounted=Mounted '{1}'")(mount));
			} else if (strcmp(command, "unmount") == 0 || strcmp(command, "umount") == 0) {
				// Unmount
				DataManager::SetValue("tw_action_text2", gui_parse_text("{@unmounting}"));
				if (value[0] != '/') {
					strcpy(mount, "/");
					strcat(mount, value);
				} else
					strcpy(mount, value);
				if (PartitionManager.UnMount_By_Path(mount, true))
					gui_msg(Msg("unmounted=Unounted '{1}'")(mount));
			} else if (strcmp(command, "set") == 0) {
				// Set value
				size_t len = strlen(value);
				tok = strtok(value, " ");
				strcpy(value1, tok);
				if (len > strlen(value1) + 1) {
					char *val2 = value + strlen(value1) + 1;
					gui_msg(Msg("setting=Setting '{1}' to '{2}'")(value1)(val2));
					DataManager::SetValue(value1, val2);
				} else {
					gui_msg(Msg("setting_empty=Setting '{1}' to empty")(value1));
					DataManager::SetValue(value1, "");
				}
			} else if (strcmp(command, "mkdir") == 0) {
				// Make directory (recursive)
				DataManager::SetValue("tw_action_text2", gui_parse_text("{@making_dir1}"));
				gui_msg(Msg("making_dir2=Making directory: '{1}'")(value));
				if (!TWFunc::Recursive_Mkdir(value)) {
					// error message already displayed by Recursive_Mkdir
					ret_val = 1;
				}
			} else if (strcmp(command, "reboot") == 0) {
				if (strlen(value) && strcmp(value, "recovery") == 0)
					TWFunc::tw_reboot(rb_recovery);
				else if (strlen(value) && strcmp(value, "poweroff") == 0)
					TWFunc::tw_reboot(rb_poweroff);
				else if (strlen(value) && strcmp(value, "bootloader") == 0)
					TWFunc::tw_reboot(rb_bootloader);
				else if (strlen(value) && strcmp(value, "download") == 0)
					TWFunc::tw_reboot(rb_download);
				else
					TWFunc::tw_reboot(rb_system);
			} else if (strcmp(command, "cmd") == 0) {
				DataManager::SetValue("tw_action_text2", gui_parse_text("{@running_command}"));
				if (cindex != 0) {
					TWFunc::Exec_Cmd(value);
				} else {
					LOGERR("No value given for cmd\n");
				}
			} else if (strcmp(command, "print") == 0) {
				gui_print("%s\n", value);
			} else if (strcmp(command, "sideload") == 0) {
				// ADB Sideload
				DataManager::SetValue("tw_action_text2", gui_parse_text("{@sideload}"));
				install_cmd = -1;

				int wipe_cache = 0;
				string result;
				pid_t sideload_child_pid;

				gui_msg("start_sideload=Starting ADB sideload feature...");
				ret_val = apply_from_adb("/", &sideload_child_pid);
				if (ret_val != 0) {
					if (ret_val == -2)
						gui_err("need_new_adb=You need adb 1.0.32 or newer to sideload to this device.");
					ret_val = 1; // failure
				} else if (TWinstall_zip(FUSE_SIDELOAD_HOST_PATHNAME, &wipe_cache) == 0) {
					if (wipe_cache)
						PartitionManager.Wipe_By_Path("/cache");
				} else {
					ret_val = 1; // failure
				}
				sideload = 1; // Causes device to go to the home screen afterwards
				if (sideload_child_pid != 0) {
					LOGINFO("Signaling child sideload process to exit.\n");
					struct stat st;
					// Calling stat() on this magic filename signals the minadbd
					// subprocess to shut down.
					stat(FUSE_SIDELOAD_HOST_EXIT_PATHNAME, &st);
					int status;
					LOGINFO("Waiting for child sideload process to exit.\n");
					waitpid(sideload_child_pid, &status, 0);
				}
				property_set("ctl.start", "adbd");
				gui_msg("done=Done.");
			} else if (strcmp(command, "fixperms") == 0 || strcmp(command, "fixpermissions") == 0) {
				ret_val = PartitionManager.Fix_Permissions();
				if (ret_val != 0)
					ret_val = 1; // failure
			} else if (strcmp(command, "decrypt") == 0) {
				if (*value) {
					ret_val = PartitionManager.Decrypt_Device(value);
					if (ret_val != 0)
						ret_val = 1; // failure
				} else {
					gui_err("no_pwd=No password provided.");
					ret_val = 1; // failure
				}
			} else {
				LOGERR("Unrecognized script command: '%s'\n", command);
				ret_val = 1;
			}
		}
		fclose(fp);
		unlink(SCRIPT_FILE_TMP);
		gui_msg("done_ors=Done processing script file");
	} else {
		gui_msg(Msg(msg::kError, "error_opening_strerr=Error opening: '{1}' ({2})")(SCRIPT_FILE_TMP)(strerror(errno)));
		return 1;
	}
	if (install_cmd && DataManager::GetIntValue(TW_HAS_INJECTTWRP) == 1 && DataManager::GetIntValue(TW_INJECT_AFTER_ZIP) == 1) {
		gui_msg("injecttwrp=Injecting TWRP into boot image...");
		TWPartition* Boot = PartitionManager.Find_Partition_By_Path("/boot");
		if (Boot == NULL || Boot->Current_File_System != "emmc")
			TWFunc::Exec_Cmd("injecttwrp --dump /tmp/backup_recovery_ramdisk.img /tmp/injected_boot.img --flash");
		else {
			string injectcmd = "injecttwrp --dump /tmp/backup_recovery_ramdisk.img /tmp/injected_boot.img --flash bd=" + Boot->Actual_Block_Device;
			TWFunc::Exec_Cmd(injectcmd.c_str());
		}
		gui_msg("done=Done.");
	}
	if (sideload)
		ret_val = 1; // Forces booting to the home page after sideload
	return ret_val;
}

int OpenRecoveryScript::Insert_ORS_Command(string Command) {
	ofstream ORSfile(SCRIPT_FILE_TMP, ios_base::app | ios_base::out);
	if (ORSfile.is_open()) {
		//if (Command.substr(Command.size() - 1, 1) != "\n")
		//	Command += "\n";
		LOGINFO("Inserting '%s'\n", Command.c_str());
		ORSfile << Command.c_str() << endl;
		ORSfile.close();
		return 1;
	}
	LOGERR("Unable to append '%s' to '%s'\n", Command.c_str(), SCRIPT_FILE_TMP);
	return 0;
}

int OpenRecoveryScript::Install_Command(string Zip) {
	// Install zip
	string ret_string;
	int ret_val = 0, wipe_cache = 0;
	std::vector<PartitionList> Storage_List;
	string Full_Path;

	if (Zip.substr(0, 1) == "@") {
		// This is a special file that contains a map of blocks on the data partition
		Full_Path = Zip.substr(1);
		if (!PartitionManager.Mount_By_Path(Full_Path, true) || !TWFunc::Path_Exists(Full_Path)) {
			LOGINFO("Unable to install via mapped zip '%s'\n", Full_Path.c_str());
			gui_msg(Msg(msg::kError, "zip_err=Error installing zip file '{1}'")(Zip));
			return 1;
		}
		LOGINFO("Installing mapped zip file '%s'\n", Full_Path.c_str());
		gui_msg(Msg("installing_zip=Installing zip file '{1}'")(Zip));
	} else if (!TWFunc::Path_Exists(Zip)) {
		PartitionManager.Mount_All_Storage();
		PartitionManager.Get_Partition_List("storage", &Storage_List);
		int listSize = Storage_List.size();
		for (int i = 0; i < listSize; i++) {
			if (PartitionManager.Is_Mounted_By_Path(Storage_List.at(i).Mount_Point)) {
				Full_Path = Storage_List.at(i).Mount_Point + "/" + Zip;
				if (TWFunc::Path_Exists(Full_Path)) {
					Zip = Full_Path;
					break;
				}
				Full_Path = Zip;
				LOGINFO("Trying to find zip '%s' on '%s'...\n", Full_Path.c_str(), Storage_List.at(i).Mount_Point.c_str());
				ret_string = Locate_Zip_File(Full_Path, Storage_List.at(i).Mount_Point);
				if (!ret_string.empty()) {
					Zip = ret_string;
					break;
				}
			}
		}
		if (!TWFunc::Path_Exists(Zip)) {
			// zip file doesn't exist
			gui_print("Unable to locate zip file '%s'.\n", Zip.c_str());
			ret_val = 1;
		} else
			gui_msg(Msg("installing_zip=Installing zip file '{1}'")(Zip));
	}

	ret_val = TWinstall_zip(Zip.c_str(), &wipe_cache);
	if (ret_val != 0) {
		gui_msg(Msg(msg::kError, "zip_err=Error installing zip file '{1}'")(Zip));
		ret_val = 1;
	} else if (wipe_cache)
		PartitionManager.Wipe_By_Path("/cache");

	return ret_val;
}

string OpenRecoveryScript::Locate_Zip_File(string Zip, string Storage_Root) {
	string Path = TWFunc::Get_Path(Zip);
	string File = TWFunc::Get_Filename(Zip);
	string pathCpy = Path;
	string wholePath;
	size_t pos = Path.find("/", 1);

	while (pos != string::npos)
	{
		pathCpy = Path.substr(pos, Path.size() - pos);
		wholePath = pathCpy + File;
		LOGINFO("Looking for zip at '%s'\n", wholePath.c_str());
		if (TWFunc::Path_Exists(wholePath))
			return wholePath;
		wholePath = Storage_Root + wholePath;
		LOGINFO("Looking for zip at '%s'\n", wholePath.c_str());
		if (TWFunc::Path_Exists(wholePath))
			return wholePath;

		pos = Path.find("/", pos + 1);
	}
	return "";
}

int OpenRecoveryScript::Backup_Command(string Options) {
	char value1[SCRIPT_COMMAND_SIZE];
	int line_len, i;
	string Backup_List;

	strcpy(value1, Options.c_str());

	DataManager::SetValue(TW_USE_COMPRESSION_VAR, 0);
	DataManager::SetValue(TW_SKIP_MD5_GENERATE_VAR, 0);

	gui_msg("select_backup_opt=Setting backup options:");
	line_len = Options.size();
	for (i=0; i<line_len; i++) {
		if (Options.substr(i, 1) == "S" || Options.substr(i, 1) == "s") {
			Backup_List += "/system;";
			gui_msg("system=System");
		} else if (Options.substr(i, 1) == "D" || Options.substr(i, 1) == "d") {
			Backup_List += "/data;";
			gui_msg("data=Data");
		} else if (Options.substr(i, 1) == "C" || Options.substr(i, 1) == "c") {
			Backup_List += "/cache;";
			gui_msg("cache=Cache");
		} else if (Options.substr(i, 1) == "R" || Options.substr(i, 1) == "r") {
			Backup_List += "/recovery;";
			gui_msg("recovery=Recovery");
		} else if (Options.substr(i, 1) == "1") {
			gui_print("%s\n", "Special1 -- No Longer Supported...");
		} else if (Options.substr(i, 1) == "2") {
			gui_print("%s\n", "Special2 -- No Longer Supported...");
		} else if (Options.substr(i, 1) == "3") {
			gui_print("%s\n", "Special3 -- No Longer Supported...");
		} else if (Options.substr(i, 1) == "B" || Options.substr(i, 1) == "b") {
			Backup_List += "/boot;";
			gui_msg("boot=Boot");
		} else if (Options.substr(i, 1) == "A" || Options.substr(i, 1) == "a") {
			Backup_List += "/and-sec;";
			gui_msg("android_secure=Android Secure");
		} else if (Options.substr(i, 1) == "E" || Options.substr(i, 1) == "e") {
			Backup_List += "/sd-ext;";
			gui_msg("sdext=SD-EXT");
		} else if (Options.substr(i, 1) == "O" || Options.substr(i, 1) == "o") {
			DataManager::SetValue(TW_USE_COMPRESSION_VAR, 1);
			gui_msg("compression_on=Compression is on");
		} else if (Options.substr(i, 1) == "M" || Options.substr(i, 1) == "m") {
			DataManager::SetValue(TW_SKIP_MD5_GENERATE_VAR, 1);
			gui_msg("md5_off=MD5 Generation is off");
		}
	}
	DataManager::SetValue("tw_backup_list", Backup_List);
	if (!PartitionManager.Run_Backup()) {
		gui_err("backup_fail=Backup Failed");
		return 1;
	}
	gui_msg("backup_complete=Backup complete!");
	return 0;
}

// this is called by main()
void OpenRecoveryScript::Run_OpenRecoveryScript(void) {
	DataManager::SetValue("tw_back", "main");
	DataManager::SetValue("tw_action", "openrecoveryscript");
	DataManager::SetValue("tw_action_param", "");
	DataManager::SetValue("tw_has_action2", "0");
	DataManager::SetValue("tw_action2", "");
	DataManager::SetValue("tw_action2_param", "");
#ifdef TW_OEM_BUILD
	DataManager::SetValue("tw_action_text1", gui_lookup("running_recovery_commands", "Running Recovery Commands"));
	DataManager::SetValue("tw_complete_text1", gui_lookup("recovery_commands_complete", "Recovery Commands Complete"));
#else
	DataManager::SetValue("tw_action_text1", gui_lookup("running_ors", "Running OpenRecoveryScript"));
	DataManager::SetValue("tw_complete_text1", gui_lookup("ors_complete", "OpenRecoveryScript Complete"));
#endif
	DataManager::SetValue("tw_action_text2", "");
	DataManager::SetValue("tw_has_cancel", 0);
	DataManager::SetValue("tw_show_reboot", 0);
	if (gui_startPage("action_page", 0, 1) != 0) {
		LOGERR("Failed to load OpenRecoveryScript GUI page.\n");
	}
}

// this is called by the "openrecoveryscript" GUI action called via action page from Run_OpenRecoveryScript
int OpenRecoveryScript::Run_OpenRecoveryScript_Action() {
	int op_status = 1;
	// Check for the SCRIPT_FILE_TMP first as these are AOSP recovery commands
	// that we converted to ORS commands during boot in recovery.cpp.
	// Run those first.
	int reboot = 0;
	if (TWFunc::Path_Exists(SCRIPT_FILE_TMP)) {
		gui_msg("running_recovery_commands=Running Recovery Commands");
		if (OpenRecoveryScript::run_script_file() == 0) {
			reboot = 1;
			op_status = 0;
		}
	}
	// Check for the ORS file in /cache and attempt to run those commands.
	if (OpenRecoveryScript::check_for_script_file()) {
		gui_msg("running_ors=Running OpenRecoveryScript");
		if (OpenRecoveryScript::run_script_file() == 0) {
			reboot = 1;
			op_status = 0;
		}
	}
	if (reboot) {
		// Disable stock recovery reflashing
		TWFunc::Disable_Stock_Recovery_Replace();
		usleep(2000000); // Sleep for 2 seconds before rebooting
		TWFunc::tw_reboot(rb_system);
		usleep(5000000); // Sleep for 5 seconds to allow reboot to occur
	} else {
		DataManager::SetValue("tw_page_done", 1);
	}
	return op_status;
}

// this is called by the "twcmd" GUI action when a command is received via FIFO from the "twrp" command line tool
void OpenRecoveryScript::Run_CLI_Command(const char* command) {
	if (strlen(command) > 11 && strncmp(command, "runscript", 9) == 0) {
		const char* filename = command + 10;
		if (OpenRecoveryScript::copy_script_file(filename) == 0) {
			LOGINFO("Unable to copy script file\n");
		} else {
			OpenRecoveryScript::run_script_file();
		}
	} else if (strlen(command) > 5 && strncmp(command, "get", 3) == 0) {
		const char* varname = command + 4;
		string value;
		DataManager::GetValue(varname, value);
		gui_print("%s = %s\n", varname, value.c_str());
	} else if (strlen(command) > 9 && strncmp(command, "decrypt", 7) == 0) {
		const char* pass = command + 8;
		gui_msg("decrypt_cmd=Attempting to decrypt data partition via command line.");
		if (PartitionManager.Decrypt_Device(pass) == 0) {
			// set_page_done = 1;  // done by singleaction_page anyway
		}
	} else if (OpenRecoveryScript::Insert_ORS_Command(command)) {
		OpenRecoveryScript::run_script_file();
	}

	// let the GUI close the output fd and restart the command listener
	call_after_cli_command();
	LOGINFO("Done reading ORS command from command line\n");
}
