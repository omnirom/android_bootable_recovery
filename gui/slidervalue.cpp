// slidervalue.cpp - GUISliderValue object

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

GUISliderValue::GUISliderValue(xml_node<>* node) : Conditional(node)
{
	xml_attribute<>* attr;
	xml_node<>* child;

	mMin = 0;
	mMax = 100;
	mValue = 0;
	mLineH = 2;
	mLinePadding = 10;
	mSliderW = 5;
	mSliderH = 30;
	mLineX = 0;
	mLineY = 0;
	mValueStr = NULL;
	mAction = NULL;
	mShowCurr = true;
	mShowRange = false;
	mChangeOnDrag = false;
	mRendered = false;

	mLabel = NULL;
	ConvertStrToColor("white", &mTextColor);
	ConvertStrToColor("white", &mLineColor);
	ConvertStrToColor("blue", &mSliderColor);

	if (!node)
	{
		LOGERR("GUISliderValue created without XML node\n");
		return;
	}

	mLabel = new GUIText(node);
	if(mLabel->Render() < 0)
	{
		delete mLabel;
		mLabel = NULL;
	}

	mAction = new GUIAction(node);

	child = node->first_node("font");
	if (child)
	{
		attr = child->first_attribute("resource");
		if (attr)
			mFont = PageManager::FindResource(attr->value());

		attr = child->first_attribute("color");
		if (attr)
		{
			std::string color = attr->value();
			ConvertStrToColor(color, &mTextColor);
		}
	}

	// Load the placement
	LoadPlacement(node->first_node("placement"), &mRenderX, &mRenderY, &mRenderW);

	child = node->first_node("colors");
	if (child)
	{
		attr = child->first_attribute("line");
		if (attr)
			ConvertStrToColor(attr->value(), &mLineColor);

		attr = child->first_attribute("slider");
		if (attr)
			ConvertStrToColor(attr->value(), &mSliderColor);
	}

	child = node->first_node("data");
	if (child)
	{
		attr = child->first_attribute("variable");
		if (attr)
			mVariable = attr->value();

		attr = child->first_attribute("min");
		if (attr)
		{
			mMinStr = gui_parse_text(attr->value());
			mMin = atoi(mMinStr.c_str());
		}

		attr = child->first_attribute("max");
		if (attr)
		{
			mMaxStr = gui_parse_text(attr->value());
			mMax = atoi(mMaxStr.c_str());
		}

		if (mMin > mMax)
			mMin = mMax;

		attr = child->first_attribute("default");
		if (attr)
		{
			string parsevalue = gui_parse_text(attr->value());
			int def = atoi(parsevalue.c_str());

			if (def < mMin)      def = mMin;
			else if (def > mMax) def = mMax;
			DataManager::SetValue(mVariable, def);
		}

		attr = child->first_attribute("showrange");
		if (attr)
			mShowRange = atoi(attr->value());

		attr = child->first_attribute("showcurr");
		if (attr)
			mShowCurr = atoi(attr->value());

		attr = child->first_attribute("changeondrag");
		if (attr)
			mChangeOnDrag = atoi(attr->value());
	}

	child = node->first_node("dimensions");
	if (child)
	{
		attr = child->first_attribute("lineh");
		if (attr)
		{
			string parsevalue = gui_parse_text(attr->value());
			mLineH = atoi(parsevalue.c_str());
		}

		attr = child->first_attribute("linepadding");
		if (attr)
		{
			string parsevalue = gui_parse_text(attr->value());
			mPadding = atoi(parsevalue.c_str());
		}

		attr = child->first_attribute("sliderw");
		if (attr)
		{
			string parsevalue = gui_parse_text(attr->value());
			mSliderW = atoi(parsevalue.c_str());
		}

		attr = child->first_attribute("sliderh");
		if (attr)
		{
			string parsevalue = gui_parse_text(attr->value());
			mSliderH = atoi(parsevalue.c_str());
		}
	}

	gr_getFontDetails(mFont ? mFont->GetResource() : NULL, (unsigned*) &mFontHeight, NULL);

	if(mShowCurr)
	{
		int maxLen = std::max(strlen(mMinStr.c_str()), strlen(mMaxStr.c_str()));
		mValueStr = new char[maxLen+1];
	}

	loadValue(true);

	mLinePadding = mPadding;
	if (mShowRange)
	{
		int textW = std::max(measureText(mMaxStr), measureText(mMinStr));
		mLinePadding += textW;
	}

	SetRenderPos(mRenderX, mRenderY, mRenderW);
}

GUISliderValue::~GUISliderValue()
{
	delete mLabel;
	delete mAction;
	delete[] mValueStr;
}

void GUISliderValue::loadValue(bool force)
{
	if(!mVariable.empty())
	{
		int value = DataManager::GetIntValue(mVariable);
		if(mValue == value && !force)
			return;

		mValue = value;
	}

	mValue = std::max(mValue, mMin);
	mValue = std::min(mValue, mMax);
	mValuePct = pctFromValue(mValue);
	mRendered = false;
}

int GUISliderValue::SetRenderPos(int x, int y, int w, int h)
{
	mRenderX = x;
	mRenderY = y;
	if (w || h)
	{
		mRenderW = w;
		mRenderH = h;
	}

	mRenderH = mSliderH;
	if(mShowCurr)
		mRenderH += mFontHeight;

	if (mLabel)
	{
		int lw, lh;
		mLabel->GetCurrentBounds(lw, lh);
		int textX = mRenderX + (mRenderW/2 - lw/2);

		mLabel->SetRenderPos(textX, mRenderY);

		y += lh;
		mRenderH += lh;
	}

	mSliderY = y;
	mLineY = (y + mSliderH/2) - (mLineH/2);

	mLineX = mRenderX + mLinePadding;

	mActionX = mRenderX;
	mActionY = mRenderY;
	mActionW = mRenderW;
	mActionH = mRenderH;
	lineW = mRenderW - (mLinePadding * 2);

	return 0;
}

int GUISliderValue::measureText(const std::string& str)
{
	void* fontResource = NULL;
	if (mFont)  fontResource = mFont->GetResource();

	return gr_measureEx(str.c_str(), fontResource);
}

int GUISliderValue::Render(void)
{
	if (!isConditionTrue())
	{
		mRendered = false;
		return 0;
	}

	if(mLabel)
	{
		int w, h;
		mLabel->GetCurrentBounds(w, h);
		if (w != mLabelW) {
			mLabelW = w;
			int textX = mRenderX + (mRenderW/2 - mLabelW/2);
			mLabel->SetRenderPos(textX, mRenderY);
		}
		int res = mLabel->Render();
		if(res < 0)
			return res;
	}

	// line
	gr_color(mLineColor.red, mLineColor.green, mLineColor.blue, mLineColor.alpha);
	gr_fill(mLineX, mLineY, lineW, mLineH);

	// slider
	uint32_t sliderX = (mValuePct*lineW)/100 + mLineX;
	sliderX -= mSliderW/2;

	gr_color(mSliderColor.red, mSliderColor.green, mSliderColor.blue, mSliderColor.alpha);
	gr_fill(sliderX, mSliderY, mSliderW, mSliderH);

	void *fontResource = NULL;
	if(mFont) fontResource = mFont->GetResource();
	gr_color(mTextColor.red, mTextColor.green, mTextColor.blue, mTextColor.alpha);
	if(mShowRange)
	{
		int rangeY = (mLineY - mLineH/2) - mFontHeight/2;
		gr_textEx(mRenderX + mPadding/2, rangeY, mMinStr.c_str(), fontResource);
		gr_textEx(mLineX + lineW + mPadding/2, rangeY, mMaxStr.c_str(), fontResource);
	}

	if(mValueStr && mShowCurr)
	{
		sprintf(mValueStr, "%d", mValue);
		int textW = measureText(mValueStr);
		gr_textEx(mRenderX + (mRenderW/2 - textW/2), mSliderY+mSliderH, mValueStr, fontResource);
	}

	mRendered = true;
	return 0;
}

int GUISliderValue::Update(void)
{
	if (!isConditionTrue()) return mRendered ? 2 : 0;
	if (!mRendered)         return 2;

	if(mLabel)
		return mLabel->Update();
	return 0;
}

int GUISliderValue::valueFromPct(float pct)
{
	int range = abs(mMax - mMin);
	return mMin + (pct * range) / 100;
}

float GUISliderValue::pctFromValue(int value)
{
	return float((value - mMin) * 100) / abs(mMax - mMin);
}

int GUISliderValue::NotifyTouch(TOUCH_STATE state, int x, int y)
{
	if (!isConditionTrue())     return -1;

	static bool dragging = false;
	switch (state)
	{
	case TOUCH_START:
		if (x >= mRenderX && x <= mRenderX + mRenderW &&
			y >= mRenderY && y <= mRenderY + mRenderH)
		{
			dragging = true;
		}
		// no break
	case TOUCH_DRAG:
	{
		if (!dragging)  return 0;

		x = std::max(mLineX, x);
		x = std::min(mLineX + lineW, x);

		mValuePct = float(((x - mLineX) * 100) / lineW);
		int newVal = valueFromPct(mValuePct);
		if (newVal != mValue) {
			mRendered = false;
			mValue = newVal;
			if (mChangeOnDrag) {
				if (!mVariable.empty())
					DataManager::SetValue(mVariable, mValue);
				if (mAction)
					mAction->doActions();
			}
		}
		break;
	}
	case TOUCH_RELEASE:
	{
		if (!dragging)  return 0;
		dragging = false;

		if (!mVariable.empty())
			DataManager::SetValue(mVariable, mValue);
		if (mAction)
			mAction->doActions();
		break;
	}
	case TOUCH_REPEAT:
	case TOUCH_HOLD:
		break;
	}
	return 0;
}

int GUISliderValue::NotifyVarChange(std::string varName, std::string value)
{
	if (mLabel)
		mLabel->NotifyVarChange(varName, value);
	if (varName == mVariable) {
		int newVal = atoi(value.c_str());
		if(newVal != mValue) {
			mValue = newVal;
			mValuePct = pctFromValue(mValue);
			mRendered = false;
		}
	}
	return 0;
}

void GUISliderValue::SetPageFocus(int inFocus)
{
	if (inFocus)
		loadValue();
}
