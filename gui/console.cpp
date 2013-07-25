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

	for (start = next = buf; *next != '\0'; next++)
	{
		if (*next == '\n')
		{
			*next = '\0';
			next++;

			std::string line = start;
			gConsole.push_back(line);
			start = next;

			// Handle the normal \n\0 case
			if (*next == '\0')
				return;
		}
	}
	std::string line = start;
	gConsole.push_back(line);
	return;
}

extern "C" void gui_print_overwrite(const char *fmt, ...)
{
	char buf[512];		// We're going to limit a single request to 512 bytes

	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, 512, fmt, ap);
	va_end(ap);

	fputs(buf, stdout);

	// Pop the last line, and we can continue
	if (!gConsole.empty())   gConsole.pop_back();

	char *start, *next;
	for (start = next = buf; *next != '\0'; next++)
	{
		if (*next == '\n')
		{
			*next = '\0';
			next++;
			
			std::string line = start;
			gConsole.push_back(line);
			start = next;

			// Handle the normal \n\0 case
			if (*next == '\0')
				return;
		}
	}
	std::string line = start;
	gConsole.push_back(line);
	return;
}

GUIConsole::GUIConsole(xml_node<>* node)
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
	mSlideoutState = hidden;

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
	mLastCount = gConsole.size();
	if (mLastCount == 0)
		return (mSlideout ? RenderSlideout() : 0);

	// Find the start point
	int start;
	int curLine = mCurrentLine; // Thread-safing (Another thread updates this value)
	if (curLine == -1)
	{
		start = mLastCount - mMaxRows;
	}
	else
	{
		if (curLine > (int) mLastCount)
			curLine = (int) mLastCount;
		if ((int) mMaxRows > curLine)
			curLine = (int) mMaxRows;
		start = curLine - mMaxRows;
	}

	unsigned int line;
	for (line = 0; line < mMaxRows; line++)
	{
		if ((start + (int) line) >= 0 && (start + (int) line) < (int) mLastCount)
			gr_textExW(mConsoleX, mStartY + (line * mFontHeight), gConsole[start + line].c_str(), fontResource, mConsoleW + mConsoleX);
	}
	return (mSlideout ? RenderSlideout() : 0);
}

int GUIConsole::Render(void)
{
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
	else if (mLastTouchY >= 0)
	{
		// They're still touching, so re-render
		Render();
		mLastTouchY = -1;
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
	if (mLastCount < mMaxRows)   return 1;

	// We are scrolling!!!
	switch (state)
	{
	case TOUCH_START:
		mLastTouchX = x;
		mLastTouchY = y;
		if ((x - mConsoleX) > ((9 * mConsoleW) / 10))
			mSlideMultiplier = 10;
		else
			mSlideMultiplier = 1;
		break;

	case TOUCH_DRAG:
		// This handles tapping
		if (x == mLastTouchX && y == mLastTouchY)   break;
		mLastTouchX = -1;

		if (y > mLastTouchY + 5)
		{
			mLastTouchY = y;
			if (mCurrentLine == -1)
				mCurrentLine = mLastCount - 1;
			else if (mCurrentLine > mSlideMultiplier)
				mCurrentLine -= mSlideMultiplier;
			else
				mCurrentLine = mMaxRows;

			if (mCurrentLine < (int) mMaxRows)
				mCurrentLine = mMaxRows;
		}
		else if (y < mLastTouchY - 5)
		{
			mLastTouchY = y;
			if (mCurrentLine >= 0)
			{
				mCurrentLine += mSlideMultiplier;
				if (mCurrentLine >= (int) mLastCount)
					mCurrentLine = -1;
			}
		}
		break;

	case TOUCH_RELEASE:
		// On a tap, we jump to the tail
		if (mLastTouchX >= 0)
			mCurrentLine = -1;

		mLastTouchY = -1;
	case TOUCH_REPEAT:
	case TOUCH_HOLD:
		break;
	}
	return 0;
}
