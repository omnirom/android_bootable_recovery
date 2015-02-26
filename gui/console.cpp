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


static std::vector<std::string> gConsole;
static std::vector<std::string> gConsoleColor;
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
	if (ors_file) {
		fprintf(ors_file, "%s\n", buf);
		fflush(ors_file);
	}
}

extern "C" void gui_print(const char *fmt, ...)
{
	char buf[512];		// We're going to limit a single request to 512 bytes

	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, 512, fmt, ap);
	va_end(ap);

	fputs(buf, stdout);

	__gui_print("normal", buf);
	return;
}

extern "C" void gui_print_color(const char *color, const char *fmt, ...)
{
	char buf[512];		// We're going to limit a single request to 512 bytes

	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, 512, fmt, ap);
	va_end(ap);

	fputs(buf, stdout);

	__gui_print(color, buf);
	return;
}

extern "C" void gui_set_FILE(FILE* f)
{
	ors_file = f;
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
			LoadPlacement(child, &mSlideoutX, &mSlideoutY);

			mSlideoutImage = LoadAttrImage(child, "resource");

			if (mSlideoutImage && mSlideoutImage->GetResource())
			{
				mSlideoutW = mSlideoutImage->GetWidth();
				mSlideoutH = mSlideoutImage->GetHeight();
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

bool GUIConsole::AddLines()
{
	if (mLastCount == gConsole.size())
		return false; // nothing to add

	size_t prevCount = mLastCount;
	mLastCount = gConsole.size();

	// Due to word wrap, figure out what / how the newly added text needs to be added to the render vector that is word wrapped
	// Note, that multiple consoles on different GUI pages may be different widths or use different fonts, so the word wrapping
	// may different in different console windows
	for (size_t i = prevCount; i < mLastCount; i++) {
		string curr_line = gConsole[i];
		string curr_color = gConsoleColor[i];
		for(;;) {
			size_t line_char_width = gr_maxExW(curr_line.c_str(), mFont->GetResource(), mRenderW);
			if (line_char_width < curr_line.size()) {
				rConsole.push_back(curr_line.substr(0, line_char_width));
				rConsoleColor.push_back(curr_color);
				curr_line = curr_line.substr(line_char_width);
			} else {
				rConsole.push_back(curr_line);
				rConsoleColor.push_back(curr_color);
				break;
			}
		}
	}
	return true;
}

int GUIConsole::RenderConsole(void)
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

	if (AddLines()) {
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

void GUIConsole::RenderItem(size_t itemindex, int yPos, bool selected)
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
	gr_textEx(mRenderX, yPos, text, mFont->GetResource());
}

void GUIConsole::NotifySelect(size_t item_selected)
{
	// do nothing - console ignores selections
}
