// image.cpp - GUIImage object

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

GUIImage::GUIImage(xml_node<>* node) : GUIObject(node)
{
	mImage = NULL;
	mHighlightImage = NULL;
	isHighlighted = false;

	if (!node)
		return;

	mImage = LoadAttrImage(FindNode(node, "image"), "resource");
	mHighlightImage = LoadAttrImage(FindNode(node, "image"), "highlightresource");

	// Load the placement
	LoadPlacement(FindNode(node, "placement"), &mRenderX, &mRenderY, NULL, NULL, &mPlacement);

	if (mImage && mImage->GetResource())
	{
		mRenderW = mImage->GetWidth();
		mRenderH = mImage->GetHeight();

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

int GUIImage::Render(void)
{
	if (!isConditionTrue())
		return 0;

	if (isHighlighted && mHighlightImage && mHighlightImage->GetResource()) {
		gr_blit(mHighlightImage->GetResource(), 0, 0, mRenderW, mRenderH, mRenderX, mRenderY);
		return 0;
	}
	else if (!mImage || !mImage->GetResource())
		return -1;

	gr_blit(mImage->GetResource(), 0, 0, mRenderW, mRenderH, mRenderX, mRenderY);
	return 0;
}

int GUIImage::SetRenderPos(int x, int y, int w, int h)
{
	if (w || h)
		return -1;

	mRenderX = x;
	mRenderY = y;
	return 0;
}

