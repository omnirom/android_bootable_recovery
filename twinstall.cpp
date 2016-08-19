/*
	Copyright 2012 to 2016 bigbiff/Dees_Troy TeamWin
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
#include "mtdutils/mounts.h"
#include "mtdutils/mtdutils.h"
#include "minzip/SysUtil.h"
#include "minzip/Zip.h"
#ifdef USE_OLD_VERIFIER
#include "verifier24/verifier.h"
#else
#include "verifier.h"
#endif
#include "variables.h"
#include "data.hpp"
#include "partitions.hpp"
#include "twrpDigestDriver.hpp"
#include "twrpDigest/twrpDigest.hpp"
#include "twrpDigest/twrpMD5.hpp"
#include "twrp-functions.hpp"
#include "gui/gui.hpp"
#include "gui/pages.hpp"
#include "legacy_property_service.h"
#include "twinstall.h"
#include "installcommand.h"
extern "C" {
	#include "gui/gui.h"
}

#define AB_OTA "payload_properties.txt"

static const char* properties_path = "/dev/__properties__";
static const char* properties_path_renamed = "/dev/__properties_kk__";
static bool legacy_props_env_initd = false;
static bool legacy_props_path_modified = false;

enum zip_type {
	UNKNOWN_ZIP_TYPE = 0,
	UPDATE_BINARY_ZIP_TYPE,
	AB_OTA_ZIP_TYPE,
	TWRP_THEME_ZIP_TYPE
};

// to support pre-KitKat update-binaries that expect properties in the legacy format
static int switch_to_legacy_properties()
{
	if (!legacy_props_env_initd) {
		if (legacy_properties_init() != 0)
			return -1;

		char tmp[32];
		int propfd, propsz;
		legacy_get_property_workspace(&propfd, &propsz);
		sprintf(tmp, "%d,%d", dup(propfd), propsz);
		setenv("ANDROID_PROPERTY_WORKSPACE", tmp, 1);
		legacy_props_env_initd = true;
	}

	if (TWFunc::Path_Exists(properties_path)) {
		// hide real properties so that the updater uses the envvar to find the legacy format properties
		if (rename(properties_path, properties_path_renamed) != 0) {
			LOGERR("Renaming %s failed: %s\n", properties_path, strerror(errno));
			return -1;
		} else {
			legacy_props_path_modified = true;
		}
	}

	return 0;
}

static int switch_to_new_properties()
{
	if (TWFunc::Path_Exists(properties_path_renamed)) {
		if (rename(properties_path_renamed, properties_path) != 0) {
			LOGERR("Renaming %s failed: %s\n", properties_path_renamed, strerror(errno));
			return -1;
		} else {
			legacy_props_path_modified = false;
		}
	}

	return 0;
}

static int Install_Theme(const char* path, ZipArchive *Zip) {
#ifdef TW_OEM_BUILD // We don't do custom themes in OEM builds
	mzCloseZipArchive(Zip);
	return INSTALL_CORRUPT;
#else
	const ZipEntry* xml_location = mzFindZipEntry(Zip, "ui.xml");

	mzCloseZipArchive(Zip);
	if (xml_location == NULL) {
		return INSTALL_CORRUPT;
	}
	if (!PartitionManager.Mount_Settings_Storage(true))
		return INSTALL_ERROR;
	string theme_path = DataManager::GetSettingsStoragePath();
	theme_path += "/TWRP/theme";
	if (!TWFunc::Path_Exists(theme_path)) {
		if (!TWFunc::Recursive_Mkdir(theme_path)) {
			return INSTALL_ERROR;
		}
	}
	theme_path += "/ui.zip";
	if (TWFunc::copy_file(path, theme_path, 0644) != 0) {
		return INSTALL_ERROR;
	}
	LOGINFO("Installing custom theme '%s' to '%s'\n", path, theme_path.c_str());
	PageManager::RequestReload();
	return INSTALL_SUCCESS;
#endif
}

static int Prepare_Update_Binary(const char *path, ZipArchive *Zip, int* wipe_cache) {
	const ZipEntry* binary_location = mzFindZipEntry(Zip, ASSUMED_UPDATE_BINARY_NAME);
	int binary_fd, ret_val;

	if (binary_location == NULL) {
		return INSTALL_CORRUPT;
	}

	// Delete any existing updater
	if (TWFunc::Path_Exists(TMP_UPDATER_BINARY_PATH) && unlink(TMP_UPDATER_BINARY_PATH) != 0) {
		LOGINFO("Unable to unlink '%s': %s\n", TMP_UPDATER_BINARY_PATH, strerror(errno));
	}

	binary_fd = creat(TMP_UPDATER_BINARY_PATH, 0755);
	if (binary_fd < 0) {
		LOGERR("Could not create file for updater extract in '%s': %s\n", TMP_UPDATER_BINARY_PATH, strerror(errno));
		mzCloseZipArchive(Zip);
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
			LOGINFO("Unable to unlink '%s': %s\n", output_filename.c_str(), strerror(errno));
		}

		int file_contexts_fd = creat(output_filename.c_str(), 0644);
		if (file_contexts_fd < 0) {
			LOGERR("Could not extract to '%s': %s\n", output_filename.c_str(), strerror(errno));
			mzCloseZipArchive(Zip);
			return INSTALL_ERROR;
		}

		ret_val = mzExtractZipEntryToFile(Zip, selinx_contexts, file_contexts_fd);
		close(file_contexts_fd);

		if (!ret_val) {
			mzCloseZipArchive(Zip);
			LOGERR("Could not extract '%s'\n", output_filename.c_str());
			return INSTALL_ERROR;
		}
	}
	mzCloseZipArchive(Zip);
	return INSTALL_SUCCESS;
}

static int Run_Update_Binary(const char *path, ZipArchive *Zip, int* wipe_cache, zip_type ztype) {
	int ret_val, pipe_fd[2], status, zip_verify;
	char buffer[1024];
	FILE* child_data;

#ifndef TW_NO_LEGACY_PROPS
	/* Set legacy properties */
	if (switch_to_legacy_properties() != 0) {
		LOGERR("Legacy property environment did not initialize successfully. Properties may not be detected.\n");
	} else {
		LOGINFO("Legacy property environment initialized.\n");
	}
#endif

	pipe(pipe_fd);

	std::vector<std::string> args;
    if (ztype == UPDATE_BINARY_ZIP_TYPE) {
		ret_val = update_binary_command(path, Zip, 0, pipe_fd[1], &args);
    } else if (ztype == AB_OTA_ZIP_TYPE) {
		ret_val = abupdate_binary_command(path, Zip, 0, pipe_fd[1], &args);
	} else {
		LOGERR("Unknown zip type %i\n", ztype);
		ret_val = INSTALL_CORRUPT;
	}
    if (ret_val) {
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        return ret_val;
    }

	// Convert the vector to a NULL-terminated char* array suitable for execv.
	const char* chr_args[args.size() + 1];
	chr_args[args.size()] = NULL;
	for (size_t i = 0; i < args.size(); i++)
		chr_args[i] = args[i].c_str();

	pid_t pid = fork();
	if (pid == 0) {
		close(pipe_fd[0]);
		execve(chr_args[0], const_cast<char**>(chr_args), environ);
		printf("E:Can't execute '%s': %s\n", chr_args[0], strerror(errno));
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
		} else if (strcmp(command, "log") == 0) {
			printf("%s\n", strtok(NULL, "\n"));
		} else {
			LOGERR("unknown command [%s]\n", command);
		}
	}
	fclose(child_data);

	int waitrc = TWFunc::Wait_For_Child(pid, &status, "Updater");

#ifndef TW_NO_LEGACY_PROPS
	/* Unset legacy properties */
	if (legacy_props_path_modified) {
		if (switch_to_new_properties() != 0) {
			LOGERR("Legacy property environment did not disable successfully. Legacy properties may still be in use.\n");
		} else {
			LOGINFO("Legacy property environment disabled.\n");
		}
	}
#endif

	if (waitrc != 0)
		return INSTALL_ERROR;

	return INSTALL_SUCCESS;
}

extern "C" int TWinstall_zip(const char* path, int* wipe_cache) {
	int ret_val, zip_verify = 1;
	ZipArchive Zip;

	if (strcmp(path, "error") == 0) {
		LOGERR("Failed to get adb sideload file: '%s'\n", path);
		return INSTALL_CORRUPT;
	}

	gui_msg(Msg("installing_zip=Installing zip file '{1}'")(path));
	if (strlen(path) < 9 || strncmp(path, "/sideload", 9) != 0) {
		string digest_str;
		string Full_Filename = path;
		string digest_file = path;
		digest_file += ".md5";

		gui_msg("check_for_digest=Checking for Digest file...");
		if (!TWFunc::Path_Exists(digest_file)) {
			gui_msg("no_digest=Skipping Digest check: no Digest file found");
		}
		else {
			if (TWFunc::read_file(digest_file, digest_str) != 0) {
				LOGERR("Skipping MD5 check: MD5 file unreadable\n");
			}
			else {
				twrpDigest *digest = new twrpMD5();
				if (!twrpDigestDriver::stream_file_to_digest(Full_Filename, digest)) {
					delete digest;
					return INSTALL_CORRUPT;
				}
				string digest_check = digest->return_digest_string();
				if (digest_str == digest_check) {
					gui_msg(Msg("digest_matched=Digest matched for '{1}'.")(path));
				}
				else {
					LOGERR("Aborting zip install: Digest verification failed\n");
					delete digest;
					return INSTALL_CORRUPT;
				}
				delete digest;
			}
		}
	}

#ifndef TW_OEM_BUILD
	DataManager::GetValue(TW_SIGNED_ZIP_VERIFY_VAR, zip_verify);
#endif
	DataManager::SetProgress(0);

	MemMapping map;
	if (sysMapFile(path, &map) != 0) {
		gui_msg(Msg(msg::kError, "fail_sysmap=Failed to map file '{1}'")(path));
		return -1;
	}

	if (zip_verify) {
		gui_msg("verify_zip_sig=Verifying zip signature...");
		ret_val = verify_file(map.addr, map.length);
		if (ret_val != VERIFY_SUCCESS) {
			LOGINFO("Zip signature verification failed: %i\n", ret_val);
			gui_err("verify_zip_fail=Zip signature verification failed!");
			sysReleaseMap(&map);
			return -1;
		} else {
			gui_msg("verify_zip_done=Zip signature verified successfully.");
		}
	}
	ret_val = mzOpenZipArchive(map.addr, map.length, &Zip);
	if (ret_val != 0) {
		gui_err("zip_corrupt=Zip file is corrupt!");
		sysReleaseMap(&map);
		return INSTALL_CORRUPT;
	}

	time_t start, stop;
	time(&start);
	const ZipEntry* file_location = mzFindZipEntry(&Zip, ASSUMED_UPDATE_BINARY_NAME);
	if (file_location != NULL) {
		LOGINFO("Update binary zip\n");
		ret_val = Prepare_Update_Binary(path, &Zip, wipe_cache);
		if (ret_val == INSTALL_SUCCESS)
			ret_val = Run_Update_Binary(path, &Zip, wipe_cache, UPDATE_BINARY_ZIP_TYPE);
	} else {
		file_location = mzFindZipEntry(&Zip, AB_OTA);
		if (file_location != NULL) {
			LOGINFO("AB zip\n");
			ret_val = Run_Update_Binary(path, &Zip, wipe_cache, AB_OTA_ZIP_TYPE);
		} else {
			file_location = mzFindZipEntry(&Zip, "ui.xml");
			if (file_location != NULL) {
				LOGINFO("TWRP theme zip\n");
				ret_val = Install_Theme(path, &Zip);
			} else {
				mzCloseZipArchive(&Zip);
				ret_val = INSTALL_CORRUPT;
			}
		}
	}
	time(&stop);
	int total_time = (int) difftime(stop, start);
	if (ret_val == INSTALL_CORRUPT) {
		gui_err("invalid_zip_format=Invalid zip file format!");
	} else {
		LOGINFO("Install took %i second(s).\n", total_time);
	}
	sysReleaseMap(&map);
	return ret_val;
}
