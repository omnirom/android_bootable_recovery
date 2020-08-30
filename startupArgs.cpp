/*
	Copyright 2012-2020 TeamWin
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

#include "startupArgs.hpp"

void startupArgs::parse(int *argc, char ***argv) {
	std::vector<std::string> args = args::get_args(argc, argv);
	int index;

	LOGINFO("Startup Commands: ");
	for (index = 1; index < args.size(); index++) {
		printf("'%s'", args[index].c_str());
		if (args[index] == FASTBOOT) {
			fastboot_mode = true;
			android::base::SetProperty("sys.usb.config", "none");
			android::base::SetProperty("sys.usb.configfs", "0");
			sleep(1);
			android::base::SetProperty("sys.usb.configfs", "1");
			android::base::SetProperty("sys.usb.config", "fastboot");
			DataManager::SetValue("tw_enable_adb", 0);
			DataManager::SetValue("tw_enable_fastboot", 1);
		} else if (args[index] == UPDATE_PACKAGE) {
			std::string::size_type eq_pos = args[index].find("=");
			std::string arg = args[index].substr(eq_pos + 1, args[index].size());
			if (arg.size() == 0) {
				LOGERR("argument error specifying zip file\n");
			} else {
				std::string ORSCommand = "install " + arg;
				SkipDecryption = arg.find("@") == 1;
				if (!OpenRecoveryScript::Insert_ORS_Command(ORSCommand))
					break;
			}
		} else if (args[index].find(SEND_INTENT) != std::string::npos) {
			std::string::size_type eq_pos = args[index].find("=");
			std::string arg = args[index].substr(eq_pos + 1, args[index].size());
			if (arg.size() == 0) {
				LOGERR("argument error specifying intent file\n");
			} else {
				Send_Intent = arg;
			}
		} else if (args[index].find(WIPE_DATA) != std::string::npos) {
			if (!OpenRecoveryScript::Insert_ORS_Command("wipe data\n"))
				break;
		} else if (args[index].find(WIPE_CACHE) != std::string::npos) {
			if (!OpenRecoveryScript::Insert_ORS_Command("wipe cache\n"))
				break;
		} else if (args[index].find(NANDROID) != std::string::npos) {
			DataManager::SetValue(TW_BACKUP_NAME, gui_parse_text("{@auto_generate}"));
			if (!OpenRecoveryScript::Insert_ORS_Command("backup BSDCAE\n"))
				break;
		}
	}
	printf("\n");
}

bool startupArgs::Should_Skip_Decryption() {
	return SkipDecryption;
}

std::string startupArgs::Get_Intent() {
	return Send_Intent;
}

bool startupArgs::Get_Fastboot_Mode() {
	return fastboot_mode;
}