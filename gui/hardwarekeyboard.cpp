// hardwarekeyboard.cpp - HardwareKeyboard object

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>

#include <string>

extern "C" {
#include "../common.h"
}

#include "objects.hpp"
#include <linux/input.h>

HardwareKeyboard::HardwareKeyboard()
 : mLastKeyChar(0)
{
}

HardwareKeyboard::~HardwareKeyboard()
{
}

// Map keys to other keys.
static int TranslateKeyCode(int key_code)
{
	switch (key_code) {
		case KEY_SLEEP: // Lock key on Asus Transformer hardware keyboard
			return KEY_POWER;
	}
	return key_code;
}

static int KeyCodeToChar(int key_code, bool shiftkey, bool ctrlkey)
{
	int keyboard = -1;

	switch (key_code) {
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
		case KEY_TAB:
			keyboard = KEYBOARD_TAB;
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

#ifdef _EVENT_LOGGING
		default:
			LOGE("Unmapped keycode: %i\n", key_code);
			break;
#endif
	}
	if (ctrlkey)
	{
		if (keyboard >= 96)
			keyboard -= 96;
		else
			keyboard = -1;
	}
	return keyboard;
}

bool HardwareKeyboard::IsKeyDown(int key_code)
{
	std::set<int>::iterator it = mPressedKeys.find(key_code);
	return (it != mPressedKeys.end());
}

int HardwareKeyboard::KeyDown(int key_code)
{
#ifdef _EVENT_LOGGING
	LOGE("HardwareKeyboard::KeyDown %i\n", key_code);
#endif
	key_code = TranslateKeyCode(key_code);
	mPressedKeys.insert(key_code);

	bool ctrlkey = IsKeyDown(KEY_LEFTCTRL) || IsKeyDown(KEY_RIGHTCTRL);
	bool shiftkey = IsKeyDown(KEY_LEFTSHIFT) || IsKeyDown(KEY_RIGHTSHIFT);

	int ch = KeyCodeToChar(key_code, shiftkey, ctrlkey);

	if (ch != -1) {
		mLastKeyChar = ch;
		if (!PageManager::NotifyCharInput(ch))
			return 1;  // Return 1 to enable key repeat
	} else {
		mLastKeyChar = 0;
		mLastKey = key_code;
		if (!PageManager::NotifyKey(key_code, true))
			return 1;  // Return 1 to enable key repeat
	}
	return 0;
}

int HardwareKeyboard::KeyUp(int key_code)
{
#ifdef _EVENT_LOGGING
	LOGE("HardwareKeyboard::KeyUp %i\n", key_code);
#endif
	key_code = TranslateKeyCode(key_code);
	std::set<int>::iterator itr = mPressedKeys.find(key_code);
	if (itr != mPressedKeys.end()) {
		mPressedKeys.erase(itr);
		PageManager::NotifyKey(key_code, false);
	}
	return 0;
}

int HardwareKeyboard::KeyRepeat()
{
#ifdef _EVENT_LOGGING
	LOGE("HardwareKeyboard::KeyRepeat: %i\n", mLastKeyChar);
#endif
	if (mLastKeyChar)
		PageManager::NotifyCharInput(mLastKeyChar);
	else if (mLastKey)
		PageManager::NotifyKey(mLastKey, true);
	return 0;
}

void HardwareKeyboard::ConsumeKeyRelease(int key)
{
	// causes following KeyUp event to suppress notifications
	mPressedKeys.erase(key);
}
