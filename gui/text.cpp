// text.cpp - GUIText object

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

GUIText::GUIText(xml_node<>* node)
	: GUIObject(node)
{
	mFont = NULL;
	mIsStatic = 1;
	mVarChanged = 0;
	mFontHeight = 0;
	maxWidth = 0;
	charSkip = 0;
	isHighlighted = false;

	if (!node)
		return;

	// Load colors
	mColor = LoadAttrColor(node, "color", COLOR(0,0,0,255));
	mHighlightColor = LoadAttrColor(node, "highlightcolor", mColor);

	// Load the font, and possibly override the color
	xml_node<>* child = node->first_node("font");
	if (child)
	{
		mFont = LoadAttrFont(child, "resource");
		mColor = LoadAttrColor(child, "color", mColor);
		mHighlightColor = LoadAttrColor(child, "highlightcolor", mColor);
	}

	// Load the placement
	LoadPlacement(node->first_node("placement"), &mRenderX, &mRenderY, &mRenderW, &mRenderH, &mPlacement);

	child = node->first_node("text");
	if (child)  mText = child->value();

	// Simple way to check for static state
	mLastValue = parseText();
	if (mLastValue != mText)   mIsStatic = 0;

	mFontHeight = mFont->GetHeight();
}

int GUIText::Render(void)
{
	if (!isConditionTrue())
		return 0;

	void* fontResource = NULL;
	if (mFont)
		fontResource = mFont->GetResource();

	mLastValue = parseText();
	string displayValue = mLastValue;

	if (charSkip)
		displayValue.erase(0, charSkip);

	mVarChanged = 0;

	int x = mRenderX, y = mRenderY;
	int width = gr_measureEx(displayValue.c_str(), fontResource);

	if (mPlacement != TOP_LEFT && mPlacement != BOTTOM_LEFT)
	{
		if (mPlacement == CENTER || mPlacement == CENTER_X_ONLY)
			x -= (width / 2);
		else
			x -= width;
	}
	if (mPlacement != TOP_LEFT && mPlacement != TOP_RIGHT)
	{
		if (mPlacement == CENTER)
			y -= (mFontHeight / 2);
		else if (mPlacement == BOTTOM_LEFT || mPlacement == BOTTOM_RIGHT)
			y -= mFontHeight;
	}

	if (isHighlighted)
		gr_color(mHighlightColor.red, mHighlightColor.green, mHighlightColor.blue, mHighlightColor.alpha);
	else
		gr_color(mColor.red, mColor.green, mColor.blue, mColor.alpha);

	if (maxWidth)
		gr_textExW(x, y, displayValue.c_str(), fontResource, maxWidth + x);
	else
		gr_textEx(x, y, displayValue.c_str(), fontResource);
	return 0;
}

int GUIText::Update(void)
{
	if (!isConditionTrue())
		return 0;

	static int updateCounter = 3;

	// This hack just makes sure we update at least once a minute for things like clock and battery
	if (updateCounter)  updateCounter--;
	else
	{
		mVarChanged = 1;
		updateCounter = 3;
	}

	if (mIsStatic || !mVarChanged)
		return 0;

	std::string newValue = parseText();
	if (mLastValue == newValue)
		return 0;
	else
		mLastValue = newValue;
	return 2;
}

int GUIText::GetCurrentBounds(int& w, int& h)
{
	void* fontResource = NULL;

	if (mFont)
		fontResource = mFont->GetResource();

	h = mFontHeight;
	mLastValue = parseText();
	w = gr_measureEx(mLastValue.c_str(), fontResource);
	return 0;
}

std::string GUIText::parseText(void)
{
	static int counter = 0;
	std::string str = mText;
	size_t pos = 0;
	size_t next = 0, end = 0;

	while (1)
	{
		next = str.find('%', pos);
		if (next == std::string::npos) return str;
		end = str.find('%', next + 1);
		if (end == std::string::npos) return str;

		// We have a block of data
		std::string var = str.substr(next + 1, (end - next) - 1);
		str.erase(next, (end - next) + 1);

		if (next + 1 == end)
		{
			str.insert(next, 1, '%');
		}
		else
		{
			std::string value;
			if (DataManager::GetValue(var, value) == 0)
				str.insert(next, value);
		}

		pos = next + 1;
	}
}

int GUIText::NotifyVarChange(const std::string& varName, const std::string& value)
{
	GUIObject::NotifyVarChange(varName, value);

	mVarChanged = 1;
	return 0;
}

int GUIText::SetMaxWidth(unsigned width)
{
	maxWidth = width;
	mVarChanged = 1;
	return 0;
}

int GUIText::SkipCharCount(unsigned skip)
{
	charSkip = skip;
	mVarChanged = 1;
	return 0;
}
