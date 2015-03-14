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

#include <stdlib.h>
#include <string.h>
#include "../data.hpp"

#include <string>

extern "C" {
#include "../twcommon.h"
#include "../minuitwrp/minui.h"
#include "gui.h"
}

#include "rapidxml.hpp"
#include "objects.hpp"

GUIKeyboard::GUIKeyboard(xml_node<>* node)
	: GUIObject(node)
{
	int layoutindex, rowindex, keyindex, Xindex, Yindex, keyHeight = 0, keyWidth = 0;
	rowY = colX = -1;
	highlightRenderCount = 0;
	hasHighlight = hasCapsHighlight = false;
	char resource[10], layout[8], row[5], key[6], longpress[7];
	xml_attribute<>* attr;
	xml_node<>* child;
	xml_node<>* keylayout;
	xml_node<>* keyrow;

	for (layoutindex=0; layoutindex<MAX_KEYBOARD_LAYOUTS; layoutindex++) {
		layouts[layoutindex].keyboardImg = NULL;
		memset(layouts[layoutindex].keys, 0, sizeof(Layout::keys));
		memset(layouts[layoutindex].row_end_y, 0, sizeof(Layout::row_end_y));
	}

	mRendered = false;
	currentLayout = 1;
	CapsLockOn = false;
	KeyboardHeight = KeyboardWidth = 0;

	if (!node)  return;

	mHighlightColor = LoadAttrColor(FindNode(node, "highlight"), "color", &hasHighlight);
	mCapsHighlightColor = LoadAttrColor(FindNode(node, "capshighlight"), "color", &hasCapsHighlight);

	// compatibility ugliness: resources should be specified in the layouts themselves instead
	// Load the images for the different layouts
	child = FindNode(node, "layout");
	if (child)
	{
		layoutindex = 1;
		strcpy(resource, "resource1");
		attr = child->first_attribute(resource);
		while (attr && layoutindex < (MAX_KEYBOARD_LAYOUTS + 1)) {
			layouts[layoutindex - 1].keyboardImg = LoadAttrImage(child, resource);

			layoutindex++;
			resource[8] = (char)(layoutindex + 48);
			attr = child->first_attribute(resource);
		}
	}

	// Check the first image to get height and width
	if (layouts[0].keyboardImg && layouts[0].keyboardImg->GetResource())
	{
		KeyboardWidth = layouts[0].keyboardImg->GetWidth();
		KeyboardHeight = layouts[0].keyboardImg->GetHeight();
	}

	// Load all of the layout maps
	layoutindex = 1;
	strcpy(layout, "layout1");
	keylayout = FindNode(node, layout);
	while (keylayout)
	{
		if (layoutindex > MAX_KEYBOARD_LAYOUTS) {
			LOGERR("Too many layouts defined in keyboard.\n");
			return;
		}

		Layout& lay = layouts[layoutindex - 1];

		child = keylayout->first_node("keysize");
		keyHeight = LoadAttrIntScaleY(child, "height", 0);
		keyWidth = LoadAttrIntScaleX(child, "width", 0);
		// compatibility ugliness: capslock="0" means that this is the caps layout. Also it has nothing to do with keysize.
		lay.is_caps = (LoadAttrInt(child, "capslock", 1) == 0);
		// compatibility ugliness: revert_layout has nothing to do with keysize.
		lay.revert_layout = LoadAttrInt(child, "revert_layout", -1);

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
			lay.row_end_y[rowindex - 1] = Yindex;

			keyindex = 1;
			Xindex = 0;
			strcpy(key, "key01");
			attr = keyrow->first_attribute(key);

			while (attr) {
				if (keyindex > MAX_KEYBOARD_KEYS) {
					LOGERR("Too many keys defined in a keyboard row.\n");
					return;
				}

				const char* keyinfo = attr->value();

				if (strlen(keyinfo) == 0) {
					LOGERR("No key info on layout%i, row%i, key%dd.\n", layoutindex, rowindex, keyindex);
					return;
				}

				if (ParseKey(keyinfo, lay.keys[rowindex - 1][keyindex - 1], Xindex, keyWidth, false))
					LOGERR("Invalid key info on layout%i, row%i, key%02i.\n", layoutindex, rowindex, keyindex);


				// PROCESS LONG PRESS INFO IF EXISTS
				sprintf(longpress, "long%02i", keyindex);
				attr = keyrow->first_attribute(longpress);
				if (attr) {
					const char* keyinfo = attr->value();

					if (strlen(keyinfo) == 0) {
						LOGERR("No long press info on layout%i, row%i, long%dd.\n", layoutindex, rowindex, keyindex);
						return;
					}

					if (ParseKey(keyinfo, lay.keys[rowindex - 1][keyindex - 1], Xindex, keyWidth, true))
						LOGERR("Invalid long press key info on layout%i, row%i, long%02i.\n", layoutindex, rowindex, keyindex);
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
		keylayout = FindNode(node, layout);
	}

	int x, y;
	// Load the placement
	LoadPlacement(FindNode(node, "placement"), &x, &y);
	SetRenderPos(x, y, KeyboardWidth, KeyboardHeight);
	return;
}

GUIKeyboard::~GUIKeyboard()
{
}

int GUIKeyboard::ParseKey(const char* keyinfo, Key& key, int& Xindex, int keyWidth, bool longpress)
{
	int keychar = 0;
	if (strlen(keyinfo) == 1) {
		// This is a single key, simple definition
		keychar = keyinfo[0];
	} else {
		// This key has extra data: {keywidth}:{what_the_key_does}
		keyWidth = scale_theme_x(atoi(keyinfo));

		const char* ptr = keyinfo;
		while (*ptr > 32 && *ptr != ':')
			ptr++;
		if (*ptr != ':')
			return -1;  // no colon is an error
		ptr++;

		if (*ptr == 0) {  // This is an empty area
			keychar = 0;
		} else if (strlen(ptr) == 1) {  // This is the character that this key uses
			keychar = *ptr;
		} else if (*ptr == 'c') {  // This is an ASCII character code: "c:{number}"
			keychar = atoi(ptr + 2);
		} else if (*ptr == 'l') {  // This is a different layout: "layout{number}"
			keychar = KEYBOARD_LAYOUT;
			key.layout = atoi(ptr + 6);
		} else if (*ptr == 'a') {  // This is an action: "action"
			keychar = KEYBOARD_ACTION;
		} else
			return -1;
	}

	if (longpress) {
		key.longpresskey = keychar;
	} else {
		key.key = keychar;
		Xindex += keyWidth;
		key.end_x = Xindex - 1;
	}

	return 0;
}

int GUIKeyboard::Render(void)
{
	if (!isConditionTrue())
	{
		mRendered = false;
		return 0;
	}

	Layout& lay = layouts[currentLayout - 1];

	if (lay.keyboardImg && lay.keyboardImg->GetResource())
		gr_blit(lay.keyboardImg->GetResource(), 0, 0, KeyboardWidth, KeyboardHeight, mRenderX, mRenderY);

	// Draw highlight for capslock
	if (hasCapsHighlight && lay.is_caps && CapsLockOn) {
		gr_color(mCapsHighlightColor.red, mCapsHighlightColor.green, mCapsHighlightColor.blue, mCapsHighlightColor.alpha);
		for (int indexy=0; indexy<MAX_KEYBOARD_ROWS; indexy++) {
			for (int indexx=0; indexx<MAX_KEYBOARD_KEYS; indexx++) {
				if ((int)lay.keys[indexy][indexx].key == KEYBOARD_LAYOUT && (int)lay.keys[indexy][indexx].layout == lay.revert_layout) {
					int boxheight, boxwidth, x;
					if (indexy == 0)
						boxheight = lay.row_end_y[indexy];
					else
						boxheight = lay.row_end_y[indexy] - lay.row_end_y[indexy - 1];
					if (indexx == 0) {
						x = mRenderX;
						boxwidth = lay.keys[indexy][indexx].end_x;
					} else {
						x = mRenderX + lay.keys[indexy][indexx - 1].end_x;
						boxwidth = lay.keys[indexy][indexx].end_x - lay.keys[indexy][indexx - 1].end_x;
					}
					gr_fill(x, mRenderY + lay.row_end_y[indexy - 1], boxwidth, boxheight);
				}
			}
		}
	}

	if (hasHighlight && highlightRenderCount != 0) {
		int boxheight, boxwidth, x;
		if (rowY == 0)
			boxheight = lay.row_end_y[rowY];
		else
			boxheight = lay.row_end_y[rowY] - lay.row_end_y[rowY - 1];
		if (colX == 0) {
			x = mRenderX;
			boxwidth = lay.keys[rowY][colX].end_x;
		} else {
			x = mRenderX + lay.keys[rowY][colX - 1].end_x;
			boxwidth = lay.keys[rowY][colX].end_x - lay.keys[rowY][colX - 1].end_x;
		}
		gr_color(mHighlightColor.red, mHighlightColor.green, mHighlightColor.blue, mHighlightColor.alpha);
		gr_fill(x, mRenderY + lay.row_end_y[rowY - 1], boxwidth, boxheight);
		if (highlightRenderCount > 0)
			highlightRenderCount--;
	} else
		mRendered = true;

	return 0;
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
	mRenderW = KeyboardWidth;
	mRenderH = KeyboardHeight;
	SetActionPos(mRenderX, mRenderY, mRenderW, mRenderH);
	return 0;
}

GUIKeyboard::Key* GUIKeyboard::HitTestKey(int x, int y)
{
	if (!IsInRegion(x, y))
		return NULL;

	int rely = y - mRenderY;
	int relx = x - mRenderX;

	Layout& lay = layouts[currentLayout - 1];

	// Find the correct row
	int row;
	for (row = 0; row < MAX_KEYBOARD_ROWS; ++row) {
		if (lay.row_end_y[row] > rely)
			break;
	}
	if (row == MAX_KEYBOARD_ROWS)
		return NULL;

	// Find the correct key (column)
	int col;
	int x1 = 0;
	for (col = 0; col < MAX_KEYBOARD_KEYS; ++col) {
		Key& key = lay.keys[row][col];
		if (x1 <= relx && relx < key.end_x && key.key != 0) {
			// This is the key that was pressed!
			rowY = row;
			colX = col;
			return &key;
		}
		x1 = key.end_x;
	}
	return NULL;
}

int GUIKeyboard::NotifyTouch(TOUCH_STATE state, int x, int y)
{
	static int was_held = 0, startX = 0;
	static Key* initial_key = 0;

	if (!isConditionTrue())	 return -1;

	switch (state)
	{
	case TOUCH_START:
		was_held = 0;
		startX = x;
		initial_key = HitTestKey(x, y);
		if (initial_key)
			highlightRenderCount = -1;
		else
			highlightRenderCount = 0;
		mRendered = false;
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
		// fall through
	case TOUCH_HOLD:
	case TOUCH_REPEAT:
		if (!initial_key) {
			if (highlightRenderCount != 0) {
				highlightRenderCount = 0;
				mRendered = false;
			}
			return 0;
		}

		if (highlightRenderCount != 0) {
			if (state == TOUCH_RELEASE)
				highlightRenderCount = 2;
			else
				highlightRenderCount = -1;
			mRendered = false;
		}

		if (HitTestKey(x, y) != initial_key) {
			// We dragged off of the starting key
			if (highlightRenderCount != 0) {
				highlightRenderCount = 0;
				mRendered = false;
			}
			return 0;
		} else {
			Key& key = *initial_key;
			Layout& lay = layouts[currentLayout - 1];
			if (state == TOUCH_RELEASE && was_held == 0) {
				DataManager::Vibrate("tw_keyboard_vibrate");
				if ((int)key.key == KEYBOARD_LAYOUT) {
					// Switch layouts
					if (lay.is_caps && key.layout == lay.revert_layout && !CapsLockOn) {
						CapsLockOn = true; // Set the caps lock
					} else {
						CapsLockOn = false; // Unset the caps lock and change layouts
						currentLayout = key.layout;
					}
					mRendered = false;
				} else if ((int)key.key == KEYBOARD_ACTION) {
					// Action
					highlightRenderCount = 0;
					// Send action notification
					PageManager::NotifyKeyboard(key.key);
				} else if ((int)key.key < KEYBOARD_SPECIAL_KEYS && (int)key.key > 0) {
					// Regular key
					PageManager::NotifyKeyboard(key.key);
					if (!CapsLockOn && lay.is_caps) {
						// caps lock was not set, change layouts
						currentLayout = lay.revert_layout;
						mRendered = false;
					}
				}
			} else if (state == TOUCH_HOLD) {
				was_held = 1;
				if ((int)key.key == KEYBOARD_BACKSPACE) {
					// Repeat backspace
					PageManager::NotifyKeyboard(key.key);
				} else if ((int)key.longpresskey < KEYBOARD_SPECIAL_KEYS && (int)key.longpresskey > 0) {
					// Long Press Key
					DataManager::Vibrate("tw_keyboard_vibrate");
					PageManager::NotifyKeyboard(key.longpresskey);
				}
			} else if (state == TOUCH_REPEAT) {
				was_held = 1;
				if ((int)key.key == KEYBOARD_BACKSPACE) {
					// Repeat backspace
					PageManager::NotifyKeyboard(key.key);
				}
			}
		}
		break;
	}

	return 0;
}
