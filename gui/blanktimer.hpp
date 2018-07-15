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

#include <sys/time.h>

using namespace std;

class blanktimer
{
public:
	blanktimer();

	// set timeout in seconds
	void setTime(int newtime);

	// call this in regular intervals
	void checkForTimeout();

	// call this when an input event is received or when an operation is finished
	void resetTimerAndUnblank();

	// call this when power button is pressed
	void toggleBlank(void);

	bool isScreenOff();

	void blank(void);

private:
	void setTimer(void);
	string getBrightness(void);

	pthread_mutex_t mutex;
	enum State { kOn = 0, kDim = 1, kOff = 2, kBlanked = 3 };
	State state;
	timespec btimer;
	long sleepTimer;
	string orig_brightness;
};

extern blanktimer blankTimer;

#endif // __BLANKTIMER_HEADER_HPP
