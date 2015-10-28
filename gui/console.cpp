/*
	Copyright 2015 bigbiff/Dees_Troy TeamWin
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

// console.cpp - GUIConsole object

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
#include "gui.hpp"

#define GUI_CONSOLE_BUFFER_SIZE 512

size_t last_translated_line = 0;
std::vector<translate_later_struct> translate_later;

std::vector<std::string> gConsole;
std::vector<std::string> gConsoleColor;
static FILE* ors_file;

extern "C" void __gui_print(const char *color, char *buf)
{
	char *start, *next;

	if (buf[0] == '\n' && strlen(buf) < 2) {
		// This prevents the double lines bug seen in the console during zip installs
		return;
	}

	for (start = next = buf; *next != '\0';)
	{
		if (*next == '\n')
		{
			*next = '\0';
			gConsole.push_back(start);
			gConsoleColor.push_back(color);

			start = ++next;
		}
		else
			++next;
	}

	// The text after last \n (or whole string if there is no \n)
	if(*start) {
		gConsole.push_back(start);
		gConsoleColor.push_back(color);
	}
}

extern "C" void gui_print(const char *fmt, ...)
{
	char buf[GUI_CONSOLE_BUFFER_SIZE];		// We're going to limit a single request to 512 bytes

	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, GUI_CONSOLE_BUFFER_SIZE, fmt, ap);
	va_end(ap);

	fputs(buf, stdout);
	if (ors_file) {
		fprintf(ors_file, "%s", buf);
		fflush(ors_file);
	}

	struct translate_later_struct trans;
	trans.text = buf;
	trans.color = "normal";
	trans.inline_format = false;
	translate_later.push_back(trans);
	return;
}

extern "C" void gui_print_color(const char *color, const char *fmt, ...)
{
	char buf[GUI_CONSOLE_BUFFER_SIZE];		// We're going to limit a single request to 512 bytes

	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, GUI_CONSOLE_BUFFER_SIZE, fmt, ap);
	va_end(ap);

	fputs(buf, stdout);
	if (ors_file) {
		fprintf(ors_file, "%s", buf);
		fflush(ors_file);
	}

	struct translate_later_struct trans;
	trans.text = buf;
	trans.color = color;
	trans.inline_format = false;
	translate_later.push_back(trans);
	return;
}

void gui_translate_va(const char *resource_name, const char* default_value, const char *fmt, ...)
{
	char buf[GUI_CONSOLE_BUFFER_SIZE];		// We're going to limit a single request to 512 bytes
	va_list ap;
	struct translate_later_struct trans;

	if (!resource_name)
		return;

	if (fmt) {
		va_start(ap, fmt);
		vsnprintf(buf, GUI_CONSOLE_BUFFER_SIZE, fmt, ap);
		va_end(ap);

		trans.format = buf;
	}

	trans.resource_name = resource_name;
	if (default_value)
		trans.default_value = default_value;
	trans.color = "normal";
	trans.inline_format = false;
	translate_later.push_back(trans);

	std::string output = default_value + trans.format + "\n";
	fputs(output.c_str(), stdout);
	if (ors_file) {
		fprintf(ors_file, "%s", output.c_str());
		fflush(ors_file);
	}
	return;
}

void gui_translate_color(const char *color, const char *resource_name, const char* default_value, const char *fmt, ...)
{
	char buf[GUI_CONSOLE_BUFFER_SIZE];		// We're going to limit a single request to 512 bytes
	va_list ap;
	struct translate_later_struct trans;

	if (!resource_name)
		return;

	if (fmt) {
		va_start(ap, fmt);
		vsnprintf(buf, GUI_CONSOLE_BUFFER_SIZE, fmt, ap);
		va_end(ap);

		trans.format = buf;
	}

	trans.resource_name = resource_name;
	if (default_value)
		trans.default_value = default_value;
	trans.color = color;
	trans.inline_format = false;
	translate_later.push_back(trans);

	std::string output = default_value + trans.format + "\n";
	fputs(output.c_str(), stdout);
	if (ors_file) {
		fprintf(ors_file, "%s", output.c_str());
		fflush(ors_file);
	}
	return;
}

void gui_translate(const char *resource_name, const char* default_value, const int val)
{
	gui_translate("normal", resource_name, default_value, val);
}

void gui_translate(const char *resource_name, const char* default_value, const char* val)
{
	gui_translate("normal", resource_name, default_value, val);
}

void gui_translate(const char *color, const char *resource_name, const char* default_value, const int val)
{
	char buf[GUI_CONSOLE_BUFFER_SIZE];
	sprintf(buf, "%i", val);
	gui_translate(color, resource_name, default_value, buf);
}

void gui_translate(const char *color, const char *resource_name, const char* default_value, const char* val)
{
	char buf[GUI_CONSOLE_BUFFER_SIZE];		// We're going to limit a single request to 512 bytes
	struct translate_later_struct trans;

	if (!resource_name)
		return;

	trans.format = val;

	trans.resource_name = resource_name;
	if (default_value)
		trans.default_value = default_value;
	trans.color = color;
	trans.inline_format = true;
	translate_later.push_back(trans);

	memset(buf, 0, sizeof(char) * GUI_CONSOLE_BUFFER_SIZE);
	sprintf(buf, default_value, trans.format.c_str());
	std::string output = buf;
	output += "\n";
	fputs(output.c_str(), stdout);
	if (ors_file) {
		fprintf(ors_file, "%s", output.c_str());
		fflush(ors_file);
	}
	return;
}

extern "C" void gui_set_FILE(FILE* f)
{
	ors_file = f;
}

void GUIConsole::Translate_Later() {
	size_t trans_size = translate_later.size();
	if (trans_size <= last_translated_line)
		return;

	for (size_t i = last_translated_line; i < trans_size; i++) {
		if (translate_later[i].text.empty()) {
			if (translate_later[i].default_value.empty())
				translate_later[i].text = PageManager::GetResources()->FindString(translate_later[i].resource_name);
			else
				translate_later[i].text = PageManager::GetResources()->FindString(translate_later[i].resource_name, translate_later[i].default_value);
		}
		char buf[GUI_CONSOLE_BUFFER_SIZE];
		if (!translate_later[i].format.empty()) {
			if (translate_later[i].inline_format) {
				snprintf(buf, sizeof(char) * GUI_CONSOLE_BUFFER_SIZE - 1, translate_later[i].text.c_str(), translate_later[i].format.c_str());
				translate_later[i].text = buf;
				memset(buf, 0, sizeof(char) * GUI_CONSOLE_BUFFER_SIZE);
			} else {
				translate_later[i].text += translate_later[i].format;
			}
		}
		strncpy(buf, translate_later[i].text.c_str(), sizeof(char) * GUI_CONSOLE_BUFFER_SIZE - 1);
		__gui_print(translate_later[i].color.c_str(), buf);
	}
	last_translated_line = trans_size;
}

GUIConsole::GUIConsole(xml_node<>* node) : GUIScrollList(node)
{
	xml_node<>* child;

	mLastCount = 0;
	scrollToEnd = true;
	mSlideoutX = mSlideoutY = mSlideoutW = mSlideoutH = 0;
	mSlideout = 0;
	mSlideoutState = visible;

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

		child = FindNode(node, "slideout");
		if (child)
		{
			mSlideout = 1;
			mSlideoutState = hidden;
			LoadPlacement(child, &mSlideoutX, &mSlideoutY, &mSlideoutW, &mSlideoutH, &mPlacement);

			mSlideoutImage = LoadAttrImage(child, "resource");

			if (mSlideoutImage && mSlideoutImage->GetResource())
			{
				mSlideoutW = mSlideoutImage->GetWidth();
				mSlideoutH = mSlideoutImage->GetHeight();
				if (mPlacement == CENTER || mPlacement == CENTER_X_ONLY) {
					mSlideoutX = mSlideoutX - (mSlideoutW / 2);
					if (mPlacement == CENTER) {
						mSlideoutY = mSlideoutY - (mSlideoutH / 2);
					}
				}
			}
		}
	}
}

int GUIConsole::RenderSlideout(void)
{
	if (!mSlideoutImage || !mSlideoutImage->GetResource())
		return -1;

	gr_blit(mSlideoutImage->GetResource(), 0, 0, mSlideoutW, mSlideoutH, mSlideoutX, mSlideoutY);
	return 0;
}

int GUIConsole::RenderConsole(void)
{
	Translate_Later();
	AddLines(&gConsole, &gConsoleColor, &mLastCount, &rConsole, &rConsoleColor);
	GUIScrollList::Render();

	// if last line is fully visible, keep tracking the last line when new lines are added
	int bottom_offset = GetDisplayRemainder() - actualItemHeight;
	bool isAtBottom = firstDisplayedItem == (int)GetItemCount() - GetDisplayItemCount() - (bottom_offset != 0) && y_offset == bottom_offset;
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
	return (mSlideout ? RenderSlideout() : 0);
}

int GUIConsole::Render(void)
{
	if(!isConditionTrue())
		return 0;

	if (mSlideout && mSlideoutState == hidden)
		return RenderSlideout();

	return RenderConsole();
}

int GUIConsole::Update(void)
{
	if (mSlideout && mSlideoutState != visible)
	{
		if (mSlideoutState == hidden)
			return 0;

		if (mSlideoutState == request_hide)
			mSlideoutState = hidden;

		if (mSlideoutState == request_show)
			mSlideoutState = visible;

		// Any time we activate the console, we reset the position
		SetVisibleListLocation(rConsole.size() - 1);
		mUpdate = 1;
		scrollToEnd = true;
	}

	if (AddLines(&gConsole, &gConsoleColor, &mLastCount, &rConsole, &rConsoleColor)) {
		// someone added new text
		// at least the scrollbar must be updated, even if the new lines are currently not visible
		mUpdate = 1;
	}

	if (scrollToEnd) {
		// keep the last line in view
		SetVisibleListLocation(rConsole.size() - 1);
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
int GUIConsole::IsInRegion(int x, int y)
{
	if (mSlideout) {
		// Check if they tapped the slideout button
		if (x >= mSlideoutX && x < mSlideoutX + mSlideoutW && y >= mSlideoutY && y < mSlideoutY + mSlideoutH)
			return 1;

		// If we're only rendering the slideout, bail now
		if (mSlideoutState == hidden)
			return 0;
	}

	return GUIScrollList::IsInRegion(x, y);
}

// NotifyTouch - Notify of a touch event
//  Return 0 on success, >0 to ignore remainder of touch, and <0 on error
int GUIConsole::NotifyTouch(TOUCH_STATE state, int x, int y)
{
	if(!isConditionTrue())
		return -1;

	if (mSlideout && x >= mSlideoutX && x < mSlideoutX + mSlideoutW && y >= mSlideoutY && y < mSlideoutY + mSlideoutH) {
		if (state == TOUCH_START) {
			if (mSlideoutState == hidden)
				mSlideoutState = request_show;
			else if (mSlideoutState == visible)
				mSlideoutState = request_hide;
		}
		return 1;
	}
	scrollToEnd = false;
	return GUIScrollList::NotifyTouch(state, x, y);
}

size_t GUIConsole::GetItemCount()
{
	return rConsole.size();
}

void GUIConsole::RenderItem(size_t itemindex, int yPos, bool selected __unused)
{
	// Set the color for the font
	if (rConsoleColor[itemindex] == "normal") {
		gr_color(mFontColor.red, mFontColor.green, mFontColor.blue, mFontColor.alpha);
	} else {
		COLOR FontColor;
		std::string color = rConsoleColor[itemindex];
		ConvertStrToColor(color, &FontColor);
		FontColor.alpha = 255;
		gr_color(FontColor.red, FontColor.green, FontColor.blue, FontColor.alpha);
	}

	// render text
	const char* text = rConsole[itemindex].c_str();
	gr_textEx_scaleW(mRenderX, yPos, text, mFont->GetResource(), mRenderW, TOP_LEFT, 0);
}

void GUIConsole::NotifySelect(size_t item_selected __unused)
{
	// do nothing - console ignores selections
}
