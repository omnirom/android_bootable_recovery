/*
		Copyright 2013 to 2017 TeamWin
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
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <string>
#include <sstream>
#include <algorithm>

#include "twrpback.hpp"
#include "twadbstream.h"


int main(int argc, char **argv) {
	int index;
	size_t pos = 0;
	bool ret = false;
	size_t maxpos = strlen(TWRPARG) + 2;
	std::string command;
	twrpback tw;

	tw.adblogwrite("Starting adb backup and restore\n");
	command = argv[1];
	for (index = 2; index < argc; index++) {
		command = command + " " + argv[index];
	}

	pos = command.find(TWRP_BACKUP_ARG);
	if (pos == std::string::npos || pos > (maxpos + strlen(TWRP_BACKUP_ARG) + 1)) {
		pos = command.find(TWRP_RESTORE_ARG);
	}
	if (pos == std::string::npos || pos > maxpos + strlen(TWRP_STREAM_ARG) + 1) {
		pos = command.find(TWRP_STREAM_ARG);
	}

	tw.adblogwrite("command: " + command + "\n");
	command.erase(0, pos);
	command.erase(std::remove(command.begin(), command.end(), '\''), command.end());

	if (command.substr(0, sizeof(TWRP_BACKUP_ARG) - 1) == TWRP_BACKUP_ARG) {
		tw.adblogwrite("Starting adb backup\n");
		if (isdigit(*argv[1]))
			tw.adbd_fd = atoi(argv[1]);
		else
			tw.adbd_fd = 1;
		ret = tw.backup(command);
	}
	else if (command.substr(0, sizeof(TWRP_RESTORE_ARG) - 1) == TWRP_RESTORE_ARG) {
		tw.adblogwrite("Starting adb restore\n");
		if (isdigit(*argv[1]))
			tw.adbd_fd = atoi(argv[1]);
		else
			tw.adbd_fd = 0;
		ret = tw.restore();
	}
	else if (command.substr(0, sizeof(TWRP_STREAM_ARG) - 1) == TWRP_STREAM_ARG) {
		tw.setStreamFileName(argv[3]);
		tw.threadStream();
		ret = true;
	}
	if (ret)
		tw.adblogwrite("Adb backup/restore completed\n");
	else
		tw.adblogwrite("Adb backup/restore failed\n");

	if (unlink(TW_ADB_BU_CONTROL) < 0) {
		std::stringstream str;
		str << strerror(errno);
		tw.adblogwrite("Unable to remove TW_ADB_BU_CONTROL: " + str.str());
	}
	unlink(TW_ADB_TWRP_CONTROL);
	if (ret)
		return 0;
	else
		return -1;
}
