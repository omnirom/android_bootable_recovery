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

#include <linux/input.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>

extern "C"
{
#include "../twcommon.h"
#include "../minuitwrp/minui.h"
#ifdef HAVE_SELINUX
#include "../minzip/Zip.h"
#else
#include "../minzipold/Zip.h"
#endif
#include <pixelflinger/pixelflinger.h>
}

#include "rapidxml.hpp"
#include "objects.hpp"
#include "../data.hpp"
#include "../variables.h"
#include "../partitions.hpp"
#include "../twrp-functions.hpp"
#ifndef TW_NO_SCREEN_TIMEOUT
#include "blanktimer.hpp"
#endif

const static int CURTAIN_FADE = 32;

using namespace rapidxml;

// Global values
static gr_surface gCurtain = NULL;
static int gGuiInitialized = 0;
static int gGuiConsoleRunning = 0;
static int gGuiConsoleTerminate = 0;
static int gForceRender = 0;
pthread_mutex_t gForceRendermutex;
static int gNoAnimation = 1;
static int gGuiInputRunning = 0;
#ifndef TW_NO_SCREEN_TIMEOUT
blanktimer blankTimer;
#endif

// Needed by pages.cpp too
int gGuiRunning = 0;

static int gRecorder = -1;

extern "C" void gr_write_frame_to_file(int fd);

void flip(void)
{
	if (gRecorder != -1)
	{
		timespec time;
		clock_gettime(CLOCK_MONOTONIC, &time);
		write(gRecorder, &time, sizeof(timespec));
		gr_write_frame_to_file(gRecorder);
	}
	gr_flip();
}

void rapidxml::parse_error_handler(const char *what, void *where)
{
	fprintf(stderr, "Parser error: %s\n", what);
	fprintf(stderr, "  Start of string: %s\n",(char *) where);
	LOGERR("Error parsing XML file.\n");
	//abort();
}

static void curtainSet()
{
	gr_color(0, 0, 0, 255);
	gr_fill(0, 0, gr_fb_width(), gr_fb_height());
	gr_blit(gCurtain, 0, 0, gr_get_width(gCurtain), gr_get_height(gCurtain), 0, 0);
	gr_flip();
}

static void curtainRaise(gr_surface surface)
{
	int sy = 0;
	int h = gr_get_height(gCurtain) - 1;
	int w = gr_get_width(gCurtain);
	int fy = 1;

	int msw = gr_get_width(surface);
	int msh = gr_get_height(surface);
	int CURTAIN_RATE = msh / 30;

	if (gNoAnimation == 0)
	{
		for (; h > 0; h -= CURTAIN_RATE, sy += CURTAIN_RATE, fy += CURTAIN_RATE)
		{
			gr_blit(surface, 0, 0, msw, msh, 0, 0);
			gr_blit(gCurtain, 0, sy, w, h, 0, 0);
			gr_flip();
		}
	}
	gr_blit(surface, 0, 0, msw, msh, 0, 0);
	flip();
}

void curtainClose()
{
#if 0
	int w = gr_get_width(gCurtain);
	int h = 1;
	int sy = gr_get_height(gCurtain) - 1;
	int fbh = gr_fb_height();
	int CURTAIN_RATE = fbh / 30;

	if (gNoAnimation == 0)
	{
		for (; h < fbh; h += CURTAIN_RATE, sy -= CURTAIN_RATE)
		{
			gr_blit(gCurtain, 0, sy, w, h, 0, 0);
			gr_flip();
		}
		gr_blit(gCurtain, 0, 0, gr_get_width(gCurtain),
		gr_get_height(gCurtain), 0, 0);
		gr_flip();

		if (gRecorder != -1)
			close(gRecorder);

		int fade;
		for (fade = 16; fade < 255; fade += CURTAIN_FADE)
		{
			gr_blit(gCurtain, 0, 0, gr_get_width(gCurtain),
			gr_get_height(gCurtain), 0, 0);
			gr_color(0, 0, 0, fade);
			gr_fill(0, 0, gr_fb_width(), gr_fb_height());
			gr_flip();
		}
		gr_color(0, 0, 0, 255);
		gr_fill(0, 0, gr_fb_width(), gr_fb_height());
		gr_flip();
	}
#else
	gr_blit(gCurtain, 0, 0, gr_get_width(gCurtain), gr_get_height(gCurtain), 0, 0);
	gr_flip();
#endif
}

static void * input_thread(void *cookie)
{

	int drag = 0;
	static int touch_and_hold = 0, dontwait = 0;
	static int touch_repeat = 0, key_repeat = 0;
	static int x = 0, y = 0;
	static int lshift = 0, rshift = 0;
	static struct timeval touchStart;
	HardwareKeyboard kb;
	string seconds;

#ifndef TW_NO_SCREEN_TIMEOUT
	//start screen timeout threads
	blankTimer.setTimerThread();
	DataManager::GetValue("tw_screen_timeout_secs", seconds);
	blankTimer.setTime(atoi(seconds.c_str()));
#else
	LOGINFO("Skipping screen timeout threads: TW_NO_SCREEN_TIMEOUT is set\n");
#endif

	for (;;)
	{
		// wait for the next event
		struct input_event ev;
		int state = 0, ret = 0;

		ret = ev_get(&ev, dontwait);

		if (ret < 0)
		{
			struct timeval curTime;
			gettimeofday(&curTime, NULL);
			long mtime, seconds, useconds;

			seconds = curTime.tv_sec - touchStart.tv_sec;
			useconds = curTime.tv_usec - touchStart.tv_usec;

			mtime = ((seconds) * 1000 + useconds / 1000.0) + 0.5;
			if (touch_and_hold && mtime > 500)
			{
				touch_and_hold = 0;
				touch_repeat = 1;
				gettimeofday(&touchStart, NULL);
#ifdef _EVENT_LOGGING
				LOGERR("TOUCH_HOLD: %d,%d\n", x, y);
#endif
				PageManager::NotifyTouch(TOUCH_HOLD, x, y);
#ifndef TW_NO_SCREEN_TIMEOUT
				blankTimer.resetTimerAndUnblank();
#endif
			}
			else if (touch_repeat && mtime > 100)
			{
#ifdef _EVENT_LOGGING
				LOGERR("TOUCH_REPEAT: %d,%d\n", x, y);
#endif
				gettimeofday(&touchStart, NULL);
				PageManager::NotifyTouch(TOUCH_REPEAT, x, y);
#ifndef TW_NO_SCREEN_TIMEOUT
				blankTimer.resetTimerAndUnblank();
#endif
			}
			else if (key_repeat == 1 && mtime > 500)
			{
#ifdef _EVENT_LOGGING
				LOGERR("KEY_HOLD: %d,%d\n", x, y);
#endif
				gettimeofday(&touchStart, NULL);
				key_repeat = 2;
				kb.KeyRepeat();
#ifndef TW_NO_SCREEN_TIMEOUT
				blankTimer.resetTimerAndUnblank();
#endif

			}
			else if (key_repeat == 2 && mtime > 100)
			{
#ifdef _EVENT_LOGGING
				LOGERR("KEY_REPEAT: %d,%d\n", x, y);
#endif
				gettimeofday(&touchStart, NULL);
				kb.KeyRepeat();
#ifndef TW_NO_SCREEN_TIMEOUT
				blankTimer.resetTimerAndUnblank();
#endif
			}
		}
		else if (ev.type == EV_ABS)
		{

			x = ev.value >> 16;
			y = ev.value & 0xFFFF;

			if (ev.code == 0)
			{
				if (state == 0)
				{
#ifdef _EVENT_LOGGING
					LOGERR("TOUCH_RELEASE: %d,%d\n", x, y);
#endif
					PageManager::NotifyTouch(TOUCH_RELEASE, x, y);
#ifndef TW_NO_SCREEN_TIMEOUT
					blankTimer.resetTimerAndUnblank();
#endif
					touch_and_hold = 0;
					touch_repeat = 0;
					if (!key_repeat)
						dontwait = 0;
				}
				state = 0;
				drag = 0;
			}
			else
			{
				if (!drag)
				{
#ifdef _EVENT_LOGGING
					LOGERR("TOUCH_START: %d,%d\n", x, y);
#endif
					if (PageManager::NotifyTouch(TOUCH_START, x, y) > 0)
						state = 1;
					drag = 1;
					touch_and_hold = 1;
					dontwait = 1;
					key_repeat = 0;
					gettimeofday(&touchStart, NULL);
#ifndef TW_NO_SCREEN_TIMEOUT
					blankTimer.resetTimerAndUnblank();
#endif
				}
				else
				{
					if (state == 0)
					{
#ifdef _EVENT_LOGGING
						LOGERR("TOUCH_DRAG: %d,%d\n", x, y);
#endif
						if (PageManager::NotifyTouch(TOUCH_DRAG, x, y) > 0)
							state = 1;
						key_repeat = 0;
#ifndef TW_NO_SCREEN_TIMEOUT
						blankTimer.resetTimerAndUnblank();
#endif
					}
				}
			}
		}
		else if (ev.type == EV_KEY)
		{
			// Handle key-press here
#ifdef _EVENT_LOGGING
			LOGERR("TOUCH_KEY: %d\n", ev.code);
#endif
			if (ev.value != 0)
			{
				// This is a key press
				if (kb.KeyDown(ev.code))
				{
					key_repeat = 1;
					touch_and_hold = 0;
					touch_repeat = 0;
					dontwait = 1;
					gettimeofday(&touchStart, NULL);
#ifndef TW_NO_SCREEN_TIMEOUT
					blankTimer.resetTimerAndUnblank();
#endif
				}
				else
				{
					key_repeat = 0;
					touch_and_hold = 0;
					touch_repeat = 0;
					dontwait = 0;
#ifndef TW_NO_SCREEN_TIMEOUT
					blankTimer.resetTimerAndUnblank();
#endif
				}
			}
			else
			{
				// This is a key release
				kb.KeyUp(ev.code);
				key_repeat = 0;
				touch_and_hold = 0;
				touch_repeat = 0;
				dontwait = 0;
#ifndef TW_NO_SCREEN_TIMEOUT
				blankTimer.resetTimerAndUnblank();
#endif
			}
		}
	}
	return NULL;
}

// This special function will return immediately the first time, but then
// always returns 1/30th of a second (or immediately if called later) from
// the last time it was called
static void loopTimer(void)
{
	static timespec lastCall;
	static int initialized = 0;

	if (!initialized)
	{
		clock_gettime(CLOCK_MONOTONIC, &lastCall);
		initialized = 1;
		return;
	}

	do
	{
		timespec curTime;
		clock_gettime(CLOCK_MONOTONIC, &curTime);

		timespec diff = TWFunc::timespec_diff(lastCall, curTime);

		// This is really 30 times per second
		if (diff.tv_sec || diff.tv_nsec > 33333333)
		{
			lastCall = curTime;
			return;
		}

		// We need to sleep some period time microseconds
		unsigned int sleepTime = 33333 -(diff.tv_nsec / 1000);
		usleep(sleepTime);
	} while (1);
}

static int runPages(void)
{
	// Raise the curtain
	if (gCurtain != NULL)
	{
		gr_surface surface;

		PageManager::Render();
		gr_get_surface(&surface);
		curtainRaise(surface);
		gr_free_surface(surface);
	}

	gGuiRunning = 1;

	DataManager::SetValue("tw_loaded", 1);

	for (;;)
	{
		loopTimer();

		if (!gForceRender)
		{
			int ret;

			ret = PageManager::Update();
			if (ret > 1)
				PageManager::Render();

			if (ret > 0)
				flip();
		}
		else
		{
			pthread_mutex_lock(&gForceRendermutex);
			gForceRender = 0;
			pthread_mutex_unlock(&gForceRendermutex);
			PageManager::Render();
			flip();
		}

		if (DataManager::GetIntValue("tw_gui_done") != 0)
			break;
	}

	gGuiRunning = 0;
	return 0;
}

static int runPage(const char *page_name)
{
	gui_changePage(page_name);

	// Raise the curtain
	if (gCurtain != NULL)
	{
		gr_surface surface;

		PageManager::Render();
		gr_get_surface(&surface);
		curtainRaise(surface);
		gr_free_surface(surface);
	}

	gGuiRunning = 1;

	DataManager::SetValue("tw_loaded", 1);

	for (;;)
	{
		loopTimer();

		if (!gForceRender)
		{
			int ret;

			ret = PageManager::Update();
			if (ret > 1)
				PageManager::Render();

			if (ret > 0)
				flip();
		}
		else
		{
			pthread_mutex_lock(&gForceRendermutex);
			gForceRender = 0;
			pthread_mutex_unlock(&gForceRendermutex);
			PageManager::Render();
			flip();
		}
		if (DataManager::GetIntValue("tw_page_done") != 0)
		{
			gui_changePage("main");
			break;
		}
	}

	gGuiRunning = 0;
	return 0;
}

int gui_forceRender(void)
{
	pthread_mutex_lock(&gForceRendermutex);
	gForceRender = 1;
	pthread_mutex_unlock(&gForceRendermutex);
	return 0;
}

int gui_changePage(std::string newPage)
{
	LOGINFO("Set page: '%s'\n", newPage.c_str());
	PageManager::ChangePage(newPage);
	pthread_mutex_lock(&gForceRendermutex);
	gForceRender = 1;
	pthread_mutex_unlock(&gForceRendermutex);
	return 0;
}

int gui_changeOverlay(std::string overlay)
{
	PageManager::ChangeOverlay(overlay);
	pthread_mutex_lock(&gForceRendermutex);
	gForceRender = 1;
	pthread_mutex_unlock(&gForceRendermutex);
	return 0;
}

int gui_changePackage(std::string newPackage)
{
	PageManager::SelectPackage(newPackage);
	pthread_mutex_lock(&gForceRendermutex);
	gForceRender = 1;
	pthread_mutex_unlock(&gForceRendermutex);
	return 0;
}

std::string gui_parse_text(string inText)
{
	// Copied from std::string GUIText::parseText(void)
	// This function parses text for DataManager values encompassed by %value% in the XML
	static int counter = 0;
	std::string str = inText;
	size_t pos = 0;
	size_t next = 0, end = 0;

	while (1)
	{
		next = str.find('%', pos);
		if (next == std::string::npos)
			return str;

		end = str.find('%', next + 1);
		if (end == std::string::npos)
			return str;

		// We have a block of data
		std::string var = str.substr(next + 1,(end - next) - 1);
		str.erase(next,(end - next) + 1);

		if (next + 1 == end)
			str.insert(next, 1, '%');
		else
		{
			std::string value;
			if (DataManager::GetValue(var, value) == 0)
				str.insert(next, value);
		}

		pos = next + 1;
	}
}

extern "C" int gui_init(void)
{
	int fd;

	gr_init();

	if (res_create_surface("/res/images/curtain.jpg", &gCurtain))
	{
		printf
		("Unable to locate '/res/images/curtain.jpg'\nDid you set a DEVICE_RESOLUTION in your config files?\n");
		return -1;
	}

	curtainSet();

	ev_init();
	return 0;
}

extern "C" int gui_loadResources(void)
{
	//    unlink("/sdcard/video.last");
	//    rename("/sdcard/video.bin", "/sdcard/video.last");
	//    gRecorder = open("/sdcard/video.bin", O_CREAT | O_WRONLY);

	int check = 0;
	DataManager::GetValue(TW_IS_ENCRYPTED, check);
	if (check)
	{
		if (PageManager::LoadPackage("TWRP", "/res/ui.xml", "decrypt"))
		{
			LOGERR("Failed to load base packages.\n");
			goto error;
		}
		else
			check = 1;
	}

	if (check == 0 && PageManager::LoadPackage("TWRP", "/script/ui.xml", "main"))
	{
		std::string theme_path;

		theme_path = DataManager::GetSettingsStoragePath();
		if (!PartitionManager.Mount_Settings_Storage(false))
		{
			int retry_count = 5;
			while (retry_count > 0 && !PartitionManager.Mount_Settings_Storage(false))
			{
				usleep(500000);
				retry_count--;
			}

			if (!PartitionManager.Mount_Settings_Storage(false))
			{
				LOGERR("Unable to mount %s during GUI startup.\n",
					   theme_path.c_str());
				check = 1;
			}
		}

		theme_path += "/TWRP/theme/ui.zip";
		if (check || PageManager::LoadPackage("TWRP", theme_path, "main"))
		{
			if (PageManager::LoadPackage("TWRP", "/res/ui.xml", "main"))
			{
				LOGERR("Failed to load base packages.\n");
				goto error;
			}
		}
	}

	// Set the default package
	PageManager::SelectPackage("TWRP");

	gGuiInitialized = 1;
	return 0;

error:
	LOGERR("An internal error has occurred.\n");
	gGuiInitialized = 0;
	return -1;
}

extern "C" int gui_start(void)
{
	if (!gGuiInitialized)
		return -1;

	gGuiConsoleTerminate = 1;

	while (gGuiConsoleRunning)
		loopTimer();

	// Set the default package
	PageManager::SelectPackage("TWRP");

	if (!gGuiInputRunning)
	{
		// Start by spinning off an input handler.
		pthread_t t;
		pthread_create(&t, NULL, input_thread, NULL);
		gGuiInputRunning = 1;
	}

	return runPages();
}

extern "C" int gui_startPage(const char *page_name)
{
	if (!gGuiInitialized)
		return -1;

	gGuiConsoleTerminate = 1;

	while (gGuiConsoleRunning)
		loopTimer();

	// Set the default package
	PageManager::SelectPackage("TWRP");

	if (!gGuiInputRunning)
	{
		// Start by spinning off an input handler.
		pthread_t t;
		pthread_create(&t, NULL, input_thread, NULL);
		gGuiInputRunning = 1;
	}

	DataManager::SetValue("tw_page_done", 0);
	return runPage(page_name);
}

static void * console_thread(void *cookie)
{
	PageManager::SwitchToConsole();

	while (!gGuiConsoleTerminate)
	{
		loopTimer();

		if (!gForceRender)
		{
			int ret;

			ret = PageManager::Update();
			if (ret > 1)
				PageManager::Render();

			if (ret > 0)
				flip();

			if (ret < 0)
				LOGERR("An update request has failed.\n");
		}
		else
		{
			pthread_mutex_lock(&gForceRendermutex);
			gForceRender = 0;
			pthread_mutex_unlock(&gForceRendermutex);
			PageManager::Render();
			flip();
		}
	}
	gGuiConsoleRunning = 0;
	return NULL;
}

extern "C" int gui_console_only(void)
{
	if (!gGuiInitialized)
		return -1;

	gGuiConsoleTerminate = 0;
	gGuiConsoleRunning = 1;

	// Start by spinning off an input handler.
	pthread_t t;
	pthread_create(&t, NULL, console_thread, NULL);

	return 0;
}
