/*
        Copyright 2013 to 2016 TeamWin
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

#ifndef TWRPADBFIFO_HPP
#define TWRPADBFIFO_HPP

#include <string>
#include <pthread.h>

#define TW_ADB_FIFO "/tmp/twadbfifo"

class twrpAdbFifo {
	public:
		twrpAdbFifo(void);
		pthread_t threadAdbFifo(void);
	private:
		bool start(void);
		bool Backup_ADB_Command(std::string Options);
		bool Check_Adb_Fifo_For_Events(void);
		bool Restore_ADB_Backup(void);
		typedef bool (twrpAdbFifo::*ThreadPtr)(void);
		typedef void* (*PThreadPtr)(void *);
		int adb_fifo_fd;
};
#endif
