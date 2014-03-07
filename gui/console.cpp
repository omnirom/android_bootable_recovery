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

extern "C" void gui_print(const char *fmt, ...)
{
	char buf[512];		// We're going to limit a single request to 512 bytes

	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, 512, fmt, ap);
	va_end(ap);

	fputs(buf, stdout);

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

			start = ++next;
		}
		else
			++next;
	}

	// The text after last \n (or whole string if there is no \n)
	if(*start)
		gConsole.push_back(start);
	return;
}

GUIConsole::GUIConsole(xml_node<>* node) : GUIObject(node)
{
	xml_attribute<>* attr;
	xml_node<>* child;

	mFont = NULL;
	mCurrentLine = -1;
	memset(&mForegroundColor, 255, sizeof(COLOR));
	memset(&mBackgroundColor, 0, sizeof(COLOR));
	mBackgroundColor.alpha = 255;
	memset(&mScrollColor, 0x08, sizeof(COLOR));
	mScrollColor.alpha = 255;
	mLastCount = 0;
	mSlideout = 0;
	RenderCount = 0;
	mSlideoutState = hidden;
	mRender = true;

	mRenderX = 0; mRenderY = 0; mRenderW = gr_fb_width(); mRenderH = gr_fb_height();

	if (!node)
	{
		mSlideoutX = 0; mSlideoutY = 0; mSlideoutW = 0; mSlideoutH = 0;
		mConsoleX = 0;  mConsoleY = 0;  mConsoleW = gr_fb_width();  mConsoleH = gr_fb_height();
	}
	else
	{
		child = node->first_node("font");
		if (child)
		{
			attr = child->first_attribute("resource");
			if (attr)
				mFont = PageManager::FindResource(attr->value());
		}

		child = node->first_node("color");
		if (child)
		{
			attr = child->first_attribute("foreground");
			if (attr)
			{
				std::string color = attr->value();
				ConvertStrToColor(color, &mForegroundColor);
			}
			attr = child->first_attribute("background");
			if (attr)
			{
				std::string color = attr->value();
				ConvertStrToColor(color, &mBackgroundColor);
			}
			attr = child->first_attribute("scroll");
			if (attr)
			{
				std::string color = attr->value();
				ConvertStrToColor(color, &mScrollColor);
			}
		}

		// Load the placement
		LoadPlacement(node->first_node("placement"), &mConsoleX, &mConsoleY, &mConsoleW, &mConsoleH);

		child = node->first_node("slideout");
		if (child)
		{
			mSlideout = 1;
			LoadPlacement(child, &mSlideoutX, &mSlideoutY);

			attr = child->first_attribute("resource");
			if (attr)   mSlideoutImage = PageManager::FindResource(attr->value());

			if (mSlideoutImage && mSlideoutImage->GetResource())
			{
				mSlideoutW = gr_get_width(mSlideoutImage->GetResource());
				mSlideoutH = gr_get_height(mSlideoutImage->GetResource());
			}
		}
	}

	gr_getFontDetails(mFont, &mFontHeight, NULL);
	SetActionPos(mRenderX, mRenderY, mRenderW, mRenderH);
	SetRenderPos(mConsoleX, mConsoleY);
	return;
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
	void* fontResource = NULL;
	if (mFont)
		fontResource = mFont->GetResource();

	// We fill the background
	gr_color(mBackgroundColor.red, mBackgroundColor.green, mBackgroundColor.blue, 255);
	gr_fill(mConsoleX, mConsoleY, mConsoleW, mConsoleH);

	gr_color(mScrollColor.red, mScrollColor.green, mScrollColor.blue, mScrollColor.alpha);
	gr_fill(mConsoleX + (mConsoleW * 9 / 10), mConsoleY, (mConsoleW / 10), mConsoleH);

	// Render the lines
	gr_color(mForegroundColor.red, mForegroundColor.green, mForegroundColor.blue, mForegroundColor.alpha);

	// Don't try to continue to render without data
	int prevCount = mLastCount;
	mLastCount = gConsole.size();
	mRender = false;
	if (mLastCount == 0)
		return (mSlideout ? RenderSlideout() : 0);

	// Due to word wrap, figure out what / how the newly added text needs to be added to the render vector that is word wrapped
	// Note, that multiple consoles on different GUI pages may be different widths or use different fonts, so the word wrapping
	// may different in different console windows
	for (int i = prevCount; i < mLastCount; i++) {
		string curr_line = gConsole[i];
		int line_char_width;
		for(;;) {
			line_char_width = gr_maxExW(curr_line.c_str(), fontResource, mConsoleW);
			if (line_char_width < curr_line.size()) {
				rConsole.push_back(curr_line.substr(0, line_char_width));
				curr_line = curr_line.substr(line_char_width);
			} else {
				rConsole.push_back(curr_line);
				break;
			}
		}
	}
	RenderCount = rConsole.size();

	// Find the start point
	int start;
	int curLine = mCurrentLine; // Thread-safing (Another thread updates this value)
	if (curLine == -1)
	{
		start = RenderCount - mMaxRows;
	}
	else
	{
		if (curLine > (int) RenderCount)
			curLine = (int) RenderCount;
		if ((int) mMaxRows > curLine)
			curLine = (int) mMaxRows;
		start = curLine - mMaxRows;
	}

	unsigned int line;
	for (line = 0; line < mMaxRows; line++)
	{
		if ((start + (int) line) >= 0 && (start + (int) line) < (int) RenderCount)
			gr_textExW(mConsoleX, mStartY + (line * mFontHeight), rConsole[start + line].c_str(), fontResource, mConsoleW + mConsoleX);
	}
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
	if(!isConditionTrue())
		return 0;

	if (mSlideout && mSlideoutState != visible)
	{
		if (mSlideoutState == hidden)
			return 0;

		if (mSlideoutState == request_hide)
			mSlideoutState = hidden;

		if (mSlideoutState == request_show)
			mSlideoutState = visible;

		// Any time we activate the slider, we reset the position
		mCurrentLine = -1;
		return 2;
	}

	if (mCurrentLine == -1 && mLastCount != gConsole.size())
	{
		// We can use Render, and return for just a flip
		Render();
		return 2;
	}
	else if (mRender)
	{
		// They're still touching, so re-render
		Render();
		return 2;
	}
	return 0;
}

int GUIConsole::SetRenderPos(int x, int y, int w, int h)
{
	// Adjust the stub position accordingly
	mSlideoutX += (x - mConsoleX);
	mSlideoutY += (y - mConsoleY);

	mConsoleX = x;
	mConsoleY = y;
	if (w || h)
	{
		mConsoleW = w;
		mConsoleH = h;
	}

	// Calculate the max rows
	mMaxRows = mConsoleH / mFontHeight;

	// Adjust so we always fit to bottom
	mStartY = mConsoleY + (mConsoleH % mFontHeight);
	return 0;
}

// IsInRegion - Checks if the request is handled by this object
//  Return 0 if this object handles the request, 1 if not
int GUIConsole::IsInRegion(int x, int y)
{
	if (mSlideout)
	{
		// Check if they tapped the slideout button
		if (x >= mSlideoutX && x <= mSlideoutX + mSlideoutW && y >= mSlideoutY && y < mSlideoutY + mSlideoutH)
			return 1;

		// If we're only rendering the slideout, bail now
		if (mSlideoutState == hidden)
			return 0;
	}

	return (x < mConsoleX || x > mConsoleX + mConsoleW || y < mConsoleY || y > mConsoleY + mConsoleH) ? 0 : 1;
}

// NotifyTouch - Notify of a touch event
//  Return 0 on success, >0 to ignore remainder of touch, and <0 on error
int GUIConsole::NotifyTouch(TOUCH_STATE state, int x, int y)
{
	if(!isConditionTrue())
		return -1;

	if (mSlideout && mSlideoutState == hidden)
	{
		if (state == TOUCH_START)
		{
			mSlideoutState = request_show;
			return 1;
		}
	}
	else if (mSlideout && mSlideoutState == visible)
	{
		// Are we sliding it back in?
		if (state == TOUCH_START && x > mSlideoutX && x < (mSlideoutX + mSlideoutW) && y > mSlideoutY && y < (mSlideoutY + mSlideoutH))
		{
			mSlideoutState = request_hide;
			return 1;
		}
	}

	// If we don't have enough lines to scroll, throw this away.
	if (RenderCount < mMaxRows)   return 1;

	// We are scrolling!!!
	switch (state)
	{
	case TOUCH_START:
		mLastTouchX = x;
		mLastTouchY = y;
		break;

	case TOUCH_DRAG:
		if (x < mConsoleX || x > mConsoleX + mConsoleW || y < mConsoleY || y > mConsoleY + mConsoleH)
			break; // touch is outside of the console area -- do nothing
		if (y > mLastTouchY + mFontHeight) {
			while (y > mLastTouchY + mFontHeight) {
				if (mCurrentLine == -1)
					mCurrentLine = RenderCount - 1;
				else if (mCurrentLine > mMaxRows)
					mCurrentLine--;
				mLastTouchY += mFontHeight;
			}
			mRender = true;
		} else if (y < mLastTouchY - mFontHeight) {
			while (y < mLastTouchY - mFontHeight) {
				if (mCurrentLine >= 0)
					mCurrentLine++;
				mLastTouchY -= mFontHeight;
			}
			if (mCurrentLine >= (int) RenderCount)
				mCurrentLine = -1;
			mRender = true;
		}
		break;

	case TOUCH_RELEASE:
		mLastTouchY = -1;
	case TOUCH_REPEAT:
	case TOUCH_HOLD:
		break;
	}
	return 0;
}
