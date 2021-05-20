/*
	Copyright 2014 to 2020 TeamWin
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

#include <string>
#include "partitions.hpp"

#ifndef TWRP_REPACKER
#define TWRP_REPACKER

enum Repack_Type {
	REPLACE_NONE = 0,
	REPLACE_RAMDISK = 1,
	REPLACE_KERNEL = 2,
	REPLACE_RAMDISK_UNPACKED = 3,
};

struct Repack_Options_struct {
	Repack_Type Type;
	bool Backup_First;
	bool Disable_Verity;
	bool Disable_Force_Encrypt;
};

class twrpRepacker {
    public:
        bool Backup_Image_For_Repack(TWPartition* Part, const std::string& Temp_Folder_Destination, const bool Create_Backup, const std::string& Backup_Name); // Prepares an image for repacking by unpacking it to the temp folder destination
        std::string Unpack_Image(const std::string& Source_Path, const std::string& Temp_Folder_Destination, const bool Copy_Source, const bool Create_Destination = true); // Prepares an image for repacking by unpacking it to the temp folder destination and return the ramdisk format
        bool Repack_Image_And_Flash(const std::string& Target_Image, const struct Repack_Options_struct& Repack_Options); // Repacks the boot image with a new kernel or a new ramdisk
        bool Flash_Current_Twrp();
    private:
    	bool Prepare_Empty_Folder(const std::string& Folder); // Creates an empty folder at Folder. If the folder already exists, the folder is deleted, then created
    	std::string original_ramdisk_format;                  // Ramdisk format of boot partition
	    std::string image_ramdisk_format;                     // Ramdisk format of boot image to repack from
};
#endif // TWRP_REPACKER
