#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
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

GUIPatternPassword::GUIPatternPassword(xml_node<>* node)
	: GUIObject(node)
{
	xml_attribute<>* attr;
	xml_node<>* child;

	ResetActiveDots();
	mTrackingTouch = false;
	mNeedRender = true;

	ConvertStrToColor("blue", &mDotColor);
	ConvertStrToColor("white", &mActiveDotColor);
	ConvertStrToColor("blue", &mLineColor);

	mDotImage = mActiveDotImage = NULL;
	mDotCircle = mActiveDotCircle = NULL;
	mDotRadius = 50;
	mLineWidth = 35;

	mAction = NULL;
	mUpdate = 0;

	if (!node)
		return;

	LoadPlacement(FindNode(node, "placement"), &mRenderX, &mRenderY, &mRenderW, &mRenderH, &mPlacement);

	mAction = new GUIAction(node);

	child = FindNode(node, "dot");
	if(child)
	{
		mDotColor = LoadAttrColor(child, "color", mDotColor);
		mActiveDotColor = LoadAttrColor(child, "activecolor", mActiveDotColor);
		mDotRadius = LoadAttrIntScaleX(child, "radius", mDotRadius);

		mDotImage = LoadAttrImage(child, "image");
		mActiveDotImage = LoadAttrImage(child, "activeimage");
	}

	child = FindNode(node, "line");
	if(child)
	{
		mLineColor = LoadAttrColor(child, "color", mLineColor);
		mLineWidth = LoadAttrIntScaleX(child, "width", mLineWidth);
	}

	child = FindNode(node, "data");
	if(child)
		mPassVar = LoadAttrString(child, "name", "");


	if(!mDotImage || !mDotImage->GetResource() || !mActiveDotImage || !mActiveDotImage->GetResource())
	{
		mDotCircle = gr_render_circle(mDotRadius, mDotColor.red, mDotColor.green, mDotColor.blue, mDotColor.alpha);
		mActiveDotCircle = gr_render_circle(mDotRadius/2, mActiveDotColor.red, mActiveDotColor.green, mActiveDotColor.blue, mActiveDotColor.alpha);
	}
	else
		mDotRadius = mDotImage->GetWidth()/2;

	SetRenderPos(mRenderX, mRenderY, mRenderW, mRenderH);
}

GUIPatternPassword::~GUIPatternPassword()
{
	delete mDotImage;
	delete mActiveDotImage;
	delete mAction;

	if(mDotCircle)
		gr_free_surface(mDotCircle);

	if(mActiveDotCircle)
		gr_free_surface(mActiveDotCircle);
}

void GUIPatternPassword::ResetActiveDots()
{
	mConnectedDotsLen = 0;
	mCurLineX = mCurLineY = -1;
	for(int i = 0; i < 9; ++i)
		mDots[i].active = false;
}

int GUIPatternPassword::SetRenderPos(int x, int y, int w, int h)
{
	mRenderX = x;
	mRenderY = y;

	if (w || h)
	{
		mRenderW = w;
		mRenderH = h;

		mAction->SetActionPos(mRenderX, mRenderY, mRenderW, mRenderH);
		SetActionPos(mRenderX, mRenderY, mRenderW, mRenderH);
	}

	CalculateDotPositions();
	return 0;
}

void GUIPatternPassword::CalculateDotPositions(void)
{
	const int step_x = (mRenderW - mDotRadius*2) / 2;
	const int step_y = (mRenderH - mDotRadius*2) / 2;
	int x = mRenderX;
	int y = mRenderY;

	for(int r = 0; r < 3; ++r)
	{
		for(int c = 0; c < 3; ++c)
		{
			mDots[3*r+c].x = x;
			mDots[3*r+c].y = y;
			x += step_x;
		}
		x = mRenderX;
		y += step_y;
	}
}

int GUIPatternPassword::Render(void)
{
	if(!isConditionTrue())
		return 0;

	gr_color(mLineColor.red, mLineColor.green, mLineColor.blue, mLineColor.alpha);
	for(size_t i = 1; i < mConnectedDotsLen; ++i) {
		const Dot& dp = mDots[mConnectedDots[i-1]];
		const Dot& dc = mDots[mConnectedDots[i]];
		gr_line(dp.x + mDotRadius, dp.y + mDotRadius, dc.x + mDotRadius, dc.y + mDotRadius, mLineWidth);
	}

	if(mConnectedDotsLen > 0 && mTrackingTouch) {
		const Dot& dc = mDots[mConnectedDots[mConnectedDotsLen-1]];
		gr_line(dc.x + mDotRadius, dc.y + mDotRadius, mCurLineX, mCurLineY, mLineWidth);
	}

	for(int i = 0; i < 9; ++i) {
		if(mDotCircle) {
			gr_blit(mDotCircle, 0, 0, gr_get_width(mDotCircle), gr_get_height(mDotCircle), mDots[i].x, mDots[i].y);
			if(mDots[i].active) {
				gr_blit(mActiveDotCircle, 0, 0, gr_get_width(mActiveDotCircle), gr_get_height(mActiveDotCircle), mDots[i].x + mDotRadius/2, mDots[i].y + mDotRadius/2);
			}
		} else {
			if(mDots[i].active) {
				gr_blit(mActiveDotImage->GetResource(), 0, 0, mActiveDotImage->GetWidth(), mActiveDotImage->GetHeight(),
						mDots[i].x + (mDotRadius - mActiveDotImage->GetWidth()/2), mDots[i].y + (mDotRadius - mActiveDotImage->GetHeight()/2));
			} else {
				gr_blit(mDotImage->GetResource(), 0, 0, mDotImage->GetWidth(), mDotImage->GetHeight(), mDots[i].x, mDots[i].y);
			}
		}
	}
	return 0;
}

int GUIPatternPassword::Update(void)
{
	if(!isConditionTrue())
		return 0;

	int res = mNeedRender ? 2 : 1;
	mNeedRender = false;
	return res;
}

bool GUIPatternPassword::IsInRect(int x, int y, int rx, int ry, int rw, int rh)
{
	return x >= rx && y >= ry && x <= rx+rw && y <= ry+rh;
}

int GUIPatternPassword::InDot(int x, int y)
{
	for(int i = 0; i < 9; ++i) {
		if(IsInRect(x, y, mDots[i].x - mDotRadius*1.5, mDots[i].y - mDotRadius*1.5, mDotRadius*6, mDotRadius*6))
			return i;
	}
	return -1;
}

bool GUIPatternPassword::DotUsed(int dot_idx)
{
	for(size_t i = 0; i < mConnectedDotsLen; ++i) {
		if(mConnectedDots[i] == dot_idx)
			return true;
	}
	return false;
}

void GUIPatternPassword::ConnectDot(int dot_idx)
{
	if(mConnectedDotsLen >= 9)
	{
		LOGERR("mConnectedDots in GUIPatternPassword has overflown!\n");
		return;
	}

	mConnectedDots[mConnectedDotsLen++] = dot_idx;
	mDots[dot_idx].active = true;
}

void GUIPatternPassword::ConnectIntermediateDots(int dot_idx)
{
	if(mConnectedDotsLen == 0)
		return;

	const int last_dot = mConnectedDots[mConnectedDotsLen-1];
	int mid = -1;

	// The line is vertical and has crossed a point in the middle
	if(dot_idx%3 == last_dot%3 && abs(dot_idx - last_dot) > 3) {
		mid = 3 + dot_idx%3;
	// the line is horizontal and has crossed a point in the middle
	} else if(dot_idx/3 == last_dot/3 && abs(dot_idx - last_dot) > 1) {
		mid = (dot_idx/3)*3 + 1;
	// the line is diagonal and has crossed the middle point
	} else if((dot_idx == 0 && last_dot == 8) || (dot_idx == 8 && last_dot == 0) ||
			(dot_idx == 2 && last_dot == 6) || (dot_idx == 6 && last_dot == 2)) {
		mid = 4;
	} else {
		return;
	}

	if(!DotUsed(mid))
		ConnectDot(mid);
}

int GUIPatternPassword::NotifyTouch(TOUCH_STATE state, int x, int y)
{
	if(!isConditionTrue())
		return -1;

	switch (state)
	{
		case TOUCH_START:
		{
			const int dot_idx = InDot(x, y);
			if(dot_idx == -1)
				break;

			mTrackingTouch = true;
			ResetActiveDots();
			ConnectDot(dot_idx);
			DataManager::Vibrate("tw_button_vibrate");
			mCurLineX = x;
			mCurLineY = y;
			mNeedRender = true;
			break;
		}
		case TOUCH_DRAG:
		{
			if(!mTrackingTouch)
				break;

			const int dot_idx = InDot(x, y);
			if(dot_idx != -1 && !DotUsed(dot_idx))
			{
				ConnectIntermediateDots(dot_idx);
				ConnectDot(dot_idx);
				DataManager::Vibrate("tw_button_vibrate");
			}

			mCurLineX = x;
			mCurLineY = y;
			mNeedRender = true;
			break;
		}
		case TOUCH_RELEASE:
		{
			if(!mTrackingTouch)
				break;

			mNeedRender = true;
			mTrackingTouch = false;
			PatternDrawn();
			ResetActiveDots();
			break;
		}
		default:
			break;
	}
	return 0;
}

void GUIPatternPassword::PatternDrawn()
{
	if(!mPassVar.empty() && mConnectedDotsLen > 0)
	{
		std::string pass;
		for(size_t i = 0; i < mConnectedDotsLen; ++i)
			pass += '1' + mConnectedDots[i];
		DataManager::SetValue(mPassVar, pass);
	}

	if(mAction)
		mAction->doActions();
}
