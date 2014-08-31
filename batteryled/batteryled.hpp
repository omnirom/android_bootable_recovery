/*
	Copyright 2014 TWRP/TeamWin Recovery Project
	Copyright 2014 Tom Hite (for TeamWin)

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

#ifndef __BATTERYLED_HPP
#define __BATTERYLED_HPP

#include <pthread.h>
#include <sys/time.h>

#define MIN_CHECK_DELAY_SECONDS 2
#define DEFAULT_CHECK_DELAY_SECONDS 2
#define MAX_CHECK_DELAY_SECONDS 10

using namespace std;

class BatteryLed {
public:
	BatteryLed(void);
	void init(void);
	void setDelay(int newdelay); // newdelay in seconds

private:
	typedef int (BatteryLed::*ThreadPtr)(void);
	typedef void* (*PThreadPtr)(void*);
	pthread_mutex_t delaymutex;
	unsigned int checkDelay;

	int write_file(string fn, string& line);
	char getChargingStatus(void);
	int setChargingStatus(void);
	int setTimerThread(void);
	int startBatteryCheckLoop(void);

#ifdef HTC_LEGACY_LED
	int setHtcChargingStatus(void);
#endif
};

#endif // __BATTERYLED_HPP
