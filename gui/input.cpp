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

// input.cpp - GUIInput object

#include <linux/input.h>
#include <pthread.h>
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
#include "../twcommon.h"
}
#include "../minuitwrp/minui.h"

#include "rapidxml.hpp"
#include "objects.hpp"
#include "../data.hpp"

GUIInput::GUIInput(xml_node<>* node)
	: GUIObject(node)
{
	xml_attribute<>* attr;
	xml_node<>* child;

	mInputText = NULL;
	mAction = NULL;
	mBackground = NULL;
	mCursor = NULL;
	mFont = NULL;
	mRendered = false;
	HasMask = false;
	DrawCursor = false;
	isLocalChange = true;
	HasAllowed = false;
	HasDisabled = false;
	scrollingX = mFontHeight = mFontY = lastX = 0;
	mBackgroundX = mBackgroundY = mBackgroundW = mBackgroundH = MinLen = MaxLen = 0;
	mCursorLocation = -1; // -1 is always the end of the string
	CursorWidth = 3;
	ConvertStrToColor("black", &mBackgroundColor);
	ConvertStrToColor("white", &mCursorColor);

	if (!node)
		return;

	// Load text directly from the node
	mInputText = new GUIText(node);
	// Load action directly from the node
	mAction = new GUIAction(node);

	if (mInputText->Render() < 0)
	{
		delete mInputText;
		mInputText = NULL;
	}

	// Load the background
	child = FindNode(node, "background");
	if (child)
	{
		mBackground = LoadAttrImage(child, "resource");
		mBackgroundColor = LoadAttrColor(child, "color", mBackgroundColor);
	}
	if (mBackground && mBackground->GetResource())
	{
		mBackgroundW = mBackground->GetWidth();
		mBackgroundH = mBackground->GetHeight();
	}

	// Load the cursor color
	child = FindNode(node, "cursor");
	if (child)
	{
		mCursor = LoadAttrImage(child, "resource");
		mCursorColor = LoadAttrColor(child, "color", mCursorColor);
		attr = child->first_attribute("hasfocus");
		if (attr)
		{
			std::string focus = attr->value();
			SetInputFocus(atoi(focus.c_str()));
		}
		CursorWidth = LoadAttrIntScaleX(child, "width", CursorWidth);
	}
	DrawCursor = HasInputFocus;

	// Load the font
	child = FindNode(node, "font");
	if (child)
	{
		mFont = LoadAttrFont(child, "resource");
		mFontHeight = mFont->GetHeight();
	}

	child = FindNode(node, "text");
	if (child)  mText = child->value();
	mLastValue = gui_parse_text(mText);

	child = FindNode(node, "data");
	if (child)
	{
		attr = child->first_attribute("name");
		if (attr)
			mVariable = attr->value();
		attr = child->first_attribute("default");
		if (attr)
			DataManager::SetValue(mVariable, attr->value());
		mMask = LoadAttrString(child, "mask");
		HasMask = !mMask.empty();
		attr = child->first_attribute("maskvariable");
		if (attr)
			mMaskVariable = attr->value();
		else
			mMaskVariable = mVariable;
	}

	// Load input restrictions
	child = FindNode(node, "restrict");
	if (child)
	{
		MinLen = LoadAttrInt(child, "minlen", MinLen);
		MaxLen = LoadAttrInt(child, "maxlen", MaxLen);
		AllowedList = LoadAttrString(child, "allow");
		HasAllowed = !AllowedList.empty();
		DisabledList = LoadAttrString(child, "disable");
		HasDisabled = !DisabledList.empty();
	}

	// Load the placement
	LoadPlacement(FindNode(node, "placement"), &mRenderX, &mRenderY, &mRenderW, &mRenderH);
	SetActionPos(mRenderX, mRenderY, mRenderW, mRenderH);

	if (mInputText && mFontHeight && mFontHeight < (unsigned)mRenderH) {
		mFontY = ((mRenderH - mFontHeight) / 2) + mRenderY;
		mInputText->SetRenderPos(mRenderX, mFontY);
	} else
		mFontY = mRenderY;

	if (mInputText)
		mInputText->SetMaxWidth(0);

	isLocalChange = false;
	HandleTextLocation(0);
}

GUIInput::~GUIInput()
{
	delete mInputText;
	delete mAction;
}

int GUIInput::HandleTextLocation(int x)
{
	int textWidth, cursorWidth;
	string displayValue, insertChar;
	void* fontResource = NULL;

	if (mFont)
		fontResource = mFont->GetResource();

	DataManager::GetValue(mVariable, displayValue);
	if (HasMask) {
		int index, string_size = displayValue.size();
		string maskedValue;
		for (index=0; index<string_size; index++)
			maskedValue += mMask;
		displayValue = maskedValue;
	}
	mRendered = false;
	textWidth = gr_ttf_measureEx(displayValue.c_str(), fontResource);
	if (textWidth <= mRenderW) {
		lastX = x;
		scrollingX = 0;
		return 0;
	}
	if (scrollingX + textWidth < mRenderW) {
		scrollingX = mRenderW - textWidth;
	}

	if (mCursorLocation != 0 && DrawCursor) {
		int cursorWidth = textWidth;
		if (mCursorLocation != -1) {
			string cursorDisplay = displayValue;
			cursorDisplay.resize(mCursorLocation);
			cursorWidth = gr_ttf_measureEx(cursorDisplay.c_str(), fontResource);
		}
		if (cursorWidth + scrollingX > mRenderW || mCursorLocation == -1) {
			scrollingX = mRenderW - cursorWidth;
		} else if (cursorWidth + scrollingX < 0) {
			scrollingX = cursorWidth * -1;
		}
	}

	int deltaX = 0;
	if (x > lastX) {
		// Dragging to right, scrolling left
		deltaX = x - lastX;
		scrollingX += deltaX;
		if (scrollingX > 0)
			scrollingX = 0;
		lastX = x;
	} else if (x < lastX) {
		// Dragging to left, scrolling right
		deltaX = lastX - x;
		scrollingX -= deltaX;
		if (scrollingX + textWidth < mRenderW) {
			scrollingX = mRenderW - textWidth;
		}
		lastX = x;
	}
	return 0;
}

int GUIInput::Render(void)
{
	if (!isConditionTrue())
	{
		mRendered = false;
		return 0;
	}

	void* fontResource = NULL;
	if (mFont)  fontResource = mFont->GetResource();

	// First step, fill background
	gr_color(mBackgroundColor.red, mBackgroundColor.green, mBackgroundColor.blue, 255);
	gr_fill(mRenderX, mRenderY, mRenderW, mRenderH);

	// Next, render the background resource (if it exists)
	if (mBackground && mBackground->GetResource())
	{
		mBackgroundX = mRenderX + ((mRenderW - mBackgroundW) / 2);
		mBackgroundY = mRenderY + ((mRenderH - mBackgroundH) / 2);
		gr_blit(mBackground->GetResource(), 0, 0, mBackgroundW, mBackgroundH, mBackgroundX, mBackgroundY);
	}

	int ret = 0;

	string displayValue;
	DataManager::GetValue(mVariable, displayValue);
	if (HasMask) {
		int index, string_size = displayValue.size();
		string maskedValue;
		for (index=0; index<string_size; index++)
			maskedValue += mMask;
		displayValue = maskedValue;
	}
	int textWidth = gr_ttf_measureEx(displayValue.c_str(), fontResource);
	// Render the text
	if (mInputText) {
		if (mCursorLocation == -1 && textWidth > mRenderW)
			scrollingX = mRenderW - textWidth;
		mInputText->SetRenderPos(mRenderX + scrollingX, mFontY);
		gr_clip(mRenderX, mRenderY, mRenderW, mRenderH);
		ret = mInputText->Render();
		gr_noclip();
	}
	if (ret < 0)
		return ret;

	if (HasInputFocus && DrawCursor) {
		// Render the cursor
		int cursorX;
		if (displayValue.size() == 0) {
			mCursorLocation = -1;
			cursorX = mRenderX;
		} else {
			if (mCursorLocation == 0) {
				// Cursor is at the beginning
				cursorX = mRenderX;
			} else if (mCursorLocation > 0) {
				// Cursor is in the middle
				if (displayValue.size() > (unsigned)mCursorLocation) {
					string cursorDisplay;

					cursorDisplay = displayValue;
					cursorDisplay.resize(mCursorLocation);
					cursorX = gr_ttf_measureEx(cursorDisplay.c_str(), fontResource) + mRenderX;
				} else {
					// Cursor location is after the end of the text  - reset to -1
					mCursorLocation = -1;
					cursorX = gr_ttf_measureEx(displayValue.c_str(), fontResource) + mRenderX;
				}
			} else {
				// Cursor is at the end (-1)
				cursorX = gr_ttf_measureEx(displayValue.c_str(), fontResource) + mRenderX;
			}
		}
		cursorX += scrollingX;
		// Make sure that the cursor doesn't go past the boundaries of the box
		if (cursorX + (int)CursorWidth > mRenderX + mRenderW)
			cursorX = mRenderX + mRenderW - CursorWidth;

		// Set the color for the cursor
		gr_color(mCursorColor.red, mCursorColor.green, mCursorColor.blue, 255);
		gr_fill(cursorX, mFontY, CursorWidth, mFontHeight);
	}

	mRendered = true;
	return ret;
}

int GUIInput::Update(void)
{
	if (!isConditionTrue())	 return (mRendered ? 2 : 0);
	if (!mRendered)			 return 2;

	int ret = 0;

	if (mInputText)		 ret = mInputText->Update();
	if (ret < 0)			return ret;

	return ret;
}

int GUIInput::GetSelection(int x, int y)
{
	if (x < mRenderX || x - mRenderX > mRenderW || y < mRenderY || y - mRenderY > mRenderH) return -1;
	return (x - mRenderX);
}

int GUIInput::NotifyTouch(TOUCH_STATE state, int x, int y)
{
	static int startSelection = -1;
	int textWidth;
	string displayValue, originalValue;
	void* fontResource = NULL;

	if (mFont)  fontResource = mFont->GetResource();

	if (!isConditionTrue())
		return -1;

	if (!HasInputFocus) {
		if (state != TOUCH_RELEASE)
			return 0; // Only change focus if touch releases within the input box
		if (GetSelection(x, y) >= 0) {
			// When changing focus, we don't scroll or change the cursor location
			PageManager::SetKeyBoardFocus(0);
			PageManager::NotifyCharInput(0);
			SetInputFocus(1);
			DrawCursor = true;
			mRendered = false;
		}
	} else {
		switch (state) {
		case TOUCH_HOLD:
		case TOUCH_REPEAT:
			break;
		case TOUCH_START:
			startSelection = GetSelection(x,y);
			lastX = x;
			DrawCursor = false;
			mRendered = false;
			break;

		case TOUCH_DRAG:
			// Check if we dragged out of the selection window
			if (GetSelection(x, y) == -1) {
				lastX = 0;
				break;
			}

			DrawCursor = false;

			// Provide some debounce on initial touches
			if (startSelection != -1 && abs(x - lastX) < 6) {
				break;
			}

			startSelection = -1;
			if (lastX != x)
				HandleTextLocation(x);
			break;

		case TOUCH_RELEASE:
			// We've moved the cursor location
			int relativeX = x - mRenderX;

			mRendered = false;
			DrawCursor = true;
			DataManager::GetValue(mVariable, displayValue);
			if (HasMask) {
				int index, string_size = displayValue.size();
				string maskedValue;
				for (index=0; index<string_size; index++)
					maskedValue += mMask;
				displayValue = maskedValue;
			}
			if (displayValue.size() == 0) {
				mCursorLocation = -1;
				return 0;
			}

			string cursorString;
			int cursorX = 0;
			unsigned index = 0;

			for(index=0; index<displayValue.size(); index++)
			{
				cursorString = displayValue.substr(0, index);
				cursorX = gr_ttf_measureEx(cursorString.c_str(), fontResource) + mRenderX + scrollingX;
				if (cursorX > x) {
					if (index > 0)
						mCursorLocation = index - 1;
					else
						mCursorLocation = index;
					return 0;
				}
			}
			mCursorLocation = -1;
			break;
		}
	}
	return 0;
}

int GUIInput::NotifyVarChange(const std::string& varName, const std::string& value)
{
	GUIObject::NotifyVarChange(varName, value);

	if (varName == mVariable) {
		if (!isLocalChange)
			HandleTextLocation(0);
		else
			isLocalChange = false;
		return 0;
	}
	return 0;
}

int GUIInput::NotifyKey(int key, bool down)
{
	if (!HasInputFocus || !down)
		return 1;

	string variableValue;
	switch (key)
	{
		case KEY_LEFT:
			if (mCursorLocation == 0)
				return 0; // we're already at the beginning
			if (mCursorLocation == -1) {
				DataManager::GetValue(mVariable, variableValue);
				if (variableValue.size() == 0)
					return 0;
				mCursorLocation = variableValue.size() - 1;
			} else {
				mCursorLocation--;
				HandleTextLocation(0);
			}
			mRendered = false;
			return 0;

		case KEY_RIGHT:
			if (mCursorLocation == -1)
				return 0; // we're already at the end
			mCursorLocation++;
			DataManager::GetValue(mVariable, variableValue);
			if (variableValue.size() <= mCursorLocation)
				mCursorLocation = -1;
			HandleTextLocation(0);
			mRendered = false;
			return 0;

		case KEY_HOME:
		case KEY_UP:
			DataManager::GetValue(mVariable, variableValue);
			if (variableValue.size() == 0)
				return 0;
			mCursorLocation = 0;
			mRendered = false;
			HandleTextLocation(0);
			return 0;

		case KEY_END:
		case KEY_DOWN:
			mCursorLocation = -1;
			mRendered = false;
			HandleTextLocation(0);
			return 0;
	}

	return 1;
}

int GUIInput::NotifyCharInput(int key)
{
	string variableValue;

	if (HasInputFocus) {
		if (key == KEYBOARD_BACKSPACE) {
			//Backspace
			DataManager::GetValue(mVariable, variableValue);
			if (variableValue.size() > 0 && mCursorLocation != 0) {
				if (mCursorLocation == -1) {
					variableValue.resize(variableValue.size() - 1);
				} else {
					variableValue.erase(mCursorLocation - 1, 1);
					mCursorLocation--;
				}
				isLocalChange = true;
				DataManager::SetValue(mVariable, variableValue);

				if (HasMask) {
					int index, string_size = variableValue.size();
					string maskedValue;
					for (index=0; index<string_size; index++)
						maskedValue += mMask;
					DataManager::SetValue(mMaskVariable, maskedValue);
				}
				HandleTextLocation(0);
			}
		} else if (key == KEYBOARD_SWIPE_LEFT) {
			// Delete all
			if (mCursorLocation == -1) {
				isLocalChange = true;
				DataManager::SetValue (mVariable, "");
				if (HasMask)
					DataManager::SetValue(mMaskVariable, "");
				mCursorLocation = -1;
			} else {
				DataManager::GetValue(mVariable, variableValue);
				variableValue.erase(0, mCursorLocation);
				isLocalChange = true;
				DataManager::SetValue(mVariable, variableValue);
				if (HasMask) {
					DataManager::GetValue(mMaskVariable, variableValue);
					variableValue.erase(0, mCursorLocation);
					DataManager::SetValue(mMaskVariable, variableValue);
				}
				mCursorLocation = 0;
			}
			scrollingX = 0;
			mRendered = false;
			HandleTextLocation(0);
			return 0;
		} else if (key >= 32) {
			// Regular key
			if (HasAllowed && AllowedList.find((char)key) == string::npos) {
				return 0;
			}
			if (HasDisabled && DisabledList.find((char)key) != string::npos) {
				return 0;
			}
			DataManager::GetValue(mVariable, variableValue);
			if (MaxLen != 0 && variableValue.size() >= MaxLen) {
				return 0;
			}
			if (mCursorLocation == -1) {
				variableValue += key;
			} else {
				variableValue.insert(mCursorLocation, 1, key);
				mCursorLocation++;
			}
			isLocalChange = true;
			DataManager::SetValue(mVariable, variableValue);

			if (HasMask) {
				int index, string_size = variableValue.size();
				string maskedValue;
				for (index=0; index<string_size; index++)
					maskedValue += mMask;
				DataManager::SetValue(mMaskVariable, maskedValue);
			}
			HandleTextLocation(0);
		} else if (key == KEYBOARD_ACTION) {
			// Action
			DataManager::GetValue(mVariable, variableValue);
			if (mAction) {
				unsigned inputLen = variableValue.length();
				if (inputLen < MinLen)
					return 0;
				else if (MaxLen != 0 && inputLen > MaxLen)
					return 0;
				else
					return (mAction ? mAction->NotifyTouch(TOUCH_RELEASE, mRenderX, mRenderY) : 1);
			}
		}
		return 0;
	} else {
		if (key == 0) {
			// Somewhat ugly hack-ish way to tell the box to redraw after losing focus to remove the cursor
			mRendered = false;
			return 1;
		}
	}
	return 1;
}
