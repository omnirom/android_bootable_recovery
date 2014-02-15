/*
		Copyright 2014 Tom Hite (for TeamWin)
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

#ifndef __BATTERYLED_HEADER_HPP
#define __BATTERYLED_HEADER_HPP

#include <pthread.h>
#include <sys/time.h>

#define MIN_CHECK_DELAY_SECONDS 2
#define DEFAULT_CHECK_DELAY_SECONDS 2
#define MAX_CHECK_DELAY_SECONDS 10

using namespace std;

class batteryled
{
public:
	batteryled(void);

	int setTimerThread(void);
	void setDelay(int newdelay);

private:
	typedef int (batteryled::*ThreadPtr)(void);
	typedef void* (*PThreadPtr)(void*);

	int  startBatteryCheckLoop(void);

	pthread_mutex_t delaymutex;
	unsigned int checkDelay;
};

#endif // __BATTERYLED_HEADER_HPP
