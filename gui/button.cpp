/*
	Copyright 2012 bigbiff/Dees_Troy TeamWin
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
#include "../data.hpp"

#include <string>

extern "C" {
#include "../twcommon.h"
}
#include "../minuitwrp/minui.h"

#include "rapidxml.hpp"
#include "objects.hpp"

GUIButton::GUIButton(xml_node<>* node)
	: GUIObject(node)
{
	xml_attribute<>* attr;
	xml_node<>* child;

	mButtonImg = NULL;
	mButtonIcon = NULL;
	mButtonLabel = NULL;
	mAction = NULL;
	mRendered = false;
	hasHighlightColor = false;
	renderHighlight = false;
	hasFill = false;

	if (!node)  return;

	// These can be loaded directly from the node
	mButtonLabel = new GUIText(node);
	mAction = new GUIAction(node);

	mButtonImg = new GUIImage(node);
	if (mButtonImg->Render() < 0)
	{
		delete mButtonImg;
		mButtonImg = NULL;
	}
	if (mButtonLabel->Render() < 0)
	{
		delete mButtonLabel;
		mButtonLabel = NULL;
	}
	// Load fill if it exists
	mFillColor = LoadAttrColor(FindNode(node, "fill"), "color", &hasFill);
	if (!hasFill && mButtonImg == NULL) {
		LOGERR("No image resource or fill specified for button.\n");
	}

	// The icon is a special case
	mButtonIcon = LoadAttrImage(FindNode(node, "icon"), "resource");

	mHighlightColor = LoadAttrColor(FindNode(node, "highlight"), "color", &hasHighlightColor);

	int x, y, w, h;
	TextPlacement = TOP_LEFT;
	if (mButtonImg) {
		mButtonImg->GetRenderPos(x, y, w, h);
	} else if (hasFill) {
		LoadPlacement(FindNode(node, "placement"), &x, &y, &w, &h, &TextPlacement);
	}
	SetRenderPos(x, y, w, h);
	if (mButtonLabel) {
		TextPlacement = (Placement)LoadAttrInt(FindNode(node, "placement"), "textplacement", TOP_LEFT);
		if (TextPlacement != TEXT_ONLY_RIGHT) {
			mButtonLabel->scaleWidth = 1;
			mButtonLabel->SetMaxWidth(w);
			mButtonLabel->SetPlacement(CENTER);
			mTextX = ((mRenderW / 2) + mRenderX);
			mTextY = mRenderY + (mRenderH / 2);
			mButtonLabel->SetRenderPos(mTextX, mTextY);
		} else {
			mTextX = mRenderW + mRenderX + 5;
			mButtonLabel->GetCurrentBounds(mTextW, mTextH);
			mRenderW += mTextW + 5;
			mTextY = mRenderY + (mRenderH / 2) - (mTextH / 2);
			mButtonLabel->SetRenderPos(mTextX, mTextY);
			if (mAction)
				mAction->SetActionPos(mRenderX, mRenderY, mRenderW, mRenderH);
			SetActionPos(mRenderX, mRenderY, mRenderW, mRenderH);
		}
	}
}

GUIButton::~GUIButton()
{
	delete mButtonImg;
	delete mButtonLabel;
	delete mAction;
}

int GUIButton::Render(void)
{
	if (!isConditionTrue())
	{
		mRendered = false;
		return 0;
	}

	int ret = 0;

	if (mButtonImg)	 ret = mButtonImg->Render();
	if (ret < 0)		return ret;
	if (hasFill) {
		gr_color(mFillColor.red, mFillColor.green, mFillColor.blue, mFillColor.alpha);
		gr_fill(mRenderX, mRenderY, mRenderW, mRenderH);
	}
	if (mButtonIcon && mButtonIcon->GetResource())
		gr_blit(mButtonIcon->GetResource(), 0, 0, mIconW, mIconH, mIconX, mIconY);
	if (mButtonLabel) {
		int w, h;
		mButtonLabel->GetCurrentBounds(w, h);
		if (w != mTextW) {
			mTextW = w;
		}
		ret = mButtonLabel->Render();
		if (ret < 0)		return ret;
	}
	if (renderHighlight && hasHighlightColor) {
		gr_color(mHighlightColor.red, mHighlightColor.green, mHighlightColor.blue, mHighlightColor.alpha);
		gr_fill(mRenderX, mRenderY, mRenderW, mRenderH);
	}
	mRendered = true;
	return ret;
}

int GUIButton::Update(void)
{
	if (!isConditionTrue())	return (mRendered ? 2 : 0);
	if (!mRendered)			return 2;

	int ret = 0, ret2 = 0;

	if (mButtonImg)			ret = mButtonImg->Update();
	if (ret < 0)			return ret;

	if (ret == 0)
	{
		if (mButtonLabel) {
			ret2 = mButtonLabel->Update();
			if (ret2 < 0)	return ret2;
			if (ret2 > ret)	ret = ret2;
		}
	}
	else if (ret == 1)
	{
		// The button re-rendered, so everyone else is a render
		if (mButtonIcon && mButtonIcon->GetResource())
			gr_blit(mButtonIcon->GetResource(), 0, 0, mIconW, mIconH, mIconX, mIconY);
		if (mButtonLabel)   ret = mButtonLabel->Render();
		if (ret < 0)		return ret;
		ret = 1;
	}
	else
	{
		// Aparently, the button needs a background update
		ret = 2;
	}
	return ret;
}

int GUIButton::SetRenderPos(int x, int y, int w, int h)
{
	mRenderX = x;
	mRenderY = y;
	if (w || h)
	{
		mRenderW = w;
		mRenderH = h;
	}

	mIconW = mButtonIcon->GetWidth();
	mIconH = mButtonIcon->GetHeight();

	mTextH = 0;
	mTextW = 0;
	mIconX = mRenderX + ((mRenderW - mIconW) / 2);
	if (mButtonLabel)   mButtonLabel->GetCurrentBounds(mTextW, mTextH);
	if (mTextW && TextPlacement == TEXT_ONLY_RIGHT)
	{
		mRenderW += mTextW + 5;
	}

	if (mIconH == 0 || mTextH == 0 || mIconH + mTextH > mRenderH)
	{
		mIconY = mRenderY + (mRenderH / 2) - (mIconH / 2);
	}
	else
	{
		int divisor = mRenderH - (mIconH + mTextH);
		mIconY = mRenderY + (divisor / 3);
	}

	if (mButtonLabel)   mButtonLabel->SetRenderPos(mTextX, mTextY);
	if (mAction)		mAction->SetActionPos(mRenderX, mRenderY, mRenderW, mRenderH);
	SetActionPos(mRenderX, mRenderY, mRenderW, mRenderH);
	return 0;
}

int GUIButton::NotifyTouch(TOUCH_STATE state, int x, int y)
{
	static int last_state = 0;

	if (!isConditionTrue())	 return -1;
	if (x < mRenderX || x - mRenderX > mRenderW || y < mRenderY || y - mRenderY > mRenderH || state == TOUCH_RELEASE) {
		if (last_state == 1) {
			last_state = 0;
			if (mButtonLabel != NULL)
				mButtonLabel->isHighlighted = false;
			if (mButtonImg != NULL)
				mButtonImg->isHighlighted = false;
			renderHighlight = false;
			mRendered = false;
		}
	} else {
		if (last_state == 0) {
			last_state = 1;
			DataManager::Vibrate("tw_button_vibrate");
			if (mButtonLabel != NULL)
				mButtonLabel->isHighlighted = true;
			if (mButtonImg != NULL)
				mButtonImg->isHighlighted = true;
			renderHighlight = true;
			mRendered = false;
		}
	}
	if (x < mRenderX || x - mRenderX > mRenderW || y < mRenderY || y - mRenderY > mRenderH)
		return 0;
	return (mAction ? mAction->NotifyTouch(state, x, y) : 1);
}
