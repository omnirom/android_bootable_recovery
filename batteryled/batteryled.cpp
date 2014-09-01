/*
	Copyright 2014 Tom Hite (for TeamWin)
	Copyright 2014 TWRP/TeamWin Recovery Project
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
#include <unistd.h>

#include "../twrp-functions.hpp"
#include "batteryled.hpp"
extern "C" {
#include "../twcommon.h"
}

BatteryLed::BatteryLed(void) {
	checkDelay = DEFAULT_CHECK_DELAY_SECONDS;
	pthread_mutex_init(&delaymutex, NULL);
}

char BatteryLed::getChargingStatus(void) {
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

int BatteryLed::setChargingStatus(void) {
#ifdef HTC_LEGACY_LED
	return setHtcChargingStatus();
#endif
}

#ifdef HTC_LEGACY_LED
int BatteryLed::setHtcChargingStatus(void) {
	char status = getChargingStatus();

	string charging_file = "/sys/class/leds/amber/brightness";
	string full_file = "/sys/class/leds/green/brightness";
	string status_charging;
	string status_full;

	switch (status) {
		case 'C':
			status_charging = "1";
			status_full = "0";
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
	LOGINFO("Writing '%s' to battery led: %s\n",
		status_charging.c_str(), charging_file.c_str());
	LOGINFO("Writing '%s' to battery led: %s\n",
		status_full.c_str(), full_file.c_str());
#endif
	int write_status = TWFunc::write_file(charging_file, status_charging);
	if (write_status != 0) {
		LOGERR("Failed writing to battery led: %s\n", charging_file.c_str());
	} else {
		write_status = TWFunc::write_file(full_file, status_full);
		if (write_status != 0) {
			LOGERR("Failed writing to battery led: %s\n", full_file.c_str());
		}
	}

	return write_status;
}
#endif // HTC_LEGACY_LED

void BatteryLed::setDelay(int newdelay) {
	pthread_mutex_lock(&delaymutex);

	// Restrict range of delays
	if (newdelay < MIN_CHECK_DELAY_SECONDS) {
		LOGINFO("BatteryLED loop delay of %d sec not in range %d-%d sec; setting to %d sec\n",
			newdelay, MIN_CHECK_DELAY_SECONDS, MAX_CHECK_DELAY_SECONDS,
			MIN_CHECK_DELAY_SECONDS);
		newdelay = MIN_CHECK_DELAY_SECONDS;
	} else if (newdelay > MAX_CHECK_DELAY_SECONDS) {
		LOGINFO("BatteryLED loop delay of %d sec not in range %d-%d sec; setting to %d sec\n",
			newdelay, MIN_CHECK_DELAY_SECONDS, MAX_CHECK_DELAY_SECONDS,
			MAX_CHECK_DELAY_SECONDS);
		newdelay = MAX_CHECK_DELAY_SECONDS;
	} else {
		LOGINFO("BatteryLED loop delay changed to %d sec\n", newdelay);
	}
	checkDelay = newdelay;

	pthread_mutex_unlock(&delaymutex);
}

int BatteryLed::setTimerThread(void) {
	int success;
	pthread_t thread;
	ThreadPtr battptr = &BatteryLed::startBatteryCheckLoop;
	PThreadPtr p = *(PThreadPtr*)&battptr;
	success = pthread_create(&thread, NULL, p, this);
	if (success != 0) {
		LOGERR("BatteryLED thread cannot start; pthread_create error=%d\n",
			success);
	}
	return 0;
}

int BatteryLed::startBatteryCheckLoop(void) {
	int set_status = 0;
	LOGINFO("BatteryLED thread started with loop delay of %d sec\n",
		checkDelay);

	while (set_status == 0) {
		usleep(checkDelay * 1000000);
		set_status = setChargingStatus();
	}

	return 0;
}

void BatteryLed::init(void) {
// If a non-default loop delay needs to be set before the loop starts, it can
// be added here, before setTimerThread().
// #ifdef SOME_LED
// 	setDelay(4);
// #endif

	setTimerThread();
}
