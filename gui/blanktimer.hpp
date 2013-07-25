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

#ifndef __BLANKTIMER_HEADER_HPP
#define __BLANKTIMER_HEADER_HPP

#include <pthread.h>
#include <sys/time.h>

using namespace std;

class blanktimer
{
public:
	blanktimer(void);

	int setTimerThread(void);
	void resetTimerAndUnblank(void);
	void setTime(int newtime);
	bool IsScreenOff();

private:
	typedef int (blanktimer::*ThreadPtr)(void);
	typedef void* (*PThreadPtr)(void*);

	void setConBlank(int blank);
	void setTimer(void);
	timespec getTimer(void);
	int getBrightness(void);
	int setBrightness(int brightness);
	int setBlankTimer(void);
	int setClockTimer(void);

	pthread_mutex_t conblankmutex;
	pthread_mutex_t timermutex;
	int conblank;
	timespec btimer;
	unsigned long long sleepTimer;
	int orig_brightness;
	bool screenoff;
};

extern blanktimer blankTimer;

#endif // __BLANKTIMER_HEADER_HPP
