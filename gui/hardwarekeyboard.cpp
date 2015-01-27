// hardwarekeyboard.cpp - HardwareKeyboard object
// Custom hardware keyboard support for Asus Transformer devices

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>

#include <string>

extern "C" {
#include "../common.h"
#include "../minuitwrp/minui.h"
#include "../recovery_ui.h"
}

#include "rapidxml.hpp"
#include "objects.hpp"
#include "../data.hpp"
#include <linux/input.h>

HardwareKeyboard::HardwareKeyboard(void) {
	// Do Nothing
	DataManager::SetValue("Lshift_down", 0);
	DataManager::SetValue("Rshift_down", 0);
	DataManager::SetValue("last_key", 0);
}

HardwareKeyboard::~HardwareKeyboard() {
	// Do Nothing
}

int HardwareKeyboard::KeyDown(int key_code) {

	int keyboard = -1;
	int shiftkey, Lshift_down, Rshift_down;

	DataManager::GetValue("Lshift_down", Lshift_down);
	DataManager::GetValue("Rshift_down", Rshift_down);
	if (Lshift_down || Rshift_down)
		shiftkey = 1;
	else
		shiftkey = 0;

#ifdef _EVENT_LOGGING
	LOGE("HardwareKeyboard::KeyDown %i\n", key_code);
#endif
	switch (key_code) {
		case KEY_LEFTSHIFT: // Left Shift
			DataManager::SetValue("Lshift_down", 1);
			return 0;
			break;
		case KEY_RIGHTSHIFT: // Right Shift
			DataManager::SetValue("Rshift_down", 1);
			return 0;
			break;
		case KEY_A:
			if (shiftkey)
				keyboard = 'A';
			else
				keyboard = 'a';
			break;
		case KEY_B:
			if (shiftkey)
				keyboard = 'B';
			else
				keyboard = 'b';
			break;
		case KEY_C:
			if (shiftkey)
				keyboard = 'C';
			else
				keyboard = 'c';
			break;
		case KEY_D:
			if (shiftkey)
				keyboard = 'D';
			else
				keyboard = 'd';
			break;
		case KEY_E:
			if (shiftkey)
				keyboard = 'E';
			else
				keyboard = 'e';
			break;
		case KEY_F:
			if (shiftkey)
				keyboard = 'F';
			else
				keyboard = 'f';
			break;
		case KEY_G:
			if (shiftkey)
				keyboard = 'G';
			else
				keyboard = 'g';
			break;
		case KEY_H:
			if (shiftkey)
				keyboard = 'H';
			else
				keyboard = 'h';
			break;
		case KEY_I:
			if (shiftkey)
				keyboard = 'I';
			else
				keyboard = 'i';
			break;
		case KEY_J:
			if (shiftkey)
				keyboard = 'J';
			else
				keyboard = 'j';
			break;
		case KEY_K:
			if (shiftkey)
				keyboard = 'K';
			else
				keyboard = 'k';
			break;
		case KEY_L:
			if (shiftkey)
				keyboard = 'L';
			else
				keyboard = 'l';
			break;
		case KEY_M:
			if (shiftkey)
				keyboard = 'M';
			else
				keyboard = 'm';
			break;
		case KEY_N:
			if (shiftkey)
				keyboard = 'N';
			else
				keyboard = 'n';
			break;
		case KEY_O:
			if (shiftkey)
				keyboard = 'O';
			else
				keyboard = 'o';
			break;
		case KEY_P:
			if (shiftkey)
				keyboard = 'P';
			else
				keyboard = 'p';
			break;
		case KEY_Q:
			if (shiftkey)
				keyboard = 'Q';
			else
				keyboard = 'q';
			break;
		case KEY_R:
			if (shiftkey)
				keyboard = 'R';
			else
				keyboard = 'r';
			break;
		case KEY_S:
			if (shiftkey)
				keyboard = 'S';
			else
				keyboard = 's';
			break;
		case KEY_T:
			if (shiftkey)
				keyboard = 'T';
			else
				keyboard = 't';
			break;
		case KEY_U:
			if (shiftkey)
				keyboard = 'U';
			else
				keyboard = 'u';
			break;
		case KEY_V:
			if (shiftkey)
				keyboard = 'V';
			else
				keyboard = 'v';
			break;
		case KEY_W:
			if (shiftkey)
				keyboard = 'W';
			else
				keyboard = 'w';
			break;
		case KEY_X:
			if (shiftkey)
				keyboard = 'X';
			else
				keyboard = 'x';
			break;
		case KEY_Y:
			if (shiftkey)
				keyboard = 'Y';
			else
				keyboard = 'y';
			break;
		case KEY_Z:
			if (shiftkey)
				keyboard = 'Z';
			else
				keyboard = 'z';
			break;
		case KEY_0:
			if (shiftkey)
				keyboard = ')';
			else
				keyboard = '0';
			break;
		case KEY_1:
			if (shiftkey)
				keyboard = '!';
			else
				keyboard = '1';
			break;
		case KEY_2:
			if (shiftkey)
				keyboard = '@';
			else
				keyboard = '2';
			break;
		case KEY_3:
			if (shiftkey)
				keyboard = '#';
			else
				keyboard = '3';
			break;
		case KEY_4:
			if (shiftkey)
				keyboard = '$';
			else
				keyboard = '4';
			break;
		case KEY_5:
			if (shiftkey)
				keyboard = '%';
			else
				keyboard = '5';
			break;
		case KEY_6:
			if (shiftkey)
				keyboard = '^';
			else
				keyboard = '6';
			break;
		case KEY_7:
			if (shiftkey)
				keyboard = '&';
			else
				keyboard = '7';
			break;
		case KEY_8:
			if (shiftkey)
				keyboard = '*';
			else
				keyboard = '8';
			break;
		case KEY_9:
			if (shiftkey)
				keyboard = '(';
			else
				keyboard = '9';
			break;
		case KEY_SPACE:
			keyboard = ' ';
			break;
		case KEY_BACKSPACE:
			keyboard = KEYBOARD_BACKSPACE;
			break;
		case KEY_ENTER:
			keyboard = KEYBOARD_ACTION;
			break;
		case KEY_SLASH:
			if (shiftkey)
				keyboard = '?';
			else
				keyboard = '/';
			break;
		case KEY_DOT:
			if (shiftkey)
				keyboard = '>';
			else
				keyboard = '.';
			break;
		case KEY_COMMA:
			if (shiftkey)
				keyboard = '<';
			else
				keyboard = ',';
			break;
		case KEY_MINUS:
			if (shiftkey)
				keyboard = '_';
			else
				keyboard = '-';
			break;
		case KEY_GRAVE:
			if (shiftkey)
				keyboard = '~';
			else
				keyboard = '`';
			break;
		case KEY_EQUAL:
			if (shiftkey)
				keyboard = '+';
			else
				keyboard = '=';
			break;
		case KEY_LEFTBRACE:
			if (shiftkey)
				keyboard = '{';
			else
				keyboard = '[';
			break;
		case KEY_RIGHTBRACE:
			if (shiftkey)
				keyboard = '}';
			else
				keyboard = ']';
			break;
		case KEY_BACKSLASH:
			if (shiftkey)
				keyboard = '|';
			else
				keyboard = '\\';
			break;
		case KEY_SEMICOLON:
			if (shiftkey)
				keyboard = ':';
			else
				keyboard = ';';
			break;
		case KEY_APOSTROPHE:
			if (shiftkey)
				keyboard = '\"';
			else
				keyboard = '\'';
			break;
		case KEY_UP: // Up arrow
			keyboard = KEYBOARD_ARROW_UP;
			break;
		case KEY_DOWN: // Down arrow
			keyboard = KEYBOARD_ARROW_DOWN;
			break;
		case KEY_LEFT: // Left arrow
			keyboard = KEYBOARD_ARROW_LEFT;
			break;
		case KEY_RIGHT: // Right arrow
			keyboard = KEYBOARD_ARROW_RIGHT;
			break;
		case KEY_BACK: // back button on screen
			mPressedKeys.insert(KEY_BACK);
			PageManager::NotifyKey(KEY_BACK, true);
			return 0;
			break;
		case KEY_HOMEPAGE: // keyboard home button
		case KEY_HOME: // home button on screen
			mPressedKeys.insert(KEY_HOME);
			PageManager::NotifyKey(KEY_HOME, true);
			return 0;
			break;
		case KEY_SLEEP: // keyboard lock button
		case KEY_POWER: // tablet power button
			mPressedKeys.insert(KEY_POWER);
			PageManager::NotifyKey(KEY_POWER, true);
			return 0;
			break;
#ifdef _EVENT_LOGGING
		default:
			LOGE("Unmapped keycode: %i\n", key_code);
			break;
#endif
	}
	if (keyboard != -1) {
		DataManager::SetValue("last_key", keyboard);
		mPressedKeys.insert(keyboard);
		if (!PageManager::NotifyKeyboard(keyboard))
			return 1;  // Return 1 to enable key repeat
	} else {
		DataManager::SetValue("last_key", 0);
	}
	return 0;
}

int HardwareKeyboard::KeyUp(int key_code) {
	std::set<int>::iterator itr = mPressedKeys.find(key_code);
	if (itr != mPressedKeys.end()) {
		mPressedKeys.erase(itr);
		PageManager::NotifyKey(key_code, false);
	}
#ifdef _EVENT_LOGGING
	LOGE("HardwareKeyboard::KeyUp %i\n", key_code);
#endif
	if (key_code == KEY_LEFTSHIFT) { // Left Shift
		DataManager::SetValue("Lshift_down", 0);
	} else if (key_code == 31) { // Right Shift
		DataManager::SetValue("Rshift_down", 0);
	}
	return 0;
}

int HardwareKeyboard::KeyRepeat(void) {
	int last_key;

	DataManager::GetValue("last_key", last_key);
#ifdef _EVENT_LOGGING
	LOGE("HardwareKeyboard::KeyRepeat: %i\n", last_key);
#endif
	if (last_key)
		PageManager::NotifyKeyboard(last_key);
	return 0;
}

void HardwareKeyboard::ConsumeKeyRelease(int key) {
	mPressedKeys.erase(key);
}
