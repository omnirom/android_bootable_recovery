/*
        Copyright 2013 to 2017 TeamWin
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

#define __STDC_FORMAT_MACROS 1
#include <string>
#include <sys/wait.h>
#include <sys/stat.h>
#include <pthread.h>
#include <zlib.h>
#include <inttypes.h>
#include "twrpAdbBuFifo.hpp"
#include "twcommon.h"
#include "data.hpp"
#include "variables.h"
#include "partitions.hpp"
#include "twrp-functions.hpp"
#include "gui/gui.hpp"
#include "gui/objects.hpp"
#include "gui/pages.hpp"
#include "adbbu/twadbstream.h"
#include "adbbu/libtwadbbu.hpp"

twrpAdbBuFifo::twrpAdbBuFifo(void) {
	unlink(TW_ADB_FIFO);
}

void twrpAdbBuFifo::Check_Adb_Fifo_For_Events(void) {
	char cmd[512];

	memset(&cmd, 0, sizeof(cmd));

	if (read(adb_fifo_fd, &cmd, sizeof(cmd)) > 0) {
		LOGINFO("adb backup cmd: %s\n", cmd);
		std::string cmdcheck(cmd);
		cmdcheck = cmdcheck.substr(0, strlen(ADB_BACKUP_OP));
		std::string Options(cmd);
		Options = Options.substr(strlen(ADB_BACKUP_OP) + 1, strlen(cmd));
		if (cmdcheck == ADB_BACKUP_OP)
			Backup_ADB_Command(Options);
		else {
			Restore_ADB_Backup();
		}
	}
}

bool twrpAdbBuFifo::start(void) {
	LOGINFO("Starting Adb Backup FIFO\n");
	unlink(TW_ADB_FIFO);
	if (mkfifo(TW_ADB_FIFO, 06660) != 0) {
		LOGINFO("Unable to mkfifo %s\n", TW_ADB_FIFO);
		return false;
	}
	adb_fifo_fd = open(TW_ADB_FIFO, O_RDONLY | O_NONBLOCK);
	if (adb_fifo_fd < 0) {
		LOGERR("Unable to open TW_ADB_FIFO for reading.\n");
		close(adb_fifo_fd);
		return false;
	}
	while (true) {
		Check_Adb_Fifo_For_Events();
		usleep(8000);
	}
	//Shouldn't get here but cleanup anwyay
	close(adb_fifo_fd);
	return true;
}

pthread_t twrpAdbBuFifo::threadAdbBuFifo(void) {
	pthread_t thread;
	ThreadPtr adbfifo = &twrpAdbBuFifo::start;
	PThreadPtr p = *(PThreadPtr*)&adbfifo;
	pthread_create(&thread, NULL, p, this);
	return thread;
}

bool twrpAdbBuFifo::Backup_ADB_Command(std::string Options) {
	std::vector<std::string> args;
	std::string Backup_List;
	bool adbbackup = true, ret = false;
	std::string rmopt = "--";

	std::replace(Options.begin(), Options.end(), ':', ' ');
	args = TWFunc::Split_String(Options, " ");

	DataManager::SetValue(TW_USE_COMPRESSION_VAR, 0);
	DataManager::SetValue(TW_SKIP_DIGEST_GENERATE_VAR, 0);

	if (args[1].compare("--twrp") != 0) {
		gui_err("twrp_adbbu_option=--twrp option is required to enable twrp adb backup");
		if (!twadbbu::Write_TWERROR())
			LOGERR("Unable to write to ADB Backup\n");
		sleep(2);
		return false;
	}

	for (unsigned i = 2; i < args.size(); i++) {
		int compress;

		std::string::size_type size = args[i].find(rmopt);
		if (size != std::string::npos)
			args[i].erase(size, rmopt.length());

		if (args[i].compare("compress") == 0) {
			gui_msg("compression_on=Compression is on");
			DataManager::SetValue(TW_USE_COMPRESSION_VAR, 1);
			continue;
		}
		DataManager::GetValue(TW_USE_COMPRESSION_VAR, compress);
		gui_print("%s\n", args[i].c_str());
		std::string path;
		path = "/" + args[i];
		TWPartition* part = PartitionManager.Find_Partition_By_Path(path);
		if (part) {
			Backup_List += path;
			Backup_List += ";";
		}
		else {
			gui_msg(Msg(msg::kError, "partition_not_found=path: {1} not found in partition list")(path));
			if (!twadbbu::Write_TWERROR())
				LOGERR("Unable to write to TWRP ADB Backup.\n");
		return false;
	}
}

	if (Backup_List.empty()) {
		DataManager::GetValue("tw_backup_list", Backup_List);
		if (Backup_List.empty()) {
			gui_err("no_partition_selected=No partitions selected for backup.");
			return false;
		}
	}
	else
		DataManager::SetValue("tw_backup_list", Backup_List);

	DataManager::SetValue("tw_action", "clear");
	DataManager::SetValue("tw_action_text1", gui_lookup("running_recovery_commands", "Running Recovery Commands"));
	DataManager::SetValue("tw_action_text2", "");
	gui_changePage("action_page");

	ret = PartitionManager.Run_Backup(adbbackup);
	DataManager::SetValue(TW_BACKUP_NAME, gui_lookup("auto_generate", "(Auto Generate)"));
	if (!ret) {
		gui_err("backup_fail=Backup failed");
		return false;
	}
	gui_msg("backup_complete=Backup Complete");
	DataManager::SetValue("ui_progress", 100);
	sleep(2); //give time for user to see messages on console
	gui_changePage("main");
	return true;
}

bool twrpAdbBuFifo::Restore_ADB_Backup(void) {
	int partition_count = 0;
	std::string Restore_Name;
	struct AdbBackupFileTrailer adbmd5;
	struct PartitionSettings part_settings;
	int adb_control_twrp_fd;
	int adb_control_bu_fd, ret = 0;
	char cmd[512];

	part_settings.total_restore_size = 0;

	PartitionManager.Mount_All_Storage();
	LOGINFO("opening TW_ADB_BU_CONTROL\n");
	adb_control_bu_fd = open(TW_ADB_BU_CONTROL, O_WRONLY | O_NONBLOCK);
	LOGINFO("opening TW_ADB_TWRP_CONTROL\n");
	adb_control_twrp_fd = open(TW_ADB_TWRP_CONTROL, O_RDONLY | O_NONBLOCK);
	memset(&adbmd5, 0, sizeof(adbmd5));

	DataManager::SetValue("tw_action", "clear");
	DataManager::SetValue("tw_action_text1", gui_lookup("running_recovery_commands", "Running Recovery Commands"));
	DataManager::SetValue("tw_action_text2", "");
	gui_changePage("action_page");

	while (true) {
		memset(&cmd, 0, sizeof(cmd));
		if (read(adb_control_twrp_fd, cmd, sizeof(cmd)) > 0) {
			struct AdbBackupControlType cmdstruct;

			memset(&cmdstruct, 0, sizeof(cmdstruct));
			memcpy(&cmdstruct, cmd, sizeof(cmdstruct));
			std::string cmdtype = cmdstruct.get_type();
			if (cmdtype == TWSTREAMHDR) {
				struct AdbBackupStreamHeader twhdr;
				memcpy(&twhdr, cmd, sizeof(cmd));
				LOGINFO("ADB Partition count: %" PRIu64 "\n", twhdr.partition_count);
				LOGINFO("ADB version: %" PRIu64 "\n", twhdr.version);
				if (twhdr.version != ADB_BACKUP_VERSION) {
					LOGERR("Incompatible adb backup version!\n");
					ret = false;
					break;
				}
				partition_count = twhdr.partition_count;
			}
			else if (cmdtype == MD5TRAILER) {
				LOGINFO("Reading ADB Backup MD5TRAILER\n");
				memcpy(&adbmd5, cmd, sizeof(cmd));
			}
			else if (cmdtype == TWMD5) {
				int check_digest;

				DataManager::GetValue(TW_SKIP_DIGEST_CHECK_VAR, check_digest);
				if (check_digest > 0) {
					TWFunc::GUI_Operation_Text(TW_VERIFY_DIGEST_TEXT, gui_parse_text("{@verifying_digest}"));
					gui_msg("verifying_digest=Verifying Digest");
					struct AdbBackupFileTrailer md5check;
					LOGINFO("Verifying md5sums\n");

					memset(&md5check, 0, sizeof(md5check));
					memcpy(&md5check, cmd, sizeof(cmd));
					if (strcmp(md5check.md5, adbmd5.md5) != 0) {
						LOGERR("md5 doesn't match!\n");
						LOGERR("Stored file md5: %s\n", adbmd5.md5);
						LOGERR("ADB Backup check md5: %s\n", md5check.md5);
						ret = false;
						break;
					}
					else {
						LOGINFO("ADB Backup md5 matches\n");
						LOGINFO("Stored file md5: %s\n", adbmd5.md5);
						LOGINFO("ADB Backup check md5: %s\n", md5check.md5);
						continue;
					}
				} else {
					gui_msg("skip_digest=Skipping Digest check based on user setting.");
					continue;
				}

			}
			else if (cmdtype == TWENDADB) {
				LOGINFO("received TWENDADB\n");
				ret = 1;
				break;
			}
			else {
				struct twfilehdr twimghdr;
				memcpy(&twimghdr, cmd, sizeof(cmd));
				std::string cmdstr(twimghdr.type);
				Restore_Name = twimghdr.name;
				part_settings.total_restore_size = twimghdr.size;
				if (cmdtype == TWIMG) {
					LOGINFO("ADB Type: %s\n", twimghdr.type);
					LOGINFO("ADB Restore_Name: %s\n", Restore_Name.c_str());
					LOGINFO("ADB Restore_size: %" PRIu64 "\n", part_settings.total_restore_size);
					string compression = (twimghdr.compressed == 1) ? "compressed" : "uncompressed";
					LOGINFO("ADB compression: %s\n", compression.c_str());
					std::string Backup_FileName;
					std::size_t pos = Restore_Name.find_last_of("/");
					std::string path = "/" + Restore_Name.substr(pos, Restore_Name.size());
					pos = path.find_first_of(".");
					path = path.substr(0, pos);
					if (path.substr(0,1).compare("//")) {
						path = path.substr(1, path.size());
					}

					pos = Restore_Name.find_last_of("/");
					Backup_FileName = Restore_Name.substr(pos + 1, Restore_Name.size());
					part_settings.Part = PartitionManager.Find_Partition_By_Path(path);
					part_settings.Backup_Folder = path;
					part_settings.partition_count = partition_count;
					part_settings.adbbackup = true;
					part_settings.adb_compression = twimghdr.compressed;
					part_settings.PM_Method = PM_RESTORE;
					ProgressTracking progress(part_settings.total_restore_size);
					part_settings.progress = &progress;
					if (!PartitionManager.Restore_Partition(&part_settings)) {
						LOGERR("ADB Restore failed.\n");
						ret = false;
						break;
					}
				}
				else if (cmdtype == TWFN) {
					LOGINFO("ADB Type: %s\n", twimghdr.type);
					LOGINFO("ADB Restore_Name: %s\n", Restore_Name.c_str());
					LOGINFO("ADB Restore_size: %" PRIi64 "\n", part_settings.total_restore_size);
					string compression = (twimghdr.compressed == 1) ? "compressed" : "uncompressed";
					LOGINFO("ADB compression: %s\n", compression.c_str());
					std::string Backup_FileName;
					std::size_t pos = Restore_Name.find_last_of("/");
					std::string path = "/" + Restore_Name.substr(pos, Restore_Name.size());
					pos = path.find_first_of(".");
					path = path.substr(0, pos);
					if (path.substr(0,1).compare("//")) {
						path = path.substr(1, path.size());
					}

					pos = Restore_Name.find_last_of("/");
					Backup_FileName = Restore_Name.substr(pos + 1, Restore_Name.size());
					pos = Restore_Name.find_last_of("/");
					part_settings.Part = PartitionManager.Find_Partition_By_Path(path);
					part_settings.Part->Set_Backup_FileName(Backup_FileName);
					PartitionManager.Set_Restore_Files(path);

					if (path.compare(PartitionManager.Get_Android_Root_Path()) == 0) {
						if (part_settings.Part->Is_Read_Only()) {
							if (!twadbbu::Write_TWERROR())
								LOGERR("Unable to write to TWRP ADB Backup.\n");
							gui_msg(Msg(msg::kError, "restore_read_only=Cannot restore {1} -- mounted read only.")(part_settings.Part->Backup_Display_Name));
							ret = false;
							break;

						}
					}
					part_settings.partition_count = partition_count;
					part_settings.adbbackup = true;
					part_settings.adb_compression = twimghdr.compressed;
					part_settings.total_restore_size += part_settings.Part->Get_Restore_Size(&part_settings);
					part_settings.PM_Method = PM_RESTORE;
					ProgressTracking progress(part_settings.total_restore_size);
					part_settings.progress = &progress;
					if (!PartitionManager.Restore_Partition(&part_settings)) {
						LOGERR("ADB Restore failed.\n");
						ret = false;
						break;
					}
				}
			}
		}
	}

	if (ret != false)
		gui_msg("restore_complete=Restore Complete");
	else
		gui_err("restore_error=Error during restore process.");

	if (!twadbbu::Write_TWENDADB())
		ret = false;
	sleep(2); //give time for user to see messages on console
	DataManager::SetValue("ui_progress", 100);
	gui_changePage("main");
	close(adb_control_bu_fd);
	return ret;
}
