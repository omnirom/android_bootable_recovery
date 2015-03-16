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
#include <pixelflinger/pixelflinger.h>
}

#include "rapidxml.hpp"
#include "objects.hpp"
#include "../data.hpp"
#include "../variables.h"
#include "../partitions.hpp"
#include "../twrp-functions.hpp"
#include "../openrecoveryscript.hpp"
#include "../orscmd/orscmd.h"
#include "blanktimer.hpp"
#include "../tw_atomic.hpp"

// Enable to print render time of each frame to the log file
//#define PRINT_RENDER_TIME 1

#ifdef _EVENT_LOGGING
#define LOGEVENT(...) LOGERR(__VA_ARGS__)
#else
#define LOGEVENT(...) do {} while (0)
#endif

const static int CURTAIN_FADE = 32;

using namespace rapidxml;

// Global values
static gr_surface gCurtain = NULL;
static int gGuiInitialized = 0;
static TWAtomicInt gGuiConsoleRunning;
static TWAtomicInt gGuiConsoleTerminate;
static TWAtomicInt gForceRender;
const int gNoAnimation = 1;
blanktimer blankTimer;
int ors_read_fd = -1;
static float scale_theme_w = 1;
static float scale_theme_h = 1;

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
	gr_blit(gCurtain, 0, 0, gr_get_width(gCurtain), gr_get_height(gCurtain), TW_X_OFFSET, TW_Y_OFFSET);
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

class InputHandler
{
public:
	void init()
	{
		// these might be read from DataManager in the future
		touch_hold_ms = 500;
		touch_repeat_ms = 100;
		key_hold_ms = 500;
		key_repeat_ms = 100;
		touch_status = TS_NONE;
		key_status = KS_NONE;
		state = AS_NO_ACTION;
		x = y = 0;

#ifndef TW_NO_SCREEN_TIMEOUT
		{
			string seconds;
			DataManager::GetValue("tw_screen_timeout_secs", seconds);
			blankTimer.setTime(atoi(seconds.c_str()));
			blankTimer.resetTimerAndUnblank();
		}
#else
		LOGINFO("Skipping screen timeout: TW_NO_SCREEN_TIMEOUT is set\n");
#endif
	}

	// process input events. returns true if any event was received.
	bool processInput(int timeout_ms);

	void handleDrag();

private:
	// timeouts for touch/key hold and repeat
	int touch_hold_ms;
	int touch_repeat_ms;
	int key_hold_ms;
	int key_repeat_ms;

	enum touch_status_enum {
		TS_NONE = 0,
		TS_TOUCH_AND_HOLD = 1,
		TS_TOUCH_REPEAT = 2,
	};

	enum key_status_enum {
		KS_NONE = 0,
		KS_KEY_PRESSED = 1,
		KS_KEY_REPEAT = 2,
	};

	enum action_state_enum {
		AS_IN_ACTION_AREA = 0, // we've touched a spot with an action
		AS_NO_ACTION = 1,    // we've touched in an empty area (no action) and ignore remaining events until touch release
	};
	touch_status_enum touch_status;
	key_status_enum key_status;
	action_state_enum state;
	int x, y; // x and y coordinates of last touch
	struct timeval touchStart; // used to track time for long press / key repeat

	void processHoldAndRepeat();
	void process_EV_REL(input_event& ev);
	void process_EV_ABS(input_event& ev);
	void process_EV_KEY(input_event& ev);

	void doTouchStart();
};

InputHandler input_handler;


bool InputHandler::processInput(int timeout_ms)
{
	input_event ev;
	int ret = ev_get(&ev, timeout_ms);

	if (ret < 0)
	{
		// This path means that we did not get any new touch data, but
		// we do not get new touch data if you press and hold on either
		// the screen or on a keyboard key or mouse button
		if (touch_status || key_status)
			processHoldAndRepeat();
		return (ret != -2);  // -2 means no more events in the queue
	}

	switch (ev.type)
	{
	case EV_ABS:
		process_EV_ABS(ev);
		break;

	case EV_REL:
		process_EV_REL(ev);
		break;

	case EV_KEY:
		process_EV_KEY(ev);
		break;
	}

	blankTimer.resetTimerAndUnblank();
	return true;  // we got an event, so there might be more in the queue
}

void InputHandler::processHoldAndRepeat()
{
	HardwareKeyboard *kb = PageManager::GetHardwareKeyboard();

	// touch and key repeat section
	struct timeval curTime;
	gettimeofday(&curTime, NULL);
	long seconds = curTime.tv_sec - touchStart.tv_sec;
	long useconds = curTime.tv_usec - touchStart.tv_usec;
	long mtime = ((seconds) * 1000 + useconds / 1000.0) + 0.5;

	if (touch_status == TS_TOUCH_AND_HOLD && mtime > touch_hold_ms)
	{
		touch_status = TS_TOUCH_REPEAT;
		gettimeofday(&touchStart, NULL);
		LOGEVENT("TOUCH_HOLD: %d,%d\n", x, y);
		PageManager::NotifyTouch(TOUCH_HOLD, x, y);
	}
	else if (touch_status == TS_TOUCH_REPEAT && mtime > touch_repeat_ms)
	{
		LOGEVENT("TOUCH_REPEAT: %d,%d\n", x, y);
		gettimeofday(&touchStart, NULL);
		PageManager::NotifyTouch(TOUCH_REPEAT, x, y);
	}
	else if (key_status == KS_KEY_PRESSED && mtime > key_hold_ms)
	{
		LOGEVENT("KEY_HOLD: %d,%d\n", x, y);
		gettimeofday(&touchStart, NULL);
		key_status = KS_KEY_REPEAT;
		kb->KeyRepeat();
	}
	else if (key_status == KS_KEY_REPEAT && mtime > key_repeat_ms)
	{
		LOGEVENT("KEY_REPEAT: %d,%d\n", x, y);
		gettimeofday(&touchStart, NULL);
		kb->KeyRepeat();
	}
}

void InputHandler::doTouchStart()
{
	LOGEVENT("TOUCH_START: %d,%d\n", x, y);
	if (PageManager::NotifyTouch(TOUCH_START, x, y) > 0)
		state = AS_NO_ACTION;
	else
		state = AS_IN_ACTION_AREA;
	touch_status = TS_TOUCH_AND_HOLD;
	gettimeofday(&touchStart, NULL);
}

void InputHandler::process_EV_ABS(input_event& ev)
{
	x = ev.value >> 16;
	y = ev.value & 0xFFFF;

	if (ev.code == 0)
	{
#ifndef TW_USE_KEY_CODE_TOUCH_SYNC
		if (state == AS_IN_ACTION_AREA)
		{
			LOGEVENT("TOUCH_RELEASE: %d,%d\n", x, y);
			PageManager::NotifyTouch(TOUCH_RELEASE, x, y);
		}
		touch_status = TS_NONE;
#endif
	}
	else
	{
		if (!touch_status)
		{
#ifndef TW_USE_KEY_CODE_TOUCH_SYNC
			doTouchStart();
#endif
		}
		else
		{
			if (state == AS_IN_ACTION_AREA)
			{
				LOGEVENT("TOUCH_DRAG: %d,%d\n", x, y);
			}
		}
	}
}

void InputHandler::process_EV_KEY(input_event& ev)
{
	HardwareKeyboard *kb = PageManager::GetHardwareKeyboard();

	// Handle key-press here
	LOGEVENT("TOUCH_KEY: %d\n", ev.code);
	// Left mouse button is treated as a touch
	if(ev.code == BTN_LEFT)
	{
		MouseCursor *cursor = PageManager::GetMouseCursor();
		if(ev.value == 1)
		{
			cursor->GetPos(x, y);
			doTouchStart();
		}
		else if(touch_status)
		{
			// Left mouse button was previously pressed and now is
			// being released so send a TOUCH_RELEASE
			if (state == AS_IN_ACTION_AREA)
			{
				cursor->GetPos(x, y);

				LOGEVENT("Mouse TOUCH_RELEASE: %d,%d\n", x, y);
				PageManager::NotifyTouch(TOUCH_RELEASE, x, y);
			}
			touch_status = TS_NONE;
		}
	}
	// side mouse button, often used for "back" function
	else if(ev.code == BTN_SIDE)
	{
		if(ev.value == 1)
			kb->KeyDown(KEY_BACK);
		else
			kb->KeyUp(KEY_BACK);
	} else if (ev.value != 0) {
		// This is a key press
#ifdef TW_USE_KEY_CODE_TOUCH_SYNC
		if (ev.code == TW_USE_KEY_CODE_TOUCH_SYNC) {
			LOGEVENT("key code %i key press == touch start %i %i\n", TW_USE_KEY_CODE_TOUCH_SYNC, x, y);
			doTouchStart();
			return;
		}
#endif
		if (kb->KeyDown(ev.code)) {
			// Key repeat is enabled for this key
			key_status = KS_KEY_PRESSED;
			touch_status = TS_NONE;
			gettimeofday(&touchStart, NULL);
		} else {
			key_status = KS_NONE;
			touch_status = TS_NONE;
		}
	} else {
		// This is a key release
		kb->KeyUp(ev.code);
		key_status = KS_NONE;
		touch_status = TS_NONE;
#ifdef TW_USE_KEY_CODE_TOUCH_SYNC
		if (ev.code == TW_USE_KEY_CODE_TOUCH_SYNC) {
			LOGEVENT("key code %i key release == touch release %i %i\n", TW_USE_KEY_CODE_TOUCH_SYNC, x, y);
			PageManager::NotifyTouch(TOUCH_RELEASE, x, y);
		}
#endif
	}
}

void InputHandler::process_EV_REL(input_event& ev)
{
	// Mouse movement
	MouseCursor *cursor = PageManager::GetMouseCursor();
	LOGEVENT("EV_REL %d %d\n", ev.code, ev.value);
	if(ev.code == REL_X)
		cursor->Move(ev.value, 0);
	else if(ev.code == REL_Y)
		cursor->Move(0, ev.value);

	if(touch_status) {
		cursor->GetPos(x, y);
		LOGEVENT("Mouse TOUCH_DRAG: %d, %d\n", x, y);
		key_status = KS_NONE;
	}
}

void InputHandler::handleDrag()
{
	// This allows us to only send one NotifyTouch event per render
	// cycle to reduce overhead and perceived input latency.
	static int prevx = 0, prevy = 0; // these track where the last drag notice was so that we don't send duplicate drag notices
	if (touch_status && (x != prevx || y != prevy)) {
		prevx = x;
		prevy = y;
		if (PageManager::NotifyTouch(TOUCH_DRAG, x, y) > 0)
			state = AS_NO_ACTION;
		else
			state = AS_IN_ACTION_AREA;
	}
}

static void setup_ors_command()
{
	ors_read_fd = -1;

	unlink(ORS_INPUT_FILE);
	if (mkfifo(ORS_INPUT_FILE, 06660) != 0) {
		LOGINFO("Unable to mkfifo %s\n", ORS_INPUT_FILE);
		return;
	}
	unlink(ORS_OUTPUT_FILE);
	if (mkfifo(ORS_OUTPUT_FILE, 06666) != 0) {
		LOGINFO("Unable to mkfifo %s\n", ORS_OUTPUT_FILE);
		unlink(ORS_INPUT_FILE);
		return;
	}

	ors_read_fd = open(ORS_INPUT_FILE, O_RDONLY | O_NONBLOCK);
	if (ors_read_fd < 0) {
		LOGINFO("Unable to open %s\n", ORS_INPUT_FILE);
		unlink(ORS_INPUT_FILE);
		unlink(ORS_OUTPUT_FILE);
	}
}

static void ors_command_read()
{
	FILE* orsout;
	char command[1024], result[512];
	int set_page_done = 0, read_ret = 0;

	if ((read_ret = read(ors_read_fd, &command, sizeof(command))) > 0) {
		command[1022] = '\n';
		command[1023] = '\0';
		LOGINFO("Command '%s' received\n", command);
		orsout = fopen(ORS_OUTPUT_FILE, "w");
		if (!orsout) {
			close(ors_read_fd);
			ors_read_fd = -1;
			LOGINFO("Unable to fopen %s\n", ORS_OUTPUT_FILE);
			unlink(ORS_INPUT_FILE);
			unlink(ORS_OUTPUT_FILE);
			return;
		}
		if (DataManager::GetIntValue("tw_busy") != 0) {
			strcpy(result, "Failed, operation in progress\n");
			fprintf(orsout, "%s", result);
			LOGINFO("Command cannot be performed, operation in progress.\n");
		} else {
			if (gui_console_only() == 0) {
				LOGINFO("Console started successfully\n");
				gui_set_FILE(orsout);
				if (strlen(command) > 11 && strncmp(command, "runscript", 9) == 0) {
					char* filename = command + 11;
					if (OpenRecoveryScript::copy_script_file(filename) == 0) {
						LOGERR("Unable to copy script file\n");
					} else {
						OpenRecoveryScript::run_script_file();
					}
				} else if (strlen(command) > 5 && strncmp(command, "get", 3) == 0) {
					char* varname = command + 4;
					string temp;
					DataManager::GetValue(varname, temp);
					gui_print("%s = %s\n", varname, temp.c_str());
				} else if (strlen(command) > 9 && strncmp(command, "decrypt", 7) == 0) {
					char* pass = command + 8;
					gui_print("Attempting to decrypt data partition via command line.\n");
					if (PartitionManager.Decrypt_Device(pass) == 0) {
						set_page_done = 1;
					}
				} else if (OpenRecoveryScript::Insert_ORS_Command(command)) {
					OpenRecoveryScript::run_script_file();
				}
				gui_set_FILE(NULL);
				gGuiConsoleTerminate.set_value(1);
			}
		}
		fclose(orsout);
		LOGINFO("Done reading ORS command from command line\n");
		if (set_page_done) {
			DataManager::SetValue("tw_page_done", 1);
		} else {
			// The select function will return ready to read and the
			// read function will return errno 19 no such device unless
			// we set everything up all over again.
			close(ors_read_fd);
			setup_ors_command();
		}
	} else {
		LOGINFO("ORS command line read returned an error: %i, %i, %s\n", read_ret, errno, strerror(errno));
	}
	return;
}

// This special function will return immediately the first time, but then
// always returns 1/30th of a second (or immediately if called later) from
// the last time it was called
static void loopTimer(int input_timeout_ms)
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
		bool got_event = input_handler.processInput(input_timeout_ms); // get inputs but don't send drag notices
		timespec curTime;
		clock_gettime(CLOCK_MONOTONIC, &curTime);

		timespec diff = TWFunc::timespec_diff(lastCall, curTime);

		// This is really 2 or 30 times per second
		// As long as we get events, increase the timeout so we can catch up with input
		long timeout = got_event ? 500000000 : 33333333;

		if (diff.tv_sec || diff.tv_nsec > timeout)
		{
			// int32_t input_time = TWFunc::timespec_diff_ms(lastCall, curTime);
			// LOGINFO("loopTimer(): %u ms, count: %u\n", input_time, count);

			lastCall = curTime;
			input_handler.handleDrag(); // send only drag notices if needed
			return;
		}

		// We need to sleep some period time microseconds
		//unsigned int sleepTime = 33333 -(diff.tv_nsec / 1000);
		//usleep(sleepTime); // removed so we can scan for input
		input_timeout_ms = 0;
	} while (1);
}

static int runPages(const char *page_name, const int stop_on_page_done)
{
	DataManager::SetValue("tw_page_done", 0);
	DataManager::SetValue("tw_gui_done", 0);

	if (page_name)
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

#ifndef TW_OEM_BUILD
	struct timeval timeout;
	fd_set fdset;
	int has_data = 0;
#endif

	int input_timeout_ms = 0;
	int idle_frames = 0;

	for (;;)
	{
		loopTimer(input_timeout_ms);
#ifndef TW_OEM_BUILD
		if (ors_read_fd > 0) {
			FD_ZERO(&fdset);
			FD_SET(ors_read_fd, &fdset);
			timeout.tv_sec = 0;
			timeout.tv_usec = 1;
			has_data = select(ors_read_fd+1, &fdset, NULL, NULL, &timeout);
			if (has_data > 0) {
				ors_command_read();
			}
		}
#endif

		if (gGuiConsoleRunning.get_value()) {
			continue;
		}

		if (!gForceRender.get_value())
		{
			int ret = PageManager::Update();
			if (ret == 0)
				++idle_frames;
			else
				idle_frames = 0;
			// due to possible animation objects, we need to delay activating the input timeout
			input_timeout_ms = idle_frames > 15 ? 1000 : 0;

#ifndef PRINT_RENDER_TIME
			if (ret > 1)
				PageManager::Render();

			if (ret > 0)
				flip();
#else
			if (ret > 1)
			{
				timespec start, end;
				int32_t render_t, flip_t;
				clock_gettime(CLOCK_MONOTONIC, &start);
				PageManager::Render();
				clock_gettime(CLOCK_MONOTONIC, &end);
				render_t = TWFunc::timespec_diff_ms(start, end);

				flip();
				clock_gettime(CLOCK_MONOTONIC, &start);
				flip_t = TWFunc::timespec_diff_ms(end, start);

				LOGINFO("Render(): %u ms, flip(): %u ms, total: %u ms\n", render_t, flip_t, render_t+flip_t);
			}
			else if (ret > 0)
				flip();
#endif
		}
		else
		{
			gForceRender.set_value(0);
			PageManager::Render();
			flip();
			input_timeout_ms = 0;
		}

		blankTimer.checkForTimeout();
		if (stop_on_page_done && DataManager::GetIntValue("tw_page_done") != 0)
		{
			gui_changePage("main");
			break;
		}
		if (DataManager::GetIntValue("tw_gui_done") != 0)
			break;
	}
	if (ors_read_fd > 0)
		close(ors_read_fd);
	ors_read_fd = -1;
	gGuiRunning = 0;
	return 0;
}

int gui_forceRender(void)
{
	gForceRender.set_value(1);
	return 0;
}

int gui_changePage(std::string newPage)
{
	LOGINFO("Set page: '%s'\n", newPage.c_str());
	PageManager::ChangePage(newPage);
	gForceRender.set_value(1);
	return 0;
}

int gui_changeOverlay(std::string overlay)
{
	LOGINFO("Set overlay: '%s'\n", overlay.c_str());
	PageManager::ChangeOverlay(overlay);
	gForceRender.set_value(1);
	return 0;
}

int gui_changePackage(std::string newPackage)
{
	PageManager::SelectPackage(newPackage);
	gForceRender.set_value(1);
	return 0;
}

std::string gui_parse_text(std::string str)
{
	// This function parses text for DataManager values encompassed by %value% in the XML
	// and string resources (%@resource_name%)
	size_t pos = 0;

	while (1)
	{
		size_t next = str.find('%', pos);
		if (next == std::string::npos)
			return str;

		size_t end = str.find('%', next + 1);
		if (end == std::string::npos)
			return str;

		// We have a block of data
		std::string var = str.substr(next + 1, (end - next) - 1);
		str.erase(next, (end - next) + 1);

		if (next + 1 == end)
			str.insert(next, 1, '%');
		else
		{
			std::string value;
			if (var.size() > 0 && var[0] == '@') {
				// this is a string resource ("%@string_name%")
				value = PageManager::GetResources()->FindString(var.substr(1));
				str.insert(next, value);
			}
			else if (DataManager::GetValue(var, value) == 0)
				str.insert(next, value);
		}

		pos = next + 1;
	}
}

extern "C" int gui_init(void)
{
	gr_init();
	std::string curtain_path = TWRES "images/curtain.jpg";
	gr_surface source_Surface = NULL;

	if (res_create_surface(curtain_path.c_str(), &source_Surface))
	{
		printf("Unable to locate '%s'\nDid you set a TW_THEME in your config files?\n", curtain_path.c_str());
		return -1;
	}
	if (gr_get_width(source_Surface) != gr_fb_width() || gr_get_height(source_Surface) != gr_fb_height()) {
		// We need to scale the curtain to fit the screen
		float scale_w = (float)gr_fb_width() / (float)gr_get_width(source_Surface);
		float scale_h = (float)gr_fb_height() / (float)gr_get_height(source_Surface);
		if (res_scale_surface(source_Surface, &gCurtain, scale_w, scale_h)) {
			LOGINFO("Failed to scale curtain\n");
			gCurtain = source_Surface;
		} else {
			LOGINFO("Scaling the curtain width %fx and height %fx\n", scale_w, scale_h);
		}
	} else {
		gCurtain = source_Surface;
	}

	curtainSet();

	ev_init();
	return 0;
}

extern "C" int gui_loadResources(void)
{
#ifndef TW_OEM_BUILD
	int check = 0;
	DataManager::GetValue(TW_IS_ENCRYPTED, check);

	if (check)
	{
		if (PageManager::LoadPackage("TWRP", TWRES "ui.xml", "decrypt"))
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
#endif // ifndef TW_OEM_BUILD
			if (PageManager::LoadPackage("TWRP", TWRES "ui.xml", "main"))
			{
				LOGERR("Failed to load base packages.\n");
				goto error;
			}
#ifndef TW_OEM_BUILD
		}
	}
#endif // ifndef TW_OEM_BUILD
	// Set the default package
	PageManager::SelectPackage("TWRP");

	gGuiInitialized = 1;
	return 0;

error:
	LOGERR("An internal error has occurred: unable to load theme.\n");
	gGuiInitialized = 0;
	return -1;
}

extern "C" int gui_loadCustomResources(void)
{
#ifndef TW_OEM_BUILD
	if (!PartitionManager.Mount_Settings_Storage(false)) {
		LOGERR("Unable to mount settings storage during GUI startup.\n");
		return -1;
	}

	std::string theme_path = DataManager::GetSettingsStoragePath();
	theme_path += "/TWRP/theme/ui.zip";
	// Check for a custom theme
	if (TWFunc::Path_Exists(theme_path)) {
		// There is a custom theme, try to load it
		if (PageManager::ReloadPackage("TWRP", theme_path)) {
			// Custom theme failed to load, try to load stock theme
			if (PageManager::ReloadPackage("TWRP", TWRES "ui.xml")) {
				LOGERR("Failed to load base packages.\n");
				goto error;
			}
		}
	}
	// Set the default package
	PageManager::SelectPackage("TWRP");
#endif
	return 0;

error:
	LOGERR("An internal error has occurred: unable to load theme.\n");
	gGuiInitialized = 0;
	return -1;
}

extern "C" int gui_start(void)
{
	return gui_startPage(NULL, 1, 0);
}

extern "C" int gui_startPage(const char *page_name, const int allow_commands, int stop_on_page_done)
{
	if (!gGuiInitialized)
		return -1;

	gGuiConsoleTerminate.set_value(1);

	while (gGuiConsoleRunning.get_value())
		usleep(10000);

	// Set the default package
	PageManager::SelectPackage("TWRP");

	input_handler.init();
#ifndef TW_OEM_BUILD
	if (allow_commands)
	{
		if (ors_read_fd < 0)
			setup_ors_command();
	} else {
		if (ors_read_fd >= 0) {
			close(ors_read_fd);
			ors_read_fd = -1;
		}
	}
#endif
	return runPages(page_name, stop_on_page_done);
}

static void * console_thread(void *cookie)
{
	PageManager::SwitchToConsole();

	while (!gGuiConsoleTerminate.get_value())
	{
		loopTimer(0);

		if (!gForceRender.get_value())
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
			gForceRender.set_value(0);
			PageManager::Render();
			flip();
		}
	}
	gGuiConsoleRunning.set_value(0);
	gForceRender.set_value(1); // this will kickstart the GUI to render again
	PageManager::EndConsole();
	LOGINFO("Console stopping\n");
	return NULL;
}

extern "C" int gui_console_only(void)
{
	if (!gGuiInitialized)
		return -1;

	gGuiConsoleTerminate.set_value(0);

	if (gGuiConsoleRunning.get_value())
		return 0;

	gGuiConsoleRunning.set_value(1);

	// Start by spinning off an input handler.
	pthread_t t;
	pthread_create(&t, NULL, console_thread, NULL);

	return 0;
}

extern "C" void set_scale_values(float w, float h)
{
	scale_theme_w = w;
	scale_theme_h = h;
}

extern "C" int scale_theme_x(int initial_x)
{
	if (scale_theme_w != 1) {
		int scaled = (float)initial_x * scale_theme_w;
		if (scaled == 0 && initial_x > 0)
			return 1;
		return scaled;
	}
	return initial_x;
}

extern "C" int scale_theme_y(int initial_y)
{
	if (scale_theme_h != 1) {
		int scaled = (float)initial_y * scale_theme_h;
		if (scaled == 0 && initial_y > 0)
			return 1;
		return scaled;
	}
	return initial_y;
}

extern "C" int scale_theme_min(int initial_value)
{
	if (scale_theme_w != 1 || scale_theme_h != 1) {
		if (scale_theme_w < scale_theme_h)
			return scale_theme_x(initial_value);
		else
			return scale_theme_y(initial_value);
	}
	return initial_value;
}

extern "C" float get_scale_w()
{
	return scale_theme_w;
}

extern "C" float get_scale_h()
{
	return scale_theme_h;
}
