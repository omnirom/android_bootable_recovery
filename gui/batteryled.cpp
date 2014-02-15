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

using namespace std;

#include <string>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

extern "C"
{
#include "../twcommon.h"
}
#include "../twrp-functions.hpp"
#include "batteryled.hpp"

static char getChargingStatus() {
	char cap_s[2];

#ifdef TW_CUSTOM_BATTERY_PATH
	string status_file = EXPAND(TW_CUSTOM_BATTERY_PATH);
	status_file += "/status";
	FILE * cap = fopen(status_file.c_str(),"rt");
#else
	FILE * cap = fopen("/sys/class/power_supply/battery/status","rt");
#endif
	if (cap) {
		fgets(cap_s, 2, cap);
		fclose(cap);
	} else {
		cap_s[0] = 'N';
	}

	return cap_s[0];
}

static bool setCharging() {
	bool charging = false;
	int success = -1;

	char status = getChargingStatus();

	string charging_file = EXPAND(TW_HTC_CHARGING_LED_PATH);
	string full_file = EXPAND(TW_HTC_CHARGED_LED_PATH);
	string status_charging;
	string status_full;

	switch (status) {
		case 'C':
			status_charging = "1";
			status_full = "0";
			charging = true;
			break;
		case 'F':
			status_charging = "0";
			status_full = "1";
			break;
		default:
			status_charging = "0";
			status_full = "0";
			break;
	}

#ifdef _EVENT_LOGGING
	LOGINFO("Writing '%s' to battery led: %s\n", status_charging.c_str(), charging_file.c_str());
	LOGINFO("Writing '%s' to battery led: %s\n", status_full.c_str(), full_file.c_str());
#endif
	success = TWFunc::write_file(charging_file, status_charging);
	if (success != 0) {
		LOGERR("Failed writing to battery led: %s\n", charging_file.c_str());
	} else {
		success = TWFunc::write_file(full_file, status_full);
		if (success != 0) {
			LOGERR("Failed writing to battery led: %s\n", full_file.c_str());
		}
	}

	return (success == 0) ? charging : false;
}

batteryled::batteryled(void) {
	setDelay(DEFAULT_CHECK_DELAY_SECONDS);
	pthread_mutex_init(&delaymutex, NULL);
}

void batteryled::setDelay(int newdelay) {
	pthread_mutex_lock(&delaymutex);

	// Restrict range of delays
	if (newdelay < MIN_CHECK_DELAY_SECONDS) {
		newdelay = MIN_CHECK_DELAY_SECONDS;
	} else if (newdelay > MAX_CHECK_DELAY_SECONDS) {
		newdelay = MAX_CHECK_DELAY_SECONDS;
	}
	checkDelay = newdelay;

	pthread_mutex_unlock(&delaymutex);
}

int batteryled::setTimerThread(void) {
	int success;
	pthread_t thread;
	ThreadPtr battptr = &batteryled::startBatteryCheckLoop;
	PThreadPtr p = *(PThreadPtr*)&battptr;
	success = pthread_create(&thread, NULL, p, this);
	if (success != 0) {
		LOGERR("TWRP battery check thread cannot start; pthread_create failed with return %d\n", success);
	}
	return 0;
}

int batteryled::startBatteryCheckLoop(void) {
	LOGINFO("Entering TWRP battery check thread loop with delay timer set to %d sec\n", checkDelay);

	// sanity check checkDelay
	while (checkDelay != 0 && checkDelay >= MIN_CHECK_DELAY_SECONDS && checkDelay <= MAX_CHECK_DELAY_SECONDS) {
		usleep(checkDelay * 1000000);
		setCharging();
	}

	LOGERR("Exiting TWRP battery check thread because delay timer (%d sec) set outside of allowed range (%d-%d sec)\n", checkDelay, MIN_CHECK_DELAY_SECONDS, MAX_CHECK_DELAY_SECONDS);
	return 0;
}

