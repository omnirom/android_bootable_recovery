/*
        Copyright 2015 bigbiff/Dees_Troy/_that TeamWin
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

// textbox.cpp - GUITextBox object

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
#include "../minuitwrp/minui.h"
}

#include "rapidxml.hpp"
#include "objects.hpp"

GUITextBox::GUITextBox(xml_node<>* node) : GUIScrollList(node)
{
	xml_node<>* child;

	mLastCount = 0;
	mIsStatic = true;

	allowSelection = false;	// textbox doesn't support list item selections

	child = FindNode(node, "color");
	if (child)
	{
		mFontColor = LoadAttrColor(child, "foreground", mFontColor);
		mBackgroundColor = LoadAttrColor(child, "background", mBackgroundColor);
		//mScrollColor = LoadAttrColor(child, "scroll", mScrollColor);
	}
	child = FindNode(node, "text");
	if (child) {
		while (child) {
			string txt = child->value();
			mText.push_back(txt);
			string lookup = gui_parse_text(txt);
			if (lookup != txt)
				mIsStatic = false;
			mLastValue.push_back(lookup);
			child = child->next_sibling("text");
		}
	}
}

int GUITextBox::Update(void)
{
	if (AddLines(&mLastValue, NULL, &mLastCount, &rText, NULL)) {
		// someone added new text
		// at least the scrollbar must be updated, even if the new lines are currently not visible
		mUpdate = 1;
	}

	GUIScrollList::Update();

	if (mUpdate) {
		mUpdate = 0;
		if (Render() == 0)
			return 2;
	}
	return 0;
}

size_t GUITextBox::GetItemCount()
{
	return rText.size();
}

void GUITextBox::RenderItem(size_t itemindex, int yPos, bool selected __unused)
{
	// Set the color for the font
	gr_color(mFontColor.red, mFontColor.green, mFontColor.blue, mFontColor.alpha);

	// render text
	const char* text = rText[itemindex].c_str();
	gr_textEx(mRenderX, yPos, text, mFont->GetResource());
}

void GUITextBox::NotifySelect(size_t item_selected __unused)
{
	// do nothing - textbox ignores selections
}

int GUITextBox::NotifyVarChange(const std::string& varName, const std::string& value)
{
	GUIScrollList::NotifyVarChange(varName, value);

	if(!isConditionTrue() || mIsStatic)
		return 0;

	// Check to see if the variable exists in mText
	for (size_t i = 0; i < mText.size(); i++) {
		string lookup = gui_parse_text(mText.at(i));
		if (lookup != mText.at(i)) {
			mLastValue.at(i) = lookup;
			mUpdate = 1;
		}
	}
	return 0;
}
