/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <linux/input.h>

#include "bootloader.h"
#include "common.h"
#include "extra-functions.h"
#include "data.h"
#include "variables.h"

void run_script(const char *str1, const char *str2, const char *str3, const char *str4, const char *str5, const char *str6, const char *str7, int request_confirm)
{
	ui_print("%s", str1);
	ui_print("%s", str2);
	pid_t pid = fork();
	if (pid == 0) {
		char *args[] = { "/sbin/sh", "-c", (char*)str3, "1>&2", NULL };
		execv("/sbin/sh", args);
		fprintf(stderr, str4, strerror(errno));
		_exit(-1);
	}
	int status;
	while (waitpid(pid, &status, WNOHANG) == 0) {
		ui_print(".");
		sleep(1);
	}
	ui_print("\n");
	if (!WIFEXITED(status) || (WEXITSTATUS(status) != 0)) {
		ui_print("%s", str5);
	} else {
		ui_print("%s", str6);
	}
}

int check_backup_name(int show_error) {
	// Check the backup name to ensure that it is the correct size and contains only valid characters
	// and that a backup with that name doesn't already exist
	char backup_name[MAX_BACKUP_NAME_LEN];
	char backup_loc[255], tw_image_dir[255];
	int copy_size = strlen(DataManager_GetStrValue(TW_BACKUP_NAME));
	int index, cur_char;
	struct stat st;

	// Check size
	if (copy_size > MAX_BACKUP_NAME_LEN) {
		if (show_error)
			LOGE("Backup name is too long.\n");
		return -2;
	}

	// Check characters
	strncpy(backup_name, DataManager_GetStrValue(TW_BACKUP_NAME), copy_size);
	if (strcmp(backup_name, "0") == 0)
		return 0; // A "0" (zero) means to use the current timestamp for the backup name
	for (index=0; index<copy_size; index++) {
		cur_char = (int)backup_name[index];
		if (cur_char == 32 || (cur_char >= 48  && cur_char <= 57) || (cur_char >= 65 && cur_char <= 91) || cur_char == 93 || cur_char == 95 || (cur_char >= 97 && cur_char <= 123) || cur_char == 125 || cur_char == 45 || cur_char == 46) {
			// These are valid characters
			// Numbers
			// Upper case letters
			// Lower case letters
			// Space
			// and -_.{}[]
		} else {
			if (show_error)
				LOGE("Backup name '%s' contains invalid character: '%c'\n", backup_name, (char)cur_char);
			return -3;
		}
	}

	// Check to make sure that a backup with this name doesn't already exist
	strcpy(backup_loc, DataManager_GetStrValue(TW_BACKUPS_FOLDER_VAR));
	sprintf(tw_image_dir,"%s/%s/.", backup_loc, backup_name);
    if (stat(tw_image_dir, &st) == 0) {
		if (show_error)
			LOGE("A backup with this name already exists.\n");
		return -4;
	}

	// No problems found, return 0
	return 0;
}
