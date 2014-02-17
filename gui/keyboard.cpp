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
#include "../data.hpp"

#include <string>

extern "C" {
#include "../twcommon.h"
#include "../minuitwrp/minui.h"
}

#include "rapidxml.hpp"
#include "objects.hpp"

GUIKeyboard::GUIKeyboard(xml_node<>* node)
	: GUIObject(node)
{
	int layoutindex, rowindex, keyindex, Xindex, Yindex, keyHeight = 0, keyWidth = 0;
	rowY = colX = -1;
	highlightRenderCount = hasHighlight = hasCapsHighlight = 0;
	char resource[10], layout[8], row[5], key[6], longpress[7];
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

	memset(&mHighlightColor, 0, sizeof(COLOR));
	child = node->first_node("highlight");
	if (child) {
		attr = child->first_attribute("color");
		if (attr) {
			hasHighlight = 1;
			std::string color = attr->value();
			ConvertStrToColor(color, &mHighlightColor);
		}
	}

	memset(&mCapsHighlightColor, 0, sizeof(COLOR));
	child = node->first_node("capshighlight");
	if (child) {
		attr = child->first_attribute("color");
		if (attr) {
			hasCapsHighlight = 1;
			std::string color = attr->value();
			ConvertStrToColor(color, &mCapsHighlightColor);
		}
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
			LOGERR("Too many layouts defined in keyboard.\n");
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
			attr = child->first_attribute("capslock");
			if (attr)
				caps_tracking[layoutindex - 1].capslock = atoi(attr->value());
			else
				caps_tracking[layoutindex - 1].capslock = 1;
			attr = child->first_attribute("revert_layout");
			if (attr)
				caps_tracking[layoutindex - 1].revert_layout = atoi(attr->value());
			else
				caps_tracking[layoutindex - 1].revert_layout = -1;
		}

		rowindex = 1;
		Yindex = 0;
		strcpy(row, "row1");
		keyrow = keylayout->first_node(row);
		while (keyrow)
		{
			if (rowindex > MAX_KEYBOARD_ROWS) {
				LOGERR("Too many rows defined in keyboard.\n");
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
					LOGERR("Too many keys defined in a keyboard row.\n");
					return;
				}

				stratt = attr->value();
				if (strlen(stratt.c_str()) >= 255) {
					LOGERR("Key info on layout%i, row%i, key%dd is too long.\n", layoutindex, rowindex, keyindex);
					return;
				}

				strcpy(keyinfo, stratt.c_str());

				if (strlen(keyinfo) == 0) {
					LOGERR("No key info on layout%i, row%i, key%dd.\n", layoutindex, rowindex, keyindex);
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
						LOGERR("Invalid key info on layout%i, row%i, key%02i.\n", layoutindex, rowindex, keyindex);
				}

				// PROCESS LONG PRESS INFO IF EXISTS
				sprintf(longpress, "long%02i", keyindex);
				attr = keyrow->first_attribute(longpress);
				if (attr) {
					stratt = attr->value();
					if (strlen(stratt.c_str()) >= 255) {
						LOGERR("Key info on layout%i, row%i, key%dd is too long.\n", layoutindex, rowindex, keyindex);
						return;
					}

					strcpy(keyinfo, stratt.c_str());

					if (strlen(keyinfo) == 0) {
						LOGERR("No long press info on layout%i, row%i, long%dd.\n", layoutindex, rowindex, keyindex);
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
							LOGERR("Invalid long press key info on layout%i, row%i, long%02i.\n", layoutindex, rowindex, keyindex);
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

	// Draw highlight for capslock
	if (hasCapsHighlight && caps_tracking[currentLayout - 1].capslock == 0 && caps_tracking[currentLayout - 1].set_capslock) {
		int boxheight, boxwidth, x;
		gr_color(mCapsHighlightColor.red, mCapsHighlightColor.green, mCapsHighlightColor.blue, mCapsHighlightColor.alpha);
		for (int indexy=0; indexy<MAX_KEYBOARD_ROWS; indexy++) {
			for (int indexx=0; indexx<MAX_KEYBOARD_KEYS; indexx++) {
				if ((int)keyboard_keys[currentLayout - 1][indexy][indexx].key == KEYBOARD_LAYOUT && (int)keyboard_keys[currentLayout - 1][indexy][indexx].layout == caps_tracking[currentLayout - 1].revert_layout) {
					if (indexy == 0)
						boxheight = row_heights[currentLayout - 1][indexy];
					else
						boxheight = row_heights[currentLayout - 1][indexy] - row_heights[currentLayout - 1][indexy - 1];
					if (indexx == 0) {
						x = mRenderX;
						boxwidth = keyboard_keys[currentLayout - 1][indexy][indexx].end_x;
					} else {
						x = mRenderX + keyboard_keys[currentLayout - 1][indexy][indexx - 1].end_x;
						boxwidth = keyboard_keys[currentLayout - 1][indexy][indexx].end_x - keyboard_keys[currentLayout - 1][indexy][indexx - 1].end_x;
					}
					gr_fill(x, mRenderY + row_heights[currentLayout - 1][indexy - 1], boxwidth, boxheight);
				}
			}
		}
	}

	if (hasHighlight && highlightRenderCount != 0) {
		int boxheight, boxwidth, x;
		if (rowY == 0)
			boxheight = row_heights[currentLayout - 1][rowY];
		else
			boxheight = row_heights[currentLayout - 1][rowY] - row_heights[currentLayout - 1][rowY - 1];
		if (colX == 0) {
			x = mRenderX;
			boxwidth = keyboard_keys[currentLayout - 1][rowY][colX].end_x;
		} else {
			x = mRenderX + keyboard_keys[currentLayout - 1][rowY][colX - 1].end_x;
			boxwidth = keyboard_keys[currentLayout - 1][rowY][colX].end_x - keyboard_keys[currentLayout - 1][rowY][colX - 1].end_x;
		}
		gr_color(mHighlightColor.red, mHighlightColor.green, mHighlightColor.blue, mHighlightColor.alpha);
		gr_fill(x, mRenderY + row_heights[currentLayout - 1][rowY - 1], boxwidth, boxheight);
		if (highlightRenderCount > 0)
			highlightRenderCount--;
	} else
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
	if (x < mRenderX || x - mRenderX > (int)KeyboardWidth || y < mRenderY || y - mRenderY > (int)KeyboardHeight) return -1;
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
					rowY = indexy;
					indexy = MAX_KEYBOARD_ROWS;
				}
			}

			// Find the correct key (column)
			for (indexx=0; indexx<MAX_KEYBOARD_KEYS; indexx++) {
				if (keyboard_keys[currentLayout - 1][rowIndex][indexx].end_x > relx) {
					// This is the key that was pressed!
					initial_key = keyboard_keys[currentLayout - 1][rowIndex][indexx].key;
					colX = indexx;
					indexx = MAX_KEYBOARD_KEYS;
				}
			}
			if (initial_key != 0)
				highlightRenderCount = -1;
			else
				highlightRenderCount = 0;
			mRendered = false;
		} else {
			if (highlightRenderCount != 0)
				mRendered = false;
			highlightRenderCount = 0;
			startSelection = 0;
		}
		break;
	case TOUCH_DRAG:
		break;
	case TOUCH_RELEASE:
		if (x < startX - (KeyboardWidth * 0.5)) {
			if (highlightRenderCount != 0) {
				highlightRenderCount = 0;
				mRendered = false;
			}
			PageManager::NotifyKeyboard(KEYBOARD_SWIPE_LEFT);
			return 0;
		} else if (x > startX + (KeyboardWidth * 0.5)) {
			if (highlightRenderCount != 0) {
				highlightRenderCount = 0;
				mRendered = false;
			}
			PageManager::NotifyKeyboard(KEYBOARD_SWIPE_RIGHT);
			return 0;
		}

	case TOUCH_HOLD:
	case TOUCH_REPEAT:

		if (startSelection == 0 || GetSelection(x, y) == -1) {
			if (highlightRenderCount != 0) {
				highlightRenderCount = 0;
				mRendered = false;
			}
			return 0;
		} else if (highlightRenderCount != 0) {
			if (state == TOUCH_RELEASE)
				highlightRenderCount = 2;
			else
				highlightRenderCount = -1;
			mRendered = false;
		}

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
					DataManager::Vibrate("tw_keyboard_vibrate");
					if ((int)keyboard_keys[currentLayout - 1][rowIndex][indexx].key < KEYBOARD_SPECIAL_KEYS && (int)keyboard_keys[currentLayout - 1][rowIndex][indexx].key > 0) {
						// Regular key
						PageManager::NotifyKeyboard(keyboard_keys[currentLayout - 1][rowIndex][indexx].key);
						if (caps_tracking[currentLayout - 1].capslock == 0 && !caps_tracking[currentLayout - 1].set_capslock) {
							// caps lock was not set, change layouts
							currentLayout = caps_tracking[currentLayout - 1].revert_layout;
							mRendered = false;
						}
					} else if ((int)keyboard_keys[currentLayout - 1][rowIndex][indexx].key == KEYBOARD_LAYOUT) {
						// Switch layouts
						if (caps_tracking[currentLayout - 1].capslock == 0 && keyboard_keys[currentLayout - 1][rowIndex][indexx].layout == caps_tracking[currentLayout - 1].revert_layout) {
							if (!caps_tracking[currentLayout - 1].set_capslock) {
								caps_tracking[currentLayout - 1].set_capslock = 1; // Set the caps lock
							} else {
								caps_tracking[currentLayout - 1].set_capslock = 0; // Unset the caps lock and change layouts
								currentLayout = keyboard_keys[currentLayout - 1][rowIndex][indexx].layout;
							}
						} else {
							currentLayout = keyboard_keys[currentLayout - 1][rowIndex][indexx].layout;
						}
						mRendered = false;
					} else if ((int)keyboard_keys[currentLayout - 1][rowIndex][indexx].key == KEYBOARD_ACTION) {
						// Action
						highlightRenderCount = 0;
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
						DataManager::Vibrate("tw_keyboard_vibrate");
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
