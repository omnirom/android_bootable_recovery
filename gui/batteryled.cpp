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

/* internal functions */
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
	int success1 = -1;
	int success2 = -1;

#if defined(TW_CHARGING_LED_PATH)
 #if defined(TW_CHARGED_LED_PATH)
	char status = getChargingStatus();

	string charging_file = EXPAND(TW_CHARGING_LED_PATH);
	string full_file = EXPAND(TW_CHARGED_LED_PATH);
	string status_charging;
	string status_full;

	// three  possibilities -- charging, plugged in and full, or not plugged in
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
	LOGINFO("Writing '%s' to battery led: '%s'\n", status_charging.c_str(), charging_file.c_str());
	LOGINFO("Writing '%s' to battery led: '%s'\n", status_full.c_str(), full_file.c_str());
  #endif
	success1 = TWFunc::write_file(charging_file, status_charging);
	success2 = TWFunc::write_file(full_file, status_full);
 #else
	#error "TWRP: Failing compilation because TW_CHARGING_LED_PATH was defined, but TW_CHARGED_LED_PATH was not!"
 #endif
#else
 #pragma message("TWRP: Not including battery led setting because TW_CHARGING_LED_PATH was not defined.")
#endif

	return (success1 == 0 && success2 == 0) ? charging : false;
}

/* class implementation */
batteryled::batteryled(void) {
	setDelay(DEFAULT_CHECK_DELAY_SECONDS); // default writes leds every two seconds
	pthread_mutex_init(&delaymutex, NULL);
}

void batteryled::setDelay(int newdelay /* in seconds */) {
	pthread_mutex_lock(&delaymutex);

	// Never allow idiotic thread spinning or delays, but let zero by as
	// setting to zero indicates exiting the check/write thread.
	if ((newdelay < 0) || (newdelay > 0 && newdelay < DEFAULT_CHECK_DELAY_SECONDS)) {
		// The consideration is that negatives are mistakes, and under a couple
		// seconds is greedy -- clamp to default.
		newdelay = DEFAULT_CHECK_DELAY_SECONDS;
	} else if (newdelay > MAX_CHECK_DELAY_SECONDS) {
 		// over ten seconds is likely confusing for user feedback -- clamp to max.
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
		LOGINFO("TWRP battery check thread cannot start -- pthread_create failed with return: %d.\n", success);
	}
	return 0;
}

int batteryled::startBatteryCheckLoop(void) {
	LOGINFO("Entering TWRP battery check thread loop with delay timer set to: %d.\n", checkDelay);

	// really probably don't need sanity checks, but just in case
	while (checkDelay != 0 && checkDelay >= DEFAULT_CHECK_DELAY_SECONDS && checkDelay <= MAX_CHECK_DELAY_SECONDS) {
		usleep(checkDelay * 1000000);
		setCharging();
	}

	LOGINFO("Exiting TWRP battery check thread because delay timer was set to: %d.\n", checkDelay);
	return 0;
}

