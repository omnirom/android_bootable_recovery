// slider.cpp - GUISlider object
// Pulled & ported from https://raw.github.com/agrabren/RecoverWin/master/gui/slider.cpp

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

GUISlider::GUISlider(xml_node<>* node)
{
	xml_attribute<>* attr;
	xml_node<>* child;

	sAction = NULL;
	sSlider = NULL;
	sSliderUsed = NULL;
	sTouch = NULL;
	sTouchW = 20;

	if (!node)
	{
		LOGERR("GUISlider created without XML node\n");
		return;
	}

	child = node->first_node("resource");
	if (child)
	{
		attr = child->first_attribute("base");
		if (attr)
			sSlider = PageManager::FindResource(attr->value());

		attr = child->first_attribute("used");
		if (attr)
			sSliderUsed = PageManager::FindResource(attr->value());

		attr = child->first_attribute("touch");
		if (attr)
			sTouch = PageManager::FindResource(attr->value());
	}

	// Load the placement
	LoadPlacement(node->first_node("placement"), &mRenderX, &mRenderY);

	if (sSlider && sSlider->GetResource())
	{
		mRenderW = gr_get_width(sSlider->GetResource());
		mRenderH = gr_get_height(sSlider->GetResource());
	}
	if (sTouch && sTouch->GetResource())
	{
		sTouchW = gr_get_width(sTouch->GetResource());  // Width of the "touch image" that follows the touch (arrow)
		sTouchH = gr_get_height(sTouch->GetResource()); // Height of the "touch image" that follows the touch (arrow)
	}

	//LOGINFO("mRenderW: %i mTouchW: %i\n", mRenderW, mTouchW);
	mActionX = mRenderX;
	mActionY = mRenderY;
	mActionW = mRenderW;
	mActionH = mRenderH;

	sAction = new GUIAction(node);

	sCurTouchX = mRenderX;
	sUpdate = 1;
}

GUISlider::~GUISlider()
{
	delete sAction;
}

int GUISlider::Render(void)
{
	if (!sSlider || !sSlider->GetResource())
		return -1;

	// Draw the slider
	gr_blit(sSlider->GetResource(), 0, 0, mRenderW, mRenderH, mRenderX, mRenderY);

	// Draw the used
	if (sSliderUsed && sSliderUsed->GetResource() && sCurTouchX > mRenderX)
		gr_blit(sSliderUsed->GetResource(), 0, 0, sCurTouchX - mRenderX, mRenderH, mRenderX, mRenderY);

	// Draw the touch icon
	if (sTouch && sTouch->GetResource())
		gr_blit(sTouch->GetResource(), 0, 0, sTouchW, sTouchH, sCurTouchX, (mRenderY + ((mRenderH - sTouchH) / 2)));

	sUpdate = 0;
	return 0;
}

int GUISlider::Update(void)
{
	if (sUpdate)
		return 2;
	return 0;
}

int GUISlider::NotifyTouch(TOUCH_STATE state, int x, int y)
{
	static bool dragging = false;

	switch (state)
	{
	case TOUCH_START:
		if (x >= mRenderX && x <= mRenderX + sTouchW && 
			y >= mRenderY && y <= mRenderY + mRenderH)
		{
			sCurTouchX = x - (sTouchW / 2);
			if (sCurTouchX < mRenderX)
				sCurTouchX = mRenderX;
			dragging = true;
		}
		break;

	case TOUCH_DRAG:
		if (!dragging)
			return 0;
		if (y < mRenderY - sTouchH || y > mRenderY + (sTouchH * 2))
		{
			sCurTouchX = mRenderX;
			dragging = false;
			sUpdate = 1;
			break;
		}
		sCurTouchX = x - (sTouchW / 2);
		if (sCurTouchX < mRenderX)
			sCurTouchX = mRenderX;
		if (sCurTouchX > mRenderX + mRenderW - sTouchW)
			sCurTouchX = mRenderX + mRenderW - sTouchW;
		sUpdate = 1;
		break;

	case TOUCH_RELEASE:
		if (!dragging)
			return 0;

		if (sCurTouchX >= mRenderX + mRenderW - sTouchW)
			sAction->doActions();

		sCurTouchX = mRenderX;
		dragging = false;
		sUpdate = 1;
	case TOUCH_REPEAT:
	case TOUCH_HOLD:
		break;
	}
	return 0;
}
