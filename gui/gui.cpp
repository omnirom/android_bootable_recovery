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
#include <pixelflinger/pixelflinger.h>
}
#include "../minuitwrp/minui.h"

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

using namespace rapidxml;

// Global values
static int gGuiInitialized = 0;
static TWAtomicInt gForceRender;
blanktimer blankTimer;
int ors_read_fd = -1;
static FILE* orsout = NULL;
static float scale_theme_w = 1;
static float scale_theme_h = 1;

// Needed by pages.cpp too
int gGuiRunning = 0;

int g_pty_fd = -1;  // set by terminal on init
void terminal_pty_read();

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

// callback called after a CLI command was executed
static void ors_command_done()
{
	gui_set_FILE(NULL);
	fclose(orsout);
	orsout = NULL;

	if (DataManager::GetIntValue("tw_page_done") == 0) {
		// The select function will return ready to read and the
		// read function will return errno 19 no such device unless
		// we set everything up all over again.
		close(ors_read_fd);
		setup_ors_command();
	}
}

static void ors_command_read()
{
	char command[1024];
	int read_ret = read(ors_read_fd, &command, sizeof(command));

	if (read_ret > 0) {
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
			fputs("Failed, operation in progress\n", orsout);
			LOGINFO("Command cannot be performed, operation in progress.\n");
			fclose(orsout);
		} else {
			if (strlen(command) == 11 && strncmp(command, "dumpstrings", 11) == 0) {
				gui_set_FILE(orsout);
				PageManager::GetResources()->DumpStrings();
				ors_command_done();
			} else {
				// mirror output messages
				gui_set_FILE(orsout);
				// close orsout and restart listener after command is done
				OpenRecoveryScript::Call_After_CLI_Command(ors_command_done);
				// run the command in a threaded action...
				DataManager::SetValue("tw_action", "twcmd");
				DataManager::SetValue("tw_action_param", command);
				// ...and switch back to the current page when finished
				std::string currentPage = PageManager::GetCurrentPage();
				DataManager::SetValue("tw_has_action2", "1");
				DataManager::SetValue("tw_action2", "page");
				DataManager::SetValue("tw_action2_param", currentPage);
				DataManager::SetValue("tw_action_text1", gui_lookup("running_recovery_commands", "Running Recovery Commands"));
				DataManager::SetValue("tw_action_text2", "");
				gui_changePage("singleaction_page");
				// now immediately return to the GUI main loop (the action runs in the background thread)
				// put all things that need to be done after the command is finished into ors_command_done, not here
			}
		}
	} else {
		LOGINFO("ORS command line read returned an error: %i, %i, %s\n", read_ret, errno, strerror(errno));
	}
}

// Get and dispatch input events until it's time to draw the next frame
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

	if (page_name) {
		PageManager::SetStartPage(page_name);
		gui_changePage(page_name);
	}

	gGuiRunning = 1;

	DataManager::SetValue("tw_loaded", 1);

	struct timeval timeout;
	fd_set fdset;
	int has_data = 0;

	int input_timeout_ms = 0;
	int idle_frames = 0;

	for (;;)
	{
		loopTimer(input_timeout_ms);
		if (g_pty_fd > 0) {
			// TODO: this is not nice, we should have one central select for input, pty, and ors
			FD_ZERO(&fdset);
			FD_SET(g_pty_fd, &fdset);
			timeout.tv_sec = 0;
			timeout.tv_usec = 1;
			has_data = select(g_pty_fd+1, &fdset, NULL, NULL, &timeout);
			if (has_data > 0) {
				terminal_pty_read();
			}
		}
#ifndef TW_OEM_BUILD
		if (ors_read_fd > 0 && !orsout) { // orsout is non-NULL if a command is still running
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

		if (!gForceRender.get_value())
		{
			int ret = PageManager::Update();
			if (ret == 0)
				++idle_frames;
			else if (ret == -2)
				break; // Theme reload failure
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

std::string gui_parse_text(std::string str)
{
	// This function parses text for DataManager values encompassed by %value% in the XML
	// and string resources (%@resource_name%)
	size_t pos = 0, next, end;

	while (1)
	{
		next = str.find("{@", pos);
		if (next == std::string::npos)
			break;

		end = str.find('}', next + 1);
		if (end == std::string::npos)
			break;

		std::string var = str.substr(next + 2, (end - next) - 2);
		str.erase(next, (end - next) + 1);

		size_t default_loc = var.find('=', 0);
		std::string lookup;
		if (default_loc == std::string::npos) {
			str.insert(next, PageManager::GetResources()->FindString(var));
		} else {
			lookup = var.substr(0, default_loc);
			std::string default_string = var.substr(default_loc + 1, var.size() - default_loc - 1);
			str.insert(next, PageManager::GetResources()->FindString(lookup, default_string));
		}
	}
	pos = 0;
	while (1)
	{
		next = str.find('%', pos);
		if (next == std::string::npos)
			return str;

		end = str.find('%', next + 1);
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

std::string gui_lookup(const std::string& resource_name, const std::string& default_value) {
	return PageManager::GetResources()->FindString(resource_name, default_value);
}

extern "C" int gui_init(void)
{
	gr_init();
	TWFunc::Set_Brightness(DataManager::GetStrValue("tw_brightness"));

	// load and show splash screen
	if (PageManager::LoadPackage("splash", TWRES "splash.xml", "splash")) {
		LOGERR("Failed to load splash screen XML.\n");
	}
	else {
		PageManager::SelectPackage("splash");
		PageManager::Render();
		flip();
		PageManager::ReleasePackage("splash");
	}

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
			gui_err("base_pkg_err=Failed to load base packages.");
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

			if (!PartitionManager.Mount_Settings_Storage(true))
			{
				LOGINFO("Unable to mount %s during GUI startup.\n", theme_path.c_str());
				check = 1;
			}
		}

		theme_path += "/TWRP/theme/ui.zip";
		if (check || PageManager::LoadPackage("TWRP", theme_path, "main"))
		{
#endif // ifndef TW_OEM_BUILD
			if (PageManager::LoadPackage("TWRP", TWRES "ui.xml", "main"))
			{
				gui_err("base_pkg_err=Failed to load base packages.");
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
		LOGINFO("Unable to mount settings storage during GUI startup.\n");
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
				gui_err("base_pkg_err=Failed to load base packages.");
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
	return gui_startPage("main", 1, 0);
}

extern "C" int gui_startPage(const char *page_name, const int allow_commands, int stop_on_page_done)
{
	if (!gGuiInitialized)
		return -1;

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
