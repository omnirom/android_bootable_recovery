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

#include "common.h"
#include "variables.h"
#include "data.hpp"
#include "makelist.hpp"
#include "twrp-functions.hpp"

int Makelist_File_Count;
unsigned long long Makelist_Current_Size;

int MakeList::Add_Item(string Item_Name) {
	char actual_filename[255];
	FILE *fp;

	if (Makelist_File_Count > 999) {
		LOGE("File count is too large!\n");
		return -1;
	}

	sprintf(actual_filename, "/tmp/list/filelist%03i", Makelist_File_Count);

	fp = fopen(actual_filename, "a");
	if (fp == NULL) {
		LOGE("Failed to open '%s'\n", actual_filename);
		return -1;
	}
	if (fprintf(fp, "%s\n", Item_Name.c_str()) < 0) {
		LOGE("Failed to write to '%s'\n", actual_filename);
		return -1;
	}
	if (fclose(fp) != 0) {
		LOGE("Failed to close '%s'\n", actual_filename);
		return -1;
	}
	return 0;
}

int MakeList::Generate_File_Lists(string Path) {
	DIR* d;
	struct dirent* de;
	struct stat st;
	string FileName;
	int has_data_media;

	DataManager::GetValue(TW_HAS_DATA_MEDIA, has_data_media);
	if (has_data_media == 1 && Path.size() >= 11 && strncmp(Path.c_str(), "/data/media", 11) == 0)
		return 0; // Skip /data/media

	d = opendir(Path.c_str());
	if (d == NULL)
	{
		LOGE("error opening '%s'\n", Path.c_str());
		return -1;
	}

	while ((de = readdir(d)) != NULL)
	{
		FileName = Path + "/";
		FileName += de->d_name;
		if (has_data_media == 1 && FileName.size() >= 11 && strncmp(FileName.c_str(), "/data/media", 11) == 0)
				continue; // Skip /data/media
		if (de->d_type == DT_DIR && strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0)
		{
			unsigned long long folder_size = TWFunc::Get_Folder_Size(FileName, false);
			if (Makelist_Current_Size + folder_size > MAX_ARCHIVE_SIZE) {
				if (Generate_File_Lists(FileName) < 0)
					return -1;
			} else {
				FileName += "/";
				if (Add_Item(FileName) < 0)
					return -1;
				Makelist_Current_Size += folder_size;
			}
		}
		else if (de->d_type == DT_REG || de->d_type == DT_LNK)
		{
			stat(FileName.c_str(), &st);

			if (Makelist_Current_Size != 0 && Makelist_Current_Size + st.st_size > MAX_ARCHIVE_SIZE) {
				Makelist_File_Count++;
				Makelist_Current_Size = 0;
			}
			if (Add_Item(FileName) < 0)
				return -1;
			Makelist_Current_Size += st.st_size;
			if (st.st_size > 2147483648LL)
				LOGE("There is a file that is larger than 2GB in the file system\n'%s'\nThis file may not restore properly\n", FileName.c_str());
		}
	}
	closedir(d);
	return 0;
}

int MakeList::Make_File_List(string Path)
{
	Makelist_File_Count = 0;
	Makelist_Current_Size = 0;
	system("cd /tmp && rm -rf list");
	system("cd /tmp && mkdir list");
	if (Generate_File_Lists(Path) < 0) {
		LOGE("Error generating file list\n");
		return -1;
	}
	LOGI("Done, generated %i files.\n", (Makelist_File_Count + 1));
	return (Makelist_File_Count + 1);
}
