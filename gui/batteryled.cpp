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
extern "C"
{
#include "../twcommon.h"
}
#include "../twrp-functions.hpp"
#include "batteryled.hpp"

batteryled::batteryled(void) {
	/* nothing to do yet */
}

bool getCharging(void) {
	// assume failure
	int success = -1;
	bool charging = false;

#ifdef TW_CHARGING_LED_PATH
	string results;
	string charging_file = EXPAND(TW_CHARGING_LED_PATH);
	success = TWFunc::read_file(charging_file, results);
	if (success == 0 && results.size() > 0) {
		charging = results[0] != '0';
	}
#endif

	return charging;
}

bool batteryled::setCharging(bool charging, char status) {
	int success = -1;

#if defined(TW_CHARGING_LED_PATH) && defined(TW_CHARGING_LED_PATH)
	string charging_file = EXPAND(TW_CHARGING_LED_PATH);
	string charged_file = EXPAND(TW_CHARGED_LED_PATH);
	string status_charging;
	string status_full;

	// three  possibilities -- charging, plugged in and full, or not plugged in
	status_charging = charging ? "1" : "0";
	status_full = status == 'F' ? "1" : "0";

	success = TWFunc::write_file(charging_file, status_charging) &&
		TWFunc::write_file(charged_file, status_full);
#endif

	return success == 0 ? charging : false;
}

