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
#include "rapidxml.hpp"
using namespace rapidxml;
extern "C" {
#include "../minuitwrp/minui.h"
}
#include <string>
#include <vector>
#include <map>
#include "resources.hpp"
#include <pthread.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <pixelflinger/pixelflinger.h>
#include <linux/kd.h>
#include <linux/fb.h>
#include <sstream>
#include "pages.hpp"
#include "blanktimer.hpp"
#include "objects.hpp"
#include "../data.hpp"
extern "C" {
#include "../twcommon.h"
#ifdef HAVE_SELINUX
#include "../minzip/Zip.h"
#else
#include "../minzipold/Zip.h"
#endif
}
#include "../twrp-functions.hpp"
#include "../variables.h"

blanktimer::blanktimer(void) {
	setTime(0);
	setConBlank(0);
	orig_brightness = getBrightness();
	screenoff = false;
}

bool blanktimer::IsScreenOff() {
	return screenoff;
}

void blanktimer::setTime(int newtime) {
	sleepTimer = newtime;
}

int blanktimer::setTimerThread(void) {
	pthread_t thread;
	ThreadPtr blankptr = &blanktimer::setClockTimer;
	PThreadPtr p = *(PThreadPtr*)&blankptr;
	pthread_create(&thread, NULL, p, this);
	return 0;
}

void blanktimer::setConBlank(int blank) {
	pthread_mutex_lock(&conblankmutex);
	conblank = blank;
	pthread_mutex_unlock(&conblankmutex);
}

void blanktimer::setTimer(void) {
	pthread_mutex_lock(&timermutex);
	clock_gettime(CLOCK_MONOTONIC, &btimer);
	pthread_mutex_unlock(&timermutex);
}

timespec blanktimer::getTimer(void) {
	return btimer;
}

int  blanktimer::setClockTimer(void) {
	timespec curTime, diff;
	for(;;) {
		usleep(1000000);
		clock_gettime(CLOCK_MONOTONIC, &curTime);
		diff = TWFunc::timespec_diff(btimer, curTime);
		if (sleepTimer > 2 && diff.tv_sec > (sleepTimer - 2) && conblank == 0) {
			orig_brightness = getBrightness();
			setConBlank(1);
			setBrightness(5);
		}
		if (sleepTimer && diff.tv_sec > sleepTimer && conblank < 2) {
			setConBlank(2);
			setBrightness(0);
			screenoff = true;
			TWFunc::check_and_run_script("/sbin/postscreenblank.sh", "blank");
			PageManager::ChangeOverlay("lock");
		}
#ifndef TW_NO_SCREEN_BLANK
		if (conblank == 2 && gr_fb_blank(1) >= 0) {
			setConBlank(3);
		}
#endif
	}
	return -1; //shouldn't get here
}

int blanktimer::getBrightness(void) {
	string results;
	string brightness_path = EXPAND(TW_BRIGHTNESS_PATH);
	if ((TWFunc::read_file(brightness_path, results)) != 0)
		return -1;
	int result = atoi(results.c_str());
	if (result == 0) {
		int tw_brightness;
		DataManager::GetValue("tw_brightness", tw_brightness);
		if (tw_brightness) {
			result = tw_brightness;
		} else {
			result = 255;
		}
	}
	return result;

}

int blanktimer::setBrightness(int brightness) {
	string brightness_path = EXPAND(TW_BRIGHTNESS_PATH);
	string bstring;
	char buff[100];
	sprintf(buff, "%d", brightness);
	bstring = buff;
	if ((TWFunc::write_file(brightness_path, bstring)) != 0)
		return -1;
	return 0;
}

void blanktimer::resetTimerAndUnblank(void) {
	setTimer();
	switch (conblank) {
		case 3:
#ifndef TW_NO_SCREEN_BLANK
			if (gr_fb_blank(0) < 0) {
				LOGINFO("blanktimer::resetTimerAndUnblank failed to gr_fb_blank(0)\n");
				break;
			}
#endif
			TWFunc::check_and_run_script("/sbin/postscreenunblank.sh", "unblank");
			// No break here, we want to keep going
		case 2:
			gui_forceRender();
			screenoff = false;
			// No break here, we want to keep going
		case 1:
			setBrightness(orig_brightness);
			setConBlank(0);
			break;
	}
}
