/*
	Copyright 2017 TeamWin
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

// checkbox.cpp - GUICheckbox object

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

GUICheckbox::GUICheckbox(xml_node<>* node)
	: GUIObject(node)
{
	xml_attribute<>* attr;
	xml_node<>* child;

	mChecked = NULL;
	mUnchecked = NULL;
	mLabel = NULL;
	mRendered = false;

	mLastState = 0;

	if (!node)
		return;

	// The label can be loaded directly
	mLabel = new GUIText(node);

	// Read the check states
	child = FindNode(node, "image");
	if (child)
	{
		mChecked = LoadAttrImage(child, "checked");
		mUnchecked = LoadAttrImage(child, "unchecked");
	}

	// Get the variable data
	child = FindNode(node, "data");
	if (child)
	{
		attr = child->first_attribute("variable");
		if (attr)
			mVarName = attr->value();
		attr = child->first_attribute("default");
		if (attr) {
			DataManager::SetValue(mVarName, attr->value());
		} else {
			int val;
			if (DataManager::GetValue(mVarName, val) != 0)
				DataManager::SetValue(mVarName, 0); // Prevents check boxes from having to be tapped twice the first time
		}
	}

	mCheckW = mCheckH = 0;
	if (mChecked && mChecked->GetResource()) {
		mCheckW = mChecked->GetWidth();
		mCheckH = mChecked->GetHeight();
	} else if (mUnchecked && mUnchecked->GetResource()) {
		mCheckW = mUnchecked->GetWidth();
		mCheckH = mUnchecked->GetHeight();
	}

	int x, y, w, h;
	mLabel->GetRenderPos(x, y, w, h);
	SetRenderPos(x, y, 0, 0);
}

GUICheckbox::~GUICheckbox()
{
}

int GUICheckbox::Render(void)
{
	if (!isConditionTrue())
	{
		mRendered = false;
		return 0;
	}

	int ret = 0;
	int lastState = 0;
	DataManager::GetValue(mVarName, lastState);

	if (lastState)
	{
		if (mChecked && mChecked->GetResource())
			gr_blit(mChecked->GetResource(), 0, 0, mCheckW, mCheckH, mRenderX, mRenderY);
	}
	else
	{
		if (mUnchecked && mUnchecked->GetResource())
			gr_blit(mUnchecked->GetResource(), 0, 0, mCheckW, mCheckH, mRenderX, mRenderY);
	}
	if (mLabel)
		ret = mLabel->Render();
	mLastState = lastState;
	mRendered = true;
	return ret;
}

int GUICheckbox::Update(void)
{
	if (!isConditionTrue())	return (mRendered ? 2 : 0);
	if (!mRendered)			return 2;

	int lastState = 0;
	DataManager::GetValue(mVarName, lastState);

	if (lastState != mLastState)
		return 2;
	return 0;
}

int GUICheckbox::SetRenderPos(int x, int y, int w, int h)
{
	mRenderX = x;
	mRenderY = y;

	if (w || h)
		return -1;

	int textW, textH;
	mLabel->GetCurrentBounds(textW, textH);

	w = textW + mCheckW + 5;
	mRenderW = w;
	mRenderH = mCheckH;

	mTextX = mRenderX + mCheckW + 5;
	mTextY = mRenderY + (mCheckH / 2);

	mLabel->SetRenderPos(mTextX, mTextY, 0, 0);
	mLabel->SetPlacement(TEXT_ONLY_RIGHT);
	mLabel->SetMaxWidth(gr_fb_width() - mTextX);
	SetActionPos(mRenderX, mRenderY, mRenderW, mRenderH);
	return 0;
}

int GUICheckbox::NotifyTouch(TOUCH_STATE state, int x __unused, int y __unused)
{
	if (!isConditionTrue())
		return -1;

	if (state == TOUCH_RELEASE)
	{
		int lastState;
		DataManager::GetValue(mVarName, lastState);
		lastState = (lastState == 0) ? 1 : 0;
		DataManager::SetValue(mVarName, lastState);

#ifndef TW_NO_HAPTICS
		DataManager::Vibrate("tw_button_vibrate");
#endif

	}
	return 0;
}

