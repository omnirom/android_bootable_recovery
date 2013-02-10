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
#include "../minzip/Zip.h"
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
extern "C" {
#include "../common.h"
#include "../recovery_ui.h"
}
#include "../twrp-functions.hpp"
#include "../variables.h"

blanktimer::blanktimer(void) {
	blanked = 0;
	sleepTimer = 60;
	orig_brightness = getBrightness();
}

int blanktimer::setTimerThread(void) {
	pthread_t thread;
	ThreadPtr blankptr = &blanktimer::setClockTimer;
	PThreadPtr p = *(PThreadPtr*)&blankptr;
	pthread_create(&thread, NULL, p, this);
	return 0;
}

void blanktimer::setBlank(int blank) {
	pthread_mutex_lock(&blankmutex);
	conblank = blank;
	pthread_mutex_unlock(&blankmutex);
}

int blanktimer::getBlank(void) {
	return conblank;
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
	while(1) {
		usleep(1000);
		clock_gettime(CLOCK_MONOTONIC, &curTime);
		diff = TWFunc::timespec_diff(btimer, curTime);
		if (diff.tv_sec > sleepTimer && conblank != 1)
			setBlank(1);
		if (conblank == 1 && blanked != 1) {
			blanked = 1;
			gr_fb_blank(conblank);
			setBrightness(0);
			PageManager::ChangeOverlay("lock");
		}
	}
	return -1;
}

int blanktimer::getBrightness(void) {
	string results;
	string brightness_path = EXPAND(TW_BRIGHTNESS_PATH);
	if ((TWFunc::read_file(brightness_path, results)) != 0)
		return -1;
	return atoi(results.c_str());

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
	if (blanked) {
		setBrightness(orig_brightness);
		blanked = 0;
		setBlank(0);
		gr_fb_blank(conblank);
		gui_forceRender();
	}
}
