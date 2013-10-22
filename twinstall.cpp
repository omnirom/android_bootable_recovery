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

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <string.h>
#include <stdio.h>

#include "twcommon.h"
#include "mincrypt/rsa.h"
#include "mincrypt/sha.h"
#include "minui/minui.h"
#ifdef HAVE_SELINUX
#include "minzip/SysUtil.h"
#include "minzip/Zip.h"
#else
#include "minzipold/SysUtil.h"
#include "minzipold/Zip.h"
#endif
#include "mtdutils/mounts.h"
#include "mtdutils/mtdutils.h"
#include "verifier.h"
#include "variables.h"
#include "data.hpp"
#include "partitions.hpp"
#include "twrpDigest.hpp"
#include "twrp-functions.hpp"
extern "C" {
	#include "gui/gui.h"
}

static int Run_Update_Binary(const char *path, ZipArchive *Zip, int* wipe_cache) {
	const ZipEntry* binary_location = mzFindZipEntry(Zip, ASSUMED_UPDATE_BINARY_NAME);
	string Temp_Binary = "/tmp/updater";
	int binary_fd, ret_val, pipe_fd[2], status, zip_verify;
	char buffer[1024];
	const char** args = (const char**)malloc(sizeof(char*) * 5);
	FILE* child_data;

	if (binary_location == NULL) {
		mzCloseZipArchive(Zip);
		return INSTALL_CORRUPT;
	}

	// Delete any existing updater
	if (TWFunc::Path_Exists(Temp_Binary) && unlink(Temp_Binary.c_str()) != 0) {
		LOGINFO("Unable to unlink '%s'\n", Temp_Binary.c_str());
	}

	binary_fd = creat(Temp_Binary.c_str(), 0755);
	if (binary_fd < 0) {
		mzCloseZipArchive(Zip);
		LOGERR("Could not create file for updater extract in '%s'\n", Temp_Binary.c_str());
		return INSTALL_ERROR;
	}

	ret_val = mzExtractZipEntryToFile(Zip, binary_location, binary_fd);
	close(binary_fd);

	if (!ret_val) {
		mzCloseZipArchive(Zip);
		LOGERR("Could not extract '%s'\n", ASSUMED_UPDATE_BINARY_NAME);
		return INSTALL_ERROR;
	}

	// If exists, extract file_contexts from the zip file
	const ZipEntry* selinx_contexts = mzFindZipEntry(Zip, "file_contexts");
	if (selinx_contexts == NULL) {
		mzCloseZipArchive(Zip);
		LOGINFO("Zip does not contain SELinux file_contexts file in its root.\n");
	} else {
		string output_filename = "/file_contexts";
		LOGINFO("Zip contains SELinux file_contexts file in its root. Extracting to %s\n", output_filename.c_str());
		// Delete any file_contexts
		if (TWFunc::Path_Exists(output_filename) && unlink(output_filename.c_str()) != 0) {
			LOGINFO("Unable to unlink '%s'\n", output_filename.c_str());
		}

		int file_contexts_fd = creat(output_filename.c_str(), 0644);
		if (file_contexts_fd < 0) {
			mzCloseZipArchive(Zip);
			LOGERR("Could not extract file_contexts to '%s'\n", output_filename.c_str());
			return INSTALL_ERROR;
		}

		ret_val = mzExtractZipEntryToFile(Zip, selinx_contexts, file_contexts_fd);
		close(file_contexts_fd);

		if (!ret_val) {
			mzCloseZipArchive(Zip);
			LOGERR("Could not extract '%s'\n", ASSUMED_UPDATE_BINARY_NAME);
			return INSTALL_ERROR;
		}
	}
	mzCloseZipArchive(Zip);

	pipe(pipe_fd);

	args[0] = Temp_Binary.c_str();
	args[1] = EXPAND(RECOVERY_API_VERSION);
	char* temp = (char*)malloc(10);
	sprintf(temp, "%d", pipe_fd[1]);
	args[2] = temp;
	args[3] = (char*)path;
	args[4] = NULL;

	pid_t pid = fork();
	if (pid == 0) {
		close(pipe_fd[0]);
		execv(Temp_Binary.c_str(), (char* const*)args);
		printf("E:Can't execute '%s'\n", Temp_Binary.c_str());
		_exit(-1);
	}
	close(pipe_fd[1]);

	*wipe_cache = 0;

	DataManager::GetValue(TW_SIGNED_ZIP_VERIFY_VAR, zip_verify);
	child_data = fdopen(pipe_fd[0], "r");
	while (fgets(buffer, sizeof(buffer), child_data) != NULL) {
		char* command = strtok(buffer, " \n");
		if (command == NULL) {
			continue;
		} else if (strcmp(command, "progress") == 0) {
			char* fraction_char = strtok(NULL, " \n");
			char* seconds_char = strtok(NULL, " \n");

			float fraction_float = strtof(fraction_char, NULL);
			int seconds_float = strtol(seconds_char, NULL, 10);

			if (zip_verify)
				DataManager::ShowProgress(fraction_float * (1 - VERIFICATION_PROGRESS_FRACTION), seconds_float);
			else
				DataManager::ShowProgress(fraction_float, seconds_float);
		} else if (strcmp(command, "set_progress") == 0) {
			char* fraction_char = strtok(NULL, " \n");
			float fraction_float = strtof(fraction_char, NULL);
			DataManager::SetProgress(fraction_float);
		} else if (strcmp(command, "ui_print") == 0) {
			char* display_value = strtok(NULL, "\n");
			if (display_value) {
				gui_print("%s", display_value);
			} else {
				gui_print("\n");
			}
		} else if (strcmp(command, "wipe_cache") == 0) {
			*wipe_cache = 1;
		} else if (strcmp(command, "clear_display") == 0) {
			// Do nothing, not supported by TWRP
		} else {
			LOGERR("unknown command [%s]\n", command);
		}
	}
	fclose(child_data);

	waitpid(pid, &status, 0);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		LOGERR("Error executing updater binary in zip '%s'\n", path);
		return INSTALL_ERROR;
	}

	return INSTALL_SUCCESS;
}

extern "C" int TWinstall_zip(const char* path, int* wipe_cache) {
	int ret_val, zip_verify, md5_return, key_count;
	twrpDigest md5sum;
	string strpath = path;
	ZipArchive Zip;

	gui_print("Installing '%s'...\nChecking for MD5 file...\n", path);
	md5sum.setfn(strpath);
	md5_return = md5sum.verify_md5digest();
	if (md5_return == -2) {
		// MD5 did not match.
		LOGERR("Zip MD5 does not match.\nUnable to install zip.\n");
		return INSTALL_CORRUPT;
	} else if (md5_return == -1) {
		gui_print("Skipping MD5 check: no MD5 file found.\n");
	} else if (md5_return == 0)
		gui_print("Zip MD5 matched.\n"); // MD5 found and matched.

	DataManager::GetValue(TW_SIGNED_ZIP_VERIFY_VAR, zip_verify);
	DataManager::SetProgress(0);
	if (zip_verify) {
		gui_print("Verifying zip signature...\n");
		ret_val = verify_file(path);
		if (ret_val != VERIFY_SUCCESS) {
			LOGERR("Zip signature verification failed: %i\n", ret_val);
			return -1;
		}
	}
	ret_val = mzOpenZipArchive(path, &Zip);
	if (ret_val != 0) {
		LOGERR("Zip file is corrupt!\n", path);
		return INSTALL_CORRUPT;
	}
	return Run_Update_Binary(path, &Zip, wipe_cache);
}
