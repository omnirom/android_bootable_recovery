// console.cpp - GUITextBox object

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
	scrollToEnd = false;
	mIsStatic = 1;

	allowSelection = false;	// console doesn't support list item selections

	if (!node)
	{
		mRenderX = 0; mRenderY = 0; mRenderW = gr_fb_width(); mRenderH = gr_fb_height();
	}
	else
	{
		child = FindNode(node, "color");
		if (child)
		{
			mFontColor = LoadAttrColor(child, "foreground", mFontColor);
			mBackgroundColor = LoadAttrColor(child, "background", mBackgroundColor);
			//mScrollColor = LoadAttrColor(child, "scroll", mScrollColor);
		}
		xml_node<>* child = FindNode(node, "text");
		if (child) {
			while (child) {
				string txt = child->value();
				mText.push_back(txt);
				string lookup = gui_parse_text(txt);
				if (lookup != txt)
					mIsStatic = 0;
				mLastValue.push_back(lookup);
				child = child->next_sibling("text");
			}
		}

		mFontHeight = mFont->GetHeight();
	}
}

bool GUITextBox::AddLines()
{
	if (mLastCount == mLastValue.size())
		return false; // nothing to add

	size_t prevCount = mLastCount;
	mLastCount = mLastValue.size();

	for (size_t i = prevCount; i < mLastCount; i++) {
		string curr_line = mLastValue[i];
		for(;;) {
			size_t line_char_width = gr_maxExW(curr_line.c_str(), mFont->GetResource(), mRenderW);
			if (line_char_width < curr_line.size()) {
				string left = curr_line.substr(0, line_char_width);
				size_t space_pos = left.rfind(" ");
				if (space_pos == string::npos) {
					space_pos = line_char_width;
				} else {
					if (curr_line.size() > space_pos) {
						space_pos = space_pos + 1;
					}
				}
				rText.push_back(curr_line.substr(0, space_pos));
				curr_line = curr_line.substr(space_pos);
			} else {
				rText.push_back(curr_line);
				break;
			}
		}
	}
	return true;
}

int GUITextBox::RenderConsole(void)
{
	AddLines();
	GUIScrollList::Render();

	// if last line is fully visible, keep tracking the last line when new lines are added
	int bottom_offset = GetDisplayRemainder() - actualItemHeight;
	bool isAtBottom = firstDisplayedItem == GetItemCount() - GetDisplayItemCount() - (bottom_offset != 0) && y_offset == bottom_offset;
	if (isAtBottom)
		scrollToEnd = true;
#if 0
	// debug - show if we are tracking the last line
	if (scrollToEnd) {
		gr_color(0,255,0,255);
		gr_fill(mRenderX+mRenderW-5, mRenderY+mRenderH-5, 5, 5);
	} else {
		gr_color(255,0,0,255);
		gr_fill(mRenderX+mRenderW-5, mRenderY+mRenderH-5, 5, 5);
	}
#endif
	return 0;
}

int GUITextBox::Render(void)
{
	if(!isConditionTrue())
		return 0;

	return RenderConsole();
}

int GUITextBox::Update(void)
{
	if (AddLines()) {
		// someone added new text
		// at least the scrollbar must be updated, even if the new lines are currently not visible
		mUpdate = 1;
	}

	if (scrollToEnd) {
		// keep the last line in view
		SetVisibleListLocation(rText.size() - 1);
	}

	GUIScrollList::Update();

	if (mUpdate) {
		mUpdate = 0;
		if (Render() == 0)
			return 2;
	}
	return 0;
}

// IsInRegion - Checks if the request is handled by this object
//  Return 1 if this object handles the request, 0 if not
int GUITextBox::IsInRegion(int x, int y)
{
	return GUIScrollList::IsInRegion(x, y);
}

// NotifyTouch - Notify of a touch event
//  Return 0 on success, >0 to ignore remainder of touch, and <0 on error
int GUITextBox::NotifyTouch(TOUCH_STATE state, int x, int y)
{
	if(!isConditionTrue())
		return -1;

	scrollToEnd = false;
	return GUIScrollList::NotifyTouch(state, x, y);
}

size_t GUITextBox::GetItemCount()
{
	return rText.size();
}

void GUITextBox::RenderItem(size_t itemindex, int yPos, bool selected)
{
	// Set the color for the font
	gr_color(mFontColor.red, mFontColor.green, mFontColor.blue, mFontColor.alpha);

	// render text
	const char* text = rText[itemindex].c_str();
	gr_textEx(mRenderX, yPos, text, mFont->GetResource());
}

void GUITextBox::NotifySelect(size_t item_selected)
{
	// do nothing - console ignores selections
}
