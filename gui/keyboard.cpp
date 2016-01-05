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

bool GUIKeyboard::CtrlActive = false;

GUIKeyboard::GUIKeyboard(xml_node<>* node)
	: GUIObject(node)
{
	int layoutindex, rowindex, keyindex, Xindex, Yindex, keyHeight = 0, keyWidth = 0;
	currentKey = NULL;
	highlightRenderCount = 0;
	hasHighlight = hasCapsHighlight = hasCtrlHighlight = false;
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

	if (!node)  return;

	mHighlightColor = LoadAttrColor(FindNode(node, "highlight"), "color", &hasHighlight);
	mCapsHighlightColor = LoadAttrColor(FindNode(node, "capshighlight"), "color", &hasCapsHighlight);
	mCtrlHighlightColor = LoadAttrColor(FindNode(node, "ctrlhighlight"), "color", &hasCtrlHighlight);

	child = FindNode(node, "keymargin");
	mKeyMarginX = LoadAttrIntScaleX(child, "x", 0);
	mKeyMarginY = LoadAttrIntScaleY(child, "y", 0);

	child = FindNode(node, "background");
	mBackgroundColor = LoadAttrColor(child, "color", COLOR(32,32,32,255));

	child = FindNode(node, "key-alphanumeric");
	mFont = PageManager::GetResources()->FindFont(LoadAttrString(child, "font", "keylabel"));
	mFontColor = LoadAttrColor(child, "textcolor", COLOR(255,255,255,255));
	mKeyColorAlphanumeric = LoadAttrColor(child, "color", COLOR(0,0,0,0));

	child = FindNode(node, "key-other");
	mSmallFont = PageManager::GetResources()->FindFont(LoadAttrString(child, "font", "keylabel-small"));
	mFontColorSmall = LoadAttrColor(child, "textcolor", COLOR(192,192,192,255));
	mKeyColorOther = LoadAttrColor(child, "color", COLOR(0,0,0,0));

	child = FindNode(node, "longpress");
	mLongpressFont = PageManager::GetResources()->FindFont(LoadAttrString(child, "font", "keylabel-longpress"));
	mLongpressFontColor = LoadAttrColor(child, "textcolor", COLOR(128,128,128,255));
	LoadPlacement(child, &longpressOffsetX, &longpressOffsetY);

	LoadKeyLabels(node, 0); // load global key labels

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
		mRenderW = layouts[0].keyboardImg->GetWidth();
		mRenderH = layouts[0].keyboardImg->GetHeight();
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

		LoadKeyLabels(keylayout, layoutindex); // load per-layout key labels

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
	LoadPlacement(FindNode(node, "placement"), &x, &y, &mRenderW, &mRenderH);
	SetRenderPos(x, y, mRenderW, mRenderH);
	return;
}

GUIKeyboard::~GUIKeyboard()
{
}

int GUIKeyboard::ParseKey(const char* keyinfo, Key& key, int& Xindex, int keyWidth, bool longpress)
{
	key.layout = 0;
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
		} else if (*ptr == 'k') {  // This is a Linux keycode from input.h: "k:{number}"
			keychar = -atoi(ptr + 2);
		} else if (*ptr == 'l') {  // This is a different layout: "layout{number}"
			key.layout = atoi(ptr + 6);
		} else if (*ptr == 'a') {  // This is an action: "action" (the Enter key)
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

void GUIKeyboard::LoadKeyLabels(xml_node<>* parent, int layout)
{
	for (xml_node<>* child = parent->first_node(); child; child = child->next_sibling()) {
		std::string name = child->name();
		if (name == "keylabel") {
			std::string keydef = LoadAttrString(child, "key", "");
			Key tempkey;
			int dummyX;
			if (ParseKey(keydef.c_str(), tempkey, dummyX, 0, false) == 0) {
				KeyLabel keylabel;
				keylabel.key = tempkey.key;
				keylabel.layout_from = layout;
				keylabel.layout_to = tempkey.layout;
				keylabel.text = LoadAttrString(child, "text", "");
				keylabel.image = LoadAttrImage(child, "resource");
				mKeyLabels.push_back(keylabel);
			} else {
				LOGERR("Ignoring invalid keylabel in layout %d: '%s'.\n", layout, keydef.c_str());
			}
		}
	}
}

void GUIKeyboard::DrawKey(Key& key, int keyX, int keyY, int keyW, int keyH)
{
	int keychar = key.key;
	if (!keychar && !key.layout)
		return;

	// key background
	COLOR& c = (keychar >= 32 && keychar < 127) ? mKeyColorAlphanumeric : mKeyColorOther;
	gr_color(c.red, c.green, c.blue, c.alpha);
	keyX += mKeyMarginX;
	keyY += mKeyMarginY;
	keyW -= mKeyMarginX * 2;
	keyH -= mKeyMarginY * 2;
	gr_fill(keyX, keyY, keyW, keyH);

	// key label
	FontResource* labelFont = mFont;
	string labelText;
	ImageResource* labelImage = NULL;
	if (keychar > 32 && keychar < 127) {
		// TODO: this will eventually need UTF-8 support
		labelText = (char) keychar;
		if (CtrlActive) {
			int ctrlchar = KeyCharToCtrlChar(keychar);
			if (ctrlchar != keychar)
				labelText = std::string("^") + (char)(ctrlchar + 64);
		}
		gr_color(mFontColor.red, mFontColor.green, mFontColor.blue, mFontColor.alpha);
	}
	else {
		// search for a special key label
		for (std::vector<KeyLabel>::iterator it = mKeyLabels.begin(); it != mKeyLabels.end(); ++it) {
			if (it->layout_from > 0 && it->layout_from != currentLayout)
				continue; // this label is for another layout
			if (it->key == key.key && it->layout_to == key.layout)
			{
				// found a label
				labelText = it->text;
				labelImage = it->image;
				break;
			}
		}
		labelFont = mSmallFont;
		gr_color(mFontColorSmall.red, mFontColorSmall.green, mFontColorSmall.blue, mFontColorSmall.alpha);
	}

	if (labelImage)
	{
		int w = labelImage->GetWidth();
		int h = labelImage->GetHeight();
		int x = keyX + (keyW - w) / 2;
		int y = keyY + (keyH - h) / 2;
		gr_blit(labelImage->GetResource(), 0, 0, w, h, x, y);
	}
	else if (!labelText.empty())
	{
		void* fontResource = labelFont->GetResource();
		int textW = gr_measureEx(labelText.c_str(), fontResource);
		int textH = labelFont->GetHeight();
		int textX = keyX + (keyW - textW) / 2;
		int textY = keyY + (keyH - textH) / 2;
		gr_textEx_scaleW(textX, textY, labelText.c_str(), fontResource, keyW, TOP_LEFT, 0);
	}

	// longpress key label (only if font is defined)
	keychar = key.longpresskey;
	if (keychar > 32 && keychar < 127 && mLongpressFont->GetResource()) {
		void* fontResource = mLongpressFont->GetResource();
		gr_color(mLongpressFontColor.red, mLongpressFontColor.green, mLongpressFontColor.blue, mLongpressFontColor.alpha);
		string text(1, keychar);
		int textH = mLongpressFont->GetHeight();
		int textW = gr_measureEx(text.c_str(), fontResource);
		int textX = keyX + keyW - longpressOffsetX - textW;
		int textY = keyY + longpressOffsetY;
		gr_textEx_scaleW(textX, textY, text.c_str(), fontResource, keyW, TOP_LEFT, 0);
	}
}

int GUIKeyboard::KeyCharToCtrlChar(int key)
{
	// convert upper and lower case to ctrl chars
	// Ctrl+A to Ctrl+_ (we don't support entering null bytes)
	if (key >= 65 && key <= 127 && key != 96)
		return key & 0x1f;
	return key; // pass on others (already ctrl chars, numbers, etc.) unchanged
}

int GUIKeyboard::Render(void)
{
	if (!isConditionTrue())
	{
		mRendered = false;
		return 0;
	}

	Layout& lay = layouts[currentLayout - 1];

	bool drawKeys = false;
	if (lay.keyboardImg && lay.keyboardImg->GetResource())
		// keyboard is image based
		gr_blit(lay.keyboardImg->GetResource(), 0, 0, mRenderW, mRenderH, mRenderX, mRenderY);
	else {
		// keyboard is software drawn
		// fill background
		gr_color(mBackgroundColor.red, mBackgroundColor.green, mBackgroundColor.blue, mBackgroundColor.alpha);
		gr_fill(mRenderX, mRenderY, mRenderW, mRenderH);
		drawKeys = true;
	}

	// draw keys
	int y1 = 0;
	for (int row = 0; row < MAX_KEYBOARD_ROWS; ++row) {
		int rowY = mRenderY + y1;
		int rowH = lay.row_end_y[row] - y1;
		y1 = lay.row_end_y[row];
		int x1 = 0;
		for (int col = 0; col < MAX_KEYBOARD_KEYS; ++col) {
			Key& key = lay.keys[row][col];
			int keyY = rowY;
			int keyH = rowH;
			int keyX = mRenderX + x1;
			int keyW = key.end_x - x1;
			x1 = key.end_x;

			// Draw key for software drawn keyboard
			if (drawKeys)
				DrawKey(key, keyX, keyY, keyW, keyH);

			// Draw highlight for capslock
			if (hasCapsHighlight && lay.is_caps && CapsLockOn && key.layout > 0 && key.layout == lay.revert_layout) {
				gr_color(mCapsHighlightColor.red, mCapsHighlightColor.green, mCapsHighlightColor.blue, mCapsHighlightColor.alpha);
				gr_fill(keyX, keyY, keyW, keyH);
			}

			// Draw highlight for control
			if (hasCtrlHighlight && key.key == -KEY_LEFTCTRL && CtrlActive) {
				gr_color(mCtrlHighlightColor.red, mCtrlHighlightColor.green, mCtrlHighlightColor.blue, mCtrlHighlightColor.alpha);
				gr_fill(keyX, keyY, keyW, keyH);
			}

			// Highlight current key
			if (hasHighlight && &key == currentKey && highlightRenderCount != 0) {
				gr_color(mHighlightColor.red, mHighlightColor.green, mHighlightColor.blue, mHighlightColor.alpha);
				gr_fill(keyX, keyY, keyW, keyH);
			}
		}
	}

	if (!hasHighlight || highlightRenderCount == 0)
		mRendered = true;
	else if (highlightRenderCount > 0)
		highlightRenderCount--;
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
	RenderObject::SetRenderPos(x, y, w, h);
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
		if (x1 <= relx && relx < key.end_x && (key.key != 0 || key.layout != 0)) {
			// This is the key that was pressed!
			return &key;
		}
		x1 = key.end_x;
	}
	return NULL;
}

int GUIKeyboard::NotifyTouch(TOUCH_STATE state, int x, int y)
{
	static int was_held = 0, startX = 0;

	if (!isConditionTrue())	 return -1;

	switch (state)
	{
	case TOUCH_START:
		was_held = 0;
		startX = x;
		currentKey = HitTestKey(x, y);
		if (currentKey)
			highlightRenderCount = -1;
		else
			highlightRenderCount = 0;
		mRendered = false;
		break;

	case TOUCH_DRAG:
		break;

	case TOUCH_RELEASE:
		// TODO: we might want to notify of key releases here
		if (x < startX - (mRenderW * 0.5)) {
			if (highlightRenderCount != 0) {
				highlightRenderCount = 0;
				mRendered = false;
			}
			PageManager::NotifyCharInput(KEYBOARD_SWIPE_LEFT);
			return 0;
		} else if (x > startX + (mRenderW * 0.5)) {
			if (highlightRenderCount != 0) {
				highlightRenderCount = 0;
				mRendered = false;
			}
			PageManager::NotifyCharInput(KEYBOARD_SWIPE_RIGHT);
			return 0;
		}
		// fall through
	case TOUCH_HOLD:
	case TOUCH_REPEAT:
		if (!currentKey) {
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

		if (HitTestKey(x, y) != currentKey) {
			// We dragged off of the starting key
			currentKey = NULL;
			if (highlightRenderCount != 0) {
				highlightRenderCount = 0;
				mRendered = false;
			}
			return 0;
		} else {
			Key& key = *currentKey;
			bool repeatKey = false;
			Layout& lay = layouts[currentLayout - 1];
			if (state == TOUCH_RELEASE && was_held == 0) {
				DataManager::Vibrate("tw_keyboard_vibrate");
				if (key.layout > 0) {
					// Switch layouts
					if (lay.is_caps && key.layout == lay.revert_layout && !CapsLockOn) {
						CapsLockOn = true; // Set the caps lock
					} else {
						CapsLockOn = false; // Unset the caps lock and change layouts
						currentLayout = key.layout;
					}
					mRendered = false;
				} else if (key.key == KEYBOARD_ACTION) {
					// Action
					highlightRenderCount = 0;
					// Send action notification
					PageManager::NotifyCharInput(key.key);
				} else if (key.key == -KEY_LEFTCTRL) {
					CtrlActive = !CtrlActive; // toggle Control key state
					mRendered = false; // render Ctrl key highlight
				} else if (key.key != 0) {
					// Regular key
					if (key.key > 0) {
						// ASCII code or character
						int keycode = key.key;
						if (CtrlActive) {
							CtrlActive = false;
							mRendered = false;
							keycode = KeyCharToCtrlChar(key.key);
						}
						PageManager::NotifyCharInput(keycode);
					} else {
						// Linux key code
						PageManager::NotifyKey(-key.key, true);
						PageManager::NotifyKey(-key.key, false);
					}
					if (!CapsLockOn && lay.is_caps) {
						// caps lock was not set, change layouts
						currentLayout = lay.revert_layout;
						mRendered = false;
					}
				}
			} else if (state == TOUCH_HOLD) {
				was_held = 1;
				if (key.longpresskey > 0) {
					// Long Press Key
					DataManager::Vibrate("tw_keyboard_vibrate");
					PageManager::NotifyCharInput(key.longpresskey);
				}
				else
					repeatKey = true;
			} else if (state == TOUCH_REPEAT) {
				was_held = 1;
				repeatKey = true;
			}
			if (repeatKey) {
				if (key.key == KEYBOARD_BACKSPACE) {
					// Repeat backspace
					PageManager::NotifyCharInput(key.key);
				}
				switch (key.key)
				{
					// Repeat arrows
					case -KEY_LEFT:
					case -KEY_RIGHT:
					case -KEY_UP:
					case -KEY_DOWN:
						PageManager::NotifyKey(-key.key, true);
						PageManager::NotifyKey(-key.key, false);
						break;
				}
			}
		}
		break;
	}

	return 0;
}

void GUIKeyboard::SetPageFocus(int inFocus)
{
	if (inFocus)
		CtrlActive = false;
}
