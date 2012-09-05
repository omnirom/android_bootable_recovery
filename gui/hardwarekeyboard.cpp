// hardwarekeyboard.cpp - HardwareKeyboard object
// Shell file used for most devices. A custom hardwarekeyboard.cpp is needed for devices with a hardware keyboard.

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

HardwareKeyboard::HardwareKeyboard(void) {
	// Do Nothing
}

HardwareKeyboard::~HardwareKeyboard() {
	// Do Nothing
}

int HardwareKeyboard::KeyDown(int key_code) {
#ifdef _EVENT_LOGGING
	LOGE("HardwareKeyboard::KeyDown %i\n", key_code);
#endif
	PageManager::NotifyKey(key_code);
	return 0; // 0 = no key repeat anything else turns on key repeat
}

int HardwareKeyboard::KeyUp(int key_code) {
#ifdef _EVENT_LOGGING
	LOGE("HardwareKeyboard::KeyUp %i\n", key_code);
#endif
	return 0;
}

int HardwareKeyboard::KeyRepeat(void) {
#ifdef _EVENT_LOGGING
	LOGE("HardwareKeyboard::KeyRepeat\n");
#endif
	return 0;
}
