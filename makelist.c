/* This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * The code was written from scratch by Dees_Troy dees_troy at
 * yahoo
 *
 * Copyright (c) 2012
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

#include "extra-functions.h"
#include "common.h"
#include "data.h"
#include "variables.h"

int makelist_file_count;
unsigned long long makelist_current_size;

unsigned long long getUsedSizeViaDu(const char* path)
{
    char cmd[512];
    sprintf(cmd, "du -sk %s | awk '{ print $1 }'", path);

    FILE *fp;
    fp = __popen(cmd, "r");
    
    char str[512];
    fgets(str, sizeof(str), fp);
    __pclose(fp);

    unsigned long long size = atol(str);
    size *= 1024ULL;

    return size;
}

int add_item(const char* item_name) {
	char actual_filename[255];
	FILE *fp;

	if (makelist_file_count > 999) {
		LOGE("File count is too large!\n");
		return -1;
	}

	sprintf(actual_filename, "/tmp/list/filelist%03i", makelist_file_count);

	fp = fopen(actual_filename, "a");
	if (fp == NULL) {
		LOGE("Failed to open '%s'\n", actual_filename);
		return -1;
	}
	if (fprintf(fp, "%s\n", item_name) < 0) {
		LOGE("Failed to write to '%s'\n", actual_filename);
		return -1;
	}
	if (fclose(fp) != 0) {
		LOGE("Failed to close '%s'\n", actual_filename);
		return -1;
	}
	return 0;
}

int generate_file_lists(const char* path) {
	DIR* d;
	struct dirent* de;
	struct stat st;
	char path2[255], filename[255];

	if (DataManager_GetIntValue(TW_HAS_DATA_MEDIA) == 1 && strlen(path) >= 11 && strncmp(path, "/data/media", 11) == 0)
		return 0; // Skip /data/media

	// Make a copy of path in case the data in the pointer gets overwritten later
	strcpy(path2, path);

	d = opendir(path2);
	if (d == NULL)
	{
		LOGE("error opening '%s'\n", path2);
		return -1;
	}

	while ((de = readdir(d)) != NULL)
	{
		if (de->d_type == DT_DIR && strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0)
		{
			strcpy(filename, path2);
			strcat(filename, "/");
			strcat(filename, de->d_name);
			if (DataManager_GetIntValue(TW_HAS_DATA_MEDIA) == 1 && strlen(filename) >= 11 && strncmp(filename, "/data/media", 11) == 0)
				continue; // Skip /data/media
			unsigned long long folder_size = getUsedSizeViaDu(filename);
			if (makelist_current_size + folder_size > MAX_ARCHIVE_SIZE) {
				if (generate_file_lists(filename) < 0)
					return -1;
			} else {
				strcat(filename, "/");
				if (add_item(filename) < 0)
					return -1;
				makelist_current_size += folder_size;
			}
		}
		else if (de->d_type == DT_REG)
		{
			if (DataManager_GetIntValue(TW_HAS_DATA_MEDIA) == 1 && strlen(path2) >= 11 && strncmp(path2, "/data/media", 11) == 0)
				continue; // Skip /data/media
			strcpy(filename, path2);
			strcat(filename, "/");
			strcat(filename, de->d_name);
			stat(filename, &st);

			if (makelist_current_size != 0 && makelist_current_size + st.st_size > MAX_ARCHIVE_SIZE) {
				makelist_file_count += 1;
				makelist_current_size = 0;
			}
			if (add_item(filename) < 0)
				return -1;
			makelist_current_size += st.st_size;
			if (st.st_size > 2147483648LL)
				LOGE("There is a file that is larger than 2GB in the file system\n'%s'\nThis file may not restore properly\n", filename);
		}
	}
	closedir(d);
	return 0;
}

int make_file_list(char* path)
{
	makelist_file_count = 0;
	makelist_current_size = 0;
	__system("cd /tmp && rm -rf list");
	__system("cd /tmp && mkdir list");
	if (generate_file_lists(path) < 0) {
		LOGE("Error generating file list\n");
		return -1;
	}
	LOGI("Done, generated %i files.\n", (makelist_file_count + 1));
	return (makelist_file_count + 1);
}
