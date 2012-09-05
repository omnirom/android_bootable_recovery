// keyboard.cpp - GUIKeyboard object

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

GUIKeyboard::GUIKeyboard(xml_node<>* node)
	: Conditional(node)
{
	int layoutindex, rowindex, keyindex, Xindex, Yindex, keyHeight = 0, keyWidth = 0;
	char resource[9], layout[7], row[4], key[5], longpress[6];
	xml_attribute<>* attr;
	xml_node<>* child;
	xml_node<>* keylayout;
	xml_node<>* keyrow;

	for (layoutindex=0; layoutindex<MAX_KEYBOARD_LAYOUTS; layoutindex++)
		keyboardImg[layoutindex] = NULL;

	mRendered = false;
	currentLayout = 1;
	mAction = NULL;
	KeyboardHeight = KeyboardWidth = cursorLocation = 0;

	if (!node)  return;

	// Load the action
	child = node->first_node("action");
	if (child)
	{
		mAction = new GUIAction(node);
	}

	// Load the images for the different layouts
	child = node->first_node("layout");
	if (child)
	{
		layoutindex = 1;
		strcpy(resource, "resource1");
		attr = child->first_attribute(resource);
		while (attr && layoutindex < (MAX_KEYBOARD_LAYOUTS + 1)) {
			keyboardImg[layoutindex - 1] = PageManager::FindResource(attr->value());

			layoutindex++;
			resource[8] = (char)(layoutindex + 48);
			attr = child->first_attribute(resource);
		}
	}

	// Check the first image to get height and width
	if (keyboardImg[0] && keyboardImg[0]->GetResource())
	{
		KeyboardWidth = gr_get_width(keyboardImg[0]->GetResource());
		KeyboardHeight = gr_get_height(keyboardImg[0]->GetResource());
	}

	// Load all of the layout maps
	layoutindex = 1;
	strcpy(layout, "layout1");
	keylayout = node->first_node(layout);
	while (keylayout)
	{
		if (layoutindex > MAX_KEYBOARD_LAYOUTS) {
			LOGE("Too many layouts defined in keyboard.\n");
			return;
		}

		child = keylayout->first_node("keysize");
		if (child) {
			attr = child->first_attribute("height");
			if (attr)
				keyHeight = atoi(attr->value());
			else
				keyHeight = 0;
			attr = child->first_attribute("width");
			if (attr)
				keyWidth = atoi(attr->value());
			else
				keyWidth = 0;
		}

		rowindex = 1;
		Yindex = 0;
		strcpy(row, "row1");
		keyrow = keylayout->first_node(row);
		while (keyrow)
		{
			if (rowindex > MAX_KEYBOARD_ROWS) {
				LOGE("Too many rows defined in keyboard.\n");
				return;
			}

			Yindex += keyHeight;
			row_heights[layoutindex - 1][rowindex - 1] = Yindex;

			keyindex = 1;
			Xindex = 0;
			strcpy(key, "key01");
			attr = keyrow->first_attribute(key);

			while (attr) {
				string stratt;
				char keyinfo[255];

				if (keyindex > MAX_KEYBOARD_KEYS) {
					LOGE("Too many keys defined in a keyboard row.\n");
					return;
				}

				stratt = attr->value();
				if (strlen(stratt.c_str()) >= 255) {
					LOGE("Key info on layout%i, row%i, key%dd is too long.\n", layoutindex, rowindex, keyindex);
					return;
				}

				strcpy(keyinfo, stratt.c_str());

				if (strlen(keyinfo) == 0) {
					LOGE("No key info on layout%i, row%i, key%dd.\n", layoutindex, rowindex, keyindex);
					return;
				}

				if (strlen(keyinfo) == 1) {
					// This is a single key, simple definition
					keyboard_keys[layoutindex - 1][rowindex - 1][keyindex - 1].key = keyinfo[0];
					Xindex += keyWidth;
					keyboard_keys[layoutindex - 1][rowindex - 1][keyindex - 1].end_x = Xindex - 1;
				} else {
					// This key has extra data
					char* ptr;
					char* offset;
					char* keyitem;
					char foratoi[10];

					ptr = keyinfo;
					offset = keyinfo;
					while (*ptr > 32 && *ptr != ':')
						ptr++;
					if (*ptr != 0)
						*ptr = 0;

					strcpy(foratoi, offset);
					Xindex += atoi(foratoi);
					keyboard_keys[layoutindex - 1][rowindex - 1][keyindex - 1].end_x = Xindex - 1;

					ptr++;
					if (*ptr == 0) {
						// This is an empty area
						keyboard_keys[layoutindex - 1][rowindex - 1][keyindex - 1].key = 0;
					} else if (strlen(ptr) == 1) {
						// This is the character that this key uses
						keyboard_keys[layoutindex - 1][rowindex - 1][keyindex - 1].key = *ptr;
					} else if (*ptr == 'c') {
						// This is an ASCII character code
						keyitem = ptr + 2;
						strcpy(foratoi, keyitem);
						keyboard_keys[layoutindex - 1][rowindex - 1][keyindex - 1].key = atoi(foratoi);
					} else if (*ptr == 'l') {
						// This is a different layout
						keyitem = ptr + 6;
						keyboard_keys[layoutindex - 1][rowindex - 1][keyindex - 1].key = KEYBOARD_LAYOUT;
						strcpy(foratoi, keyitem);
						keyboard_keys[layoutindex - 1][rowindex - 1][keyindex - 1].layout = atoi(foratoi);
					} else if (*ptr == 'a') {
						// This is an action
						keyboard_keys[layoutindex - 1][rowindex - 1][keyindex - 1].key = KEYBOARD_ACTION;
					} else
						LOGE("Invalid key info on layout%i, row%i, key%02i.\n", layoutindex, rowindex, keyindex);
				}

				// PROCESS LONG PRESS INFO IF EXISTS
				sprintf(longpress, "long%02i", keyindex);
				attr = keyrow->first_attribute(longpress);
				if (attr) {
					stratt = attr->value();
					if (strlen(stratt.c_str()) >= 255) {
						LOGE("Key info on layout%i, row%i, key%dd is too long.\n", layoutindex, rowindex, keyindex);
						return;
					}

					strcpy(keyinfo, stratt.c_str());

					if (strlen(keyinfo) == 0) {
						LOGE("No long press info on layout%i, row%i, long%dd.\n", layoutindex, rowindex, keyindex);
						return;
					}

					if (strlen(keyinfo) == 1) {
						// This is a single key, simple definition
						keyboard_keys[layoutindex - 1][rowindex - 1][keyindex - 1].longpresskey = keyinfo[0];
					} else {
						// This key has extra data
						char* ptr;
						char* offset;
						char* keyitem;
						char foratoi[10];

						ptr = keyinfo;
						offset = keyinfo;
						while (*ptr > 32 && *ptr != ':')
							ptr++;
						if (*ptr != 0)
							*ptr = 0;

						strcpy(foratoi, offset);
						Xindex += atoi(foratoi);

						ptr++;
						if (*ptr == 0) {
							// This is an empty area
							keyboard_keys[layoutindex - 1][rowindex - 1][keyindex - 1].longpresskey = 0;
						} else if (strlen(ptr) == 1) {
							// This is the character that this key uses
							keyboard_keys[layoutindex - 1][rowindex - 1][keyindex - 1].longpresskey = *ptr;
						} else if (*ptr == 'c') {
							// This is an ASCII character code
							keyitem = ptr + 2;
							strcpy(foratoi, keyitem);
							keyboard_keys[layoutindex - 1][rowindex - 1][keyindex - 1].longpresskey = atoi(foratoi);
						} else if (*ptr == 'l') {
							// This is a different layout
							keyitem = ptr + 6;
							keyboard_keys[layoutindex - 1][rowindex - 1][keyindex - 1].longpresskey = KEYBOARD_LAYOUT;
							strcpy(foratoi, keyitem);
							keyboard_keys[layoutindex - 1][rowindex - 1][keyindex - 1].layout = atoi(foratoi);
						} else if (*ptr == 'a') {
							// This is an action
							keyboard_keys[layoutindex - 1][rowindex - 1][keyindex - 1].longpresskey = KEYBOARD_ACTION;
						} else
							LOGE("Invalid long press key info on layout%i, row%i, long%02i.\n", layoutindex, rowindex, keyindex);
					}
				}
				keyindex++;
				sprintf(key, "key%02i", keyindex);
				attr = keyrow->first_attribute(key);
			}
			rowindex++;
			row[3] = (char)(rowindex + 48);
			keyrow = keylayout->first_node(row);
		}
		layoutindex++;
		layout[6] = (char)(layoutindex + 48);
		keylayout = node->first_node(layout);
	}

	int x, y, w, h;
	// Load the placement
	LoadPlacement(node->first_node("placement"), &x, &y, &w, &h);
	SetActionPos(x, y, KeyboardWidth, KeyboardHeight);
	SetRenderPos(x, y, w, h);
	return;
}

GUIKeyboard::~GUIKeyboard()
{
	int layoutindex;

	for (layoutindex=0; layoutindex<MAX_KEYBOARD_LAYOUTS; layoutindex++)
		if (keyboardImg[layoutindex])   delete keyboardImg[layoutindex];
}

int GUIKeyboard::Render(void)
{
	if (!isConditionTrue())
	{
		mRendered = false;
		return 0;
	}

	int ret = 0;

	if (keyboardImg[currentLayout - 1] && keyboardImg[currentLayout - 1]->GetResource())
		gr_blit(keyboardImg[currentLayout - 1]->GetResource(), 0, 0, KeyboardWidth, KeyboardHeight, mRenderX, mRenderY);

	mRendered = true;
	return ret;
}

int GUIKeyboard::Update(void)
{
	if (!isConditionTrue())	 return (mRendered ? 2 : 0);
	if (!mRendered)			 return 2;

	return 0;
}

int GUIKeyboard::SetRenderPos(int x, int y, int w, int h)
{
	mRenderX = x;
	mRenderY = y;
	if (w || h)
	{
		mRenderW = KeyboardWidth;
		mRenderH = KeyboardHeight;
	}

	if (mAction)		mAction->SetActionPos(mRenderX, mRenderY, mRenderW, mRenderH);
	SetActionPos(mRenderX, mRenderY, mRenderW, mRenderH);
	return 0;
}

int GUIKeyboard::GetSelection(int x, int y)
{
	if (x < mRenderX || x - mRenderX > mRenderW || y < mRenderY || y - mRenderY > mRenderH) return -1;
	return 0;
}

int GUIKeyboard::NotifyTouch(TOUCH_STATE state, int x, int y)
{
	static int startSelection = -1, was_held = 0, startX = 0;
	static unsigned char initial_key = 0;
	unsigned int indexy, indexx, rely, relx, rowIndex = 0;

	rely = y - mRenderY;
	relx = x - mRenderX;

	if (!isConditionTrue())	 return -1;

	switch (state)
	{
	case TOUCH_START:
		if (GetSelection(x, y) == 0) {
			startSelection = -1;
			was_held = 0;
			startX = x;
			// Find the correct row
			for (indexy=0; indexy<MAX_KEYBOARD_ROWS; indexy++) {
				if (row_heights[currentLayout - 1][indexy] > rely) {
					rowIndex = indexy;
					indexy = MAX_KEYBOARD_ROWS;
				}
			}

			// Find the correct key (column)
			for (indexx=0; indexx<MAX_KEYBOARD_KEYS; indexx++) {
				if (keyboard_keys[currentLayout - 1][rowIndex][indexx].end_x > relx) {
					// This is the key that was pressed!
					initial_key = keyboard_keys[currentLayout - 1][rowIndex][indexx].key;
					indexx = MAX_KEYBOARD_KEYS;
				}
			}
		} else {
			startSelection = 0;
		}
		break;
	case TOUCH_DRAG:
		break;
	case TOUCH_RELEASE:
		if (x < startX - (mRenderW * 0.5)) {
			PageManager::NotifyKeyboard(KEYBOARD_SWIPE_LEFT);
			return 0;
		} else if (x > startX + (mRenderW * 0.5)) {
			PageManager::NotifyKeyboard(KEYBOARD_SWIPE_RIGHT);
			return 0;
		}

	case TOUCH_HOLD:
	case TOUCH_REPEAT:

		if (startSelection == 0 || GetSelection(x, y) == -1)
			return 0;

		// Find the correct row
		for (indexy=0; indexy<MAX_KEYBOARD_ROWS; indexy++) {
			if (row_heights[currentLayout - 1][indexy] > rely) {
				rowIndex = indexy;
				indexy = MAX_KEYBOARD_ROWS;
			}
		}

		// Find the correct key (column)
		for (indexx=0; indexx<MAX_KEYBOARD_KEYS; indexx++) {
			if (keyboard_keys[currentLayout - 1][rowIndex][indexx].end_x > relx) {
				// This is the key that was pressed!
				if (keyboard_keys[currentLayout - 1][rowIndex][indexx].key != initial_key) {
					// We dragged off of the starting key
					startSelection = 0;
					break;
				} else if (state == TOUCH_RELEASE && was_held == 0) {
					if ((int)keyboard_keys[currentLayout - 1][rowIndex][indexx].key < KEYBOARD_SPECIAL_KEYS && (int)keyboard_keys[currentLayout - 1][rowIndex][indexx].key > 0) {
						// Regular key
						PageManager::NotifyKeyboard(keyboard_keys[currentLayout - 1][rowIndex][indexx].key);
					} else if ((int)keyboard_keys[currentLayout - 1][rowIndex][indexx].key == KEYBOARD_LAYOUT) {
						// Switch layouts
						currentLayout = keyboard_keys[currentLayout - 1][rowIndex][indexx].layout;
						mRendered = false;
					} else if ((int)keyboard_keys[currentLayout - 1][rowIndex][indexx].key == KEYBOARD_ACTION) {
						// Action
						if (mAction) {
							// Keyboard has its own action defined
							return (mAction ? mAction->NotifyTouch(state, x, y) : 1);
						} else {
							// Send action notification
							PageManager::NotifyKeyboard(keyboard_keys[currentLayout - 1][rowIndex][indexx].key);
						}
					}
				} else if (state == TOUCH_HOLD) {
					was_held = 1;
					if ((int)keyboard_keys[currentLayout - 1][rowIndex][indexx].key == KEYBOARD_BACKSPACE) {
						// Repeat backspace
						PageManager::NotifyKeyboard(keyboard_keys[currentLayout - 1][rowIndex][indexx].key);
					} else if ((int)keyboard_keys[currentLayout - 1][rowIndex][indexx].longpresskey < KEYBOARD_SPECIAL_KEYS && (int)keyboard_keys[currentLayout - 1][rowIndex][indexx].longpresskey > 0) {
						// Long Press Key
						PageManager::NotifyKeyboard(keyboard_keys[currentLayout - 1][rowIndex][indexx].longpresskey);
					}
				} else if (state == TOUCH_REPEAT) {
					was_held = 1;
					if ((int)keyboard_keys[currentLayout - 1][rowIndex][indexx].key == KEYBOARD_BACKSPACE) {
						// Repeat backspace
						PageManager::NotifyKeyboard(keyboard_keys[currentLayout - 1][rowIndex][indexx].key);
					}
				}
				indexx = MAX_KEYBOARD_KEYS;
			}
		}
		break;
	}

	return 0;
}
