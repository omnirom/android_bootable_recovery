
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

#include "../twrp-functions.hpp"
#include "../twrpTar.hpp"
#include "../twrpDU.hpp"
#include "../progresstracking.hpp"
#include "../gui/gui.hpp"
#include "../gui/twmsg.h"
#include <string.h>

twrpDU du;

void gui_msg(const char* text)
{
	if (text) {
		Message msg = Msg(text);
		gui_msg(msg);
	}
}

void gui_warn(const char* text)
{
	if (text) {
		Message msg = Msg(msg::kWarning, text);
		gui_msg(msg);
	}
}

void gui_err(const char* text)
{
	if (text) {
		Message msg = Msg(msg::kError, text);
		gui_msg(msg);
	}
}

void gui_highlight(const char* text)
{
	if (text) {
		Message msg = Msg(msg::kHighlight, text);
		gui_msg(msg);
	}
}

void gui_msg(Message msg)
{
	std::string output = msg;
	output += "\n";
	fputs(output.c_str(), stdout);
}

void usage() {
	printf("twrpTar <action> [options]\n\n");
	printf("actions: -c create\n");
	printf("         -x extract\n\n");
	printf(" -d    target directory\n");
	printf(" -t    output file\n");
	printf(" -m    skip media subfolder (has data media)\n");
	printf(" -z    compress backup (/sbin/pigz must be present)\n");
#ifndef TW_EXCLUDE_ENCRYPTED_BACKUPS
	printf(" -e    encrypt/decrypt backup followed by password (/sbin/openaes must be present)\n");
	printf(" -u    encrypt using userdata encryption (must be used with -e)\n");
#endif
	printf("\n\n");
	printf("Example: twrpTar -c -d /cache -t /sdcard/test.tar\n");
	printf("         twrpTar -x -d /cache -t /sdcard/test.tar\n");
}

int main(int argc, char **argv) {
	twrpTar tar;
	int use_encryption = 0, userdata_encryption = 0, has_data_media = 0, use_compression = 0, include_root = 0;
	int i, action = 0;
	unsigned j;
	string Directory, Tar_Filename;
	ProgressTracking progress(1);
	pid_t tar_fork_pid = 0;
#ifndef TW_EXCLUDE_ENCRYPTED_BACKUPS
	string Password;
#endif

	if (argc < 2) {
		usage();
		return 0;
	}

	if (strcmp(argv[1], "-c") == 0)
		action = 1; // create tar
	else if (strcmp(argv[1], "-x") == 0)
		action = 2; // extract tar
	else {
		printf("Invalid action '%s' specified.\n", argv[1]);
		usage();
		return -1;
	}

	for (i = 2; i < argc; i++) {
		if (strcmp(argv[i], "-d") == 0) {
			i++;
			if (argc <= i) {
				printf("No argument specified for %s\n", argv[i - 1]);
				usage();
				return -1;
			} else {
				Directory = argv[i];
			}
		} else if (strcmp(argv[i], "-t") == 0) {
			i++;
			if (argc <= i) {
				printf("No argument specified for %s\n", argv[i - 1]);
				usage();
				return -1;
			} else {
				Tar_Filename = argv[i];
			}
		} else if (strcmp(argv[i], "-e") == 0) {
#ifndef TW_EXCLUDE_ENCRYPTED_BACKUPS
			i++;
			if (argc <= i) {
				printf("No argument specified for %s\n", argv[i - 1]);
				usage();
				return -1;
			} else {
				use_encryption = 1;
				Password = argv[i];
			}
#else
			printf("Encrypted tar file support not present\n");
			usage();
			return -1;
#endif
		} else if (strcmp(argv[i], "-m") == 0) {
			if (action == 2)
				printf("NOTE: %s option not needed when extracting.\n", argv[i]);
			has_data_media = 1;
		} else if (strcmp(argv[i], "-z") == 0) {
			if (action == 2)
				printf("NOTE: %s option not needed when extracting.\n", argv[i]);
			use_compression = 1;
		} else if (strcmp(argv[i], "-u") == 0) {
#ifndef TW_EXCLUDE_ENCRYPTED_BACKUPS
			if (action == 2)
				printf("NOTE: %s option not needed when extracting.\n", argv[i]);
			userdata_encryption = 1;
#else
			printf("Encrypted tar file support not present\n");
			usage();
			return -1;
#endif
		}
	}

	tar.has_data_media = has_data_media;
	tar.setdir(Directory);
	tar.setfn(Tar_Filename);
	tar.setsize(du.Get_Folder_Size(Directory));
	tar.use_compression = use_compression;
#ifndef TW_EXCLUDE_ENCRYPTED_BACKUPS
	if (userdata_encryption && !use_encryption) {
		printf("userdata encryption set without encryption option\n");
		usage();
		return -1;
	}
	if (use_encryption) {
		tar.use_encryption = use_encryption;
		tar.userdata_encryption = userdata_encryption;
		tar.setpassword(Password);
	} else {
		use_encryption = false;
	}
#endif
	if (action == 1) {
		if (tar.createTarFork(&progress, tar_fork_pid) != 0) {
			sync();
			return -1;
		}
		sync();
		printf("\n\ntar created successfully.\n");
	} else if (action == 2) {
		if (tar.extractTarFork(&progress) != 0) {
			sync();
			return -1;
		}
		sync();
		printf("\n\ntar extracted successfully.\n");
	}
	return 0;
}
