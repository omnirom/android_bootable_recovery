/*
		Copyright 2013 to 2016 TeamWin
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
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/time.h>
#include <string>
#include <fstream>
#include <sstream>

#include "twadbstream.h"
#include "twrpback.hpp"

class twadbbu {
public:
	static bool Write_ADB_Stream_Header(uint64_t partition_count);                                 //Write ADB Stream Header to stream
	static bool Write_ADB_Stream_Trailer();                                                        //Write ADB Stream Trailer to stream
	static bool Write_TWFN(std::string Backup_FileName, uint64_t file_size, bool use_compression); //Write a tar image to stream
	static bool Write_TWIMG(std::string Backup_FileName, uint64_t file_size);                      //Write a partition image to stream
	static bool Write_TWEOF();                                                                     //Write ADB End-Of-File marker to stream
	static bool Write_TWERROR();                                                                   //Write error message occurred to stream
	static bool Write_TWENDADB();                                                                  //Write ADB End-Of-Stream command to stream
};
