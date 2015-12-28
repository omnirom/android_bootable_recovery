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
}
#include "../minuitwrp/minui.h"

#include "rapidxml.hpp"
#include "objects.hpp"

GUISliderValue::GUISliderValue(xml_node<>* node) : GUIObject(node)
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
	mBackgroundImage = NULL;
	mHandleImage = NULL;
	mHandleHoverImage = NULL;
	mDragging = false;

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

	child = FindNode(node, "font");
	if (child)
	{
		mFont = LoadAttrFont(child, "resource");
		mTextColor = LoadAttrColor(child, "color", mTextColor);
	}

	// Load the placement
	LoadPlacement(FindNode(node, "placement"), &mRenderX, &mRenderY, &mRenderW);

	child = FindNode(node, "colors");
	if (child)
	{
		mLineColor = LoadAttrColor(child, "line");
		mSliderColor = LoadAttrColor(child, "slider");
	}

	child = FindNode(node, "resource");
	if (child)
	{
		mBackgroundImage = LoadAttrImage(child, "background");
		mHandleImage = LoadAttrImage(child, "handle");
		mHandleHoverImage = LoadAttrImage(child, "handlehover");
	}

	child = FindNode(node, "data");
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

			if (def < mMin)
				def = mMin;
			else if (def > mMax)
				def = mMax;
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

	child = FindNode(node, "dimensions");
	if (child)
	{
		mLineH = LoadAttrIntScaleY(child, "lineh", mLineH);
		mLinePadding = LoadAttrIntScaleX(child, "linepadding", mLinePadding);
		mSliderW = LoadAttrIntScaleX(child, "sliderw", mSliderW);
		mSliderH = LoadAttrIntScaleY(child, "sliderh", mSliderH);
	}

	mFontHeight = mFont->GetHeight();

	if(mShowCurr)
	{
		int maxLen = std::max(strlen(mMinStr.c_str()), strlen(mMaxStr.c_str()));
		mValueStr = new char[maxLen+1];
	}

	loadValue(true);

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

	mActionX = mRenderX;
	mActionY = mRenderY;
	mActionW = mRenderW;
	mActionH = mRenderH;

	if(mBackgroundImage && mBackgroundImage->GetResource())
	{
		mLineW = mBackgroundImage->GetWidth();
		mLineH = mBackgroundImage->GetHeight();
	}
	else
		mLineW = mRenderW - (mLinePadding * 2);

	mLineY = y + (mSliderH/2 - mLineH/2);
	mLineX = mRenderX + (mRenderW/2 - mLineW/2);

	return 0;
}

int GUISliderValue::measureText(const std::string& str)
{
	void* fontResource = NULL;
	if (mFont)  fontResource = mFont->GetResource();

	return gr_ttf_measureEx(str.c_str(), fontResource);
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
	if(mBackgroundImage && mBackgroundImage->GetResource())
	{
		gr_blit(mBackgroundImage->GetResource(), 0, 0, mLineW, mLineH, mLineX, mLineY);
	}
	else
	{
		gr_color(mLineColor.red, mLineColor.green, mLineColor.blue, mLineColor.alpha);
		gr_fill(mLineX, mLineY, mLineW, mLineH);
	}

	// slider
	uint32_t sliderX = mLineX + (mValuePct*(mLineW - mSliderW))/100;

	if(mHandleImage && mHandleImage->GetResource())
	{
		gr_surface s = mHandleImage->GetResource();
		if(mDragging && mHandleHoverImage && mHandleHoverImage->GetResource())
			s = mHandleHoverImage->GetResource();
		gr_blit(s, 0, 0, mSliderW, mSliderH, sliderX, mLineY + (mLineH/2 - mSliderH/2));
	}
	else
	{
		gr_color(mSliderColor.red, mSliderColor.green, mSliderColor.blue, mSliderColor.alpha);
		gr_fill(sliderX, mSliderY, mSliderW, mSliderH);
	}

	void *fontResource = NULL;
	if(mFont) fontResource = mFont->GetResource();
	gr_color(mTextColor.red, mTextColor.green, mTextColor.blue, mTextColor.alpha);
	if(mShowRange)
	{
		int rangeY = (mLineY - mLineH/2) - mFontHeight/2;
		gr_textEx_scaleW(mRenderX + mPadding/2, rangeY, mMinStr.c_str(), fontResource, mRenderW, TOP_LEFT, 0);
		gr_textEx_scaleW(mLineX + mLineW + mPadding/2, rangeY, mMaxStr.c_str(), fontResource, mRenderW, TOP_LEFT, 0);
	}

	if(mValueStr && mShowCurr)
	{
		sprintf(mValueStr, "%d", mValue);
		int textW = measureText(mValueStr);
		gr_textEx_scaleW(mRenderX + (mRenderW/2 - textW/2), mSliderY+mSliderH, mValueStr, fontResource, mRenderW, TOP_LEFT, 0);
	}

	mRendered = true;
	return 0;
}

int GUISliderValue::Update(void)
{
	if (!isConditionTrue())
		return mRendered ? 2 : 0;
	if (!mRendered)
		return 2;

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
	if (!isConditionTrue())
		return -1;

	switch (state)
	{
	case TOUCH_START:
		if (x >= mLineX && x <= mLineX + mLineW &&
			y >= mRenderY && y <= mRenderY + mRenderH)
		{
			mDragging = true;
		}
		// no break
	case TOUCH_DRAG:
	{
		if (!mDragging)  return 0;

		x = std::max(mLineX + mSliderW/2, x);
		x = std::min(mLineX + mLineW - mSliderW/2, x);

		mValuePct = float(((x - (mLineX + mSliderW/2)) * 100) / (mLineW - mSliderW));
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
		if (!mDragging)  return 0;
		mDragging = false;

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

int GUISliderValue::NotifyVarChange(const std::string& varName, const std::string& value)
{
	GUIObject::NotifyVarChange(varName, value);

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
