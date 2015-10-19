// animation.cpp - GUIAnimation object

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


GUIAnimation::GUIAnimation(xml_node<>* node) : GUIObject(node)
{
	xml_node<>* child;

	mAnimation = NULL;
	mFrame = 1;
	mFPS = 1;
	mLoop = -1;
	mRender = 1;
	mUpdateCount = 0;

	if (!node)  return;

	mAnimation = LoadAttrAnimation(FindNode(node, "resource"), "name");

	// Load the placement
	LoadPlacement(FindNode(node, "placement"), &mRenderX, &mRenderY, NULL, NULL, &mPlacement);

	child = FindNode(node, "speed");
	if (child)
	{
		mFPS = LoadAttrInt(child, "fps", mFPS);
		mRender = LoadAttrInt(child, "render", mRender);
	}
	if (mFPS > 30)  mFPS = 30;

	child = FindNode(node, "loop");
	if (child)
	{
		xml_attribute<>* attr = child->first_attribute("frame");
		if (attr)
			mLoop = atoi(attr->value()) - 1;
		mFrame = LoadAttrInt(child, "start", mFrame);
	}

	// Fetch the render sizes
	if (mAnimation && mAnimation->GetResource())
	{
		mRenderW = mAnimation->GetWidth();
		mRenderH = mAnimation->GetHeight();

		// Adjust for placement
		if (mPlacement != TOP_LEFT && mPlacement != BOTTOM_LEFT)
		{
			if (mPlacement == CENTER)
				mRenderX -= (mRenderW / 2);
			else
				mRenderX -= mRenderW;
		}
		if (mPlacement != TOP_LEFT && mPlacement != TOP_RIGHT)
		{
			if (mPlacement == CENTER)
				mRenderY -= (mRenderH / 2);
			else
				mRenderY -= mRenderH;
		}
		SetPlacement(TOP_LEFT);
	}
}

int GUIAnimation::Render(void)
{
	if(!isConditionTrue())
		return 0;

	if (!mAnimation || !mAnimation->GetResource(mFrame))	return -1;

	gr_blit(mAnimation->GetResource(mFrame), 0, 0, mRenderW, mRenderH, mRenderX, mRenderY);
	return 0;
}

int GUIAnimation::Update(void)
{
	if(!isConditionTrue())
		return 0;

	if (!mAnimation)		return -1;

	// Handle the "end-of-animation" state
	if (mLoop == -2)		return 0;

	// Determine if we need the next frame yet...
	if (++mUpdateCount > 30 / mFPS)
	{
		mUpdateCount = 0;
		if (++mFrame >= mAnimation->GetResourceCount())
		{
			if (mLoop < 0)
			{
				mFrame = mAnimation->GetResourceCount() - 1;
				mLoop = -2;
			}
			else
				mFrame = mLoop;
		}
		if (mRender == 2)	return 2;
		return (Render() == 0 ? 1 : -1);
	}
	return 0;
}

