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

using namespace std;
#include <string>
#include <pthread.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include "pages.hpp"
#include "blanktimer.hpp"
#include "../data.hpp"
extern "C" {
#include "../minuitwrp/minui.h"
#include "../twcommon.h"
}
#include "../twrp-functions.hpp"
#include "../variables.h"

blanktimer::blanktimer(void) {
	pthread_mutex_init(&mutex, NULL);
	setTime(0); // no timeout
	state = kOn;
	orig_brightness = getBrightness();
}

bool blanktimer::isScreenOff() {
	return state >= kOff;
}

void blanktimer::setTime(int newtime) {
	pthread_mutex_lock(&mutex);
	sleepTimer = newtime;
	pthread_mutex_unlock(&mutex);
}

void blanktimer::setTimer(void) {
	clock_gettime(CLOCK_MONOTONIC, &btimer);
}

void blanktimer::checkForTimeout() {
#ifndef TW_NO_SCREEN_TIMEOUT
	pthread_mutex_lock(&mutex);
	timespec curTime, diff;
	clock_gettime(CLOCK_MONOTONIC, &curTime);
	diff = TWFunc::timespec_diff(btimer, curTime);
	if (sleepTimer > 2 && diff.tv_sec > (sleepTimer - 2) && state == kOn) {
		orig_brightness = getBrightness();
		state = kDim;
		TWFunc::Set_Brightness("5");
	}
	if (sleepTimer && diff.tv_sec > sleepTimer && state < kOff) {
		state = kOff;
		TWFunc::Set_Brightness("0");
		TWFunc::check_and_run_script("/sbin/postscreenblank.sh", "blank");
		PageManager::ChangeOverlay("lock");
	}
#ifndef TW_NO_SCREEN_BLANK
	if (state == kOff && gr_fb_blank(1) >= 0) {
		state = kBlanked;
	}
#endif
	pthread_mutex_unlock(&mutex);
#endif
}

string blanktimer::getBrightness(void) {
	string result;

	if (DataManager::GetIntValue("tw_has_brightnesss_file")) {
		DataManager::GetValue("tw_brightness", result);
		if (result.empty())
			result = "255";
	}
	return result;
}

void blanktimer::resetTimerAndUnblank(void) {
#ifndef TW_NO_SCREEN_TIMEOUT
	pthread_mutex_lock(&mutex);
	setTimer();
	switch (state) {
		case kBlanked:
#ifndef TW_NO_SCREEN_BLANK
			if (gr_fb_blank(0) < 0) {
				LOGINFO("blanktimer::resetTimerAndUnblank failed to gr_fb_blank(0)\n");
				break;
			}
#endif
			// TODO: this is asymmetric with postscreenblank.sh - shouldn't it be under the next case label?
			TWFunc::check_and_run_script("/sbin/postscreenunblank.sh", "unblank");
			// No break here, we want to keep going
		case kOff:
			gui_forceRender();
			// No break here, we want to keep going
		case kDim:
			if (!orig_brightness.empty())
				TWFunc::Set_Brightness(orig_brightness);
			state = kOn;
		case kOn:
			break;
	}
	pthread_mutex_unlock(&mutex);
#endif
}
