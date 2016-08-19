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
#include <sstream>

extern "C" {
#include "../twcommon.h"
}
#include "../minuitwrp/minui.h"
#include "../twrp-functions.hpp"
#include "rapidxml.hpp"
#include "objects.hpp"

GUIPatternPassword::GUIPatternPassword(xml_node<>* node)
	: GUIObject(node)
{
	xml_attribute<>* attr;
	xml_node<>* child;

	// 3x3 is the default.
	mGridSize = 3;
	mDots = new Dot[mGridSize * mGridSize];
	mConnectedDots = new int[mGridSize * mGridSize];

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

	child = FindNode(node, "size");
	if(child) {
		mSizeVar = LoadAttrString(child, "name", "");

		// Use the configured default, if set.
		size_t size = LoadAttrInt(child, "default", mGridSize);
		Resize(size);
	}

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

	delete[] mDots;
	delete[] mConnectedDots;

	if(mDotCircle)
		gr_free_surface(mDotCircle);

	if(mActiveDotCircle)
		gr_free_surface(mActiveDotCircle);
}

void GUIPatternPassword::ResetActiveDots()
{
	mConnectedDotsLen = 0;
	mCurLineX = mCurLineY = -1;
	for(size_t i = 0; i < mGridSize * mGridSize; ++i)
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
	const int num_gaps = mGridSize - 1;
	const int step_x = (mRenderW - mDotRadius*2) / num_gaps;
	const int step_y = (mRenderH - mDotRadius*2) / num_gaps;
	int x = mRenderX;
	int y = mRenderY;

	/* Order is important for keyphrase generation:
	 *
	 *   0    1    2    3 ...  n-1
	 *   n  n+1  n+2  n+3 ... 2n-1
	 *  2n 2n+1 2n+2 2n+3 ... 3n-1
	 *  3n 3n+1 3n+2 3n+3 ... 4n-1
	 *   :  :    :    :
	 *                       n*n-1
	 */

	for(size_t r = 0; r < mGridSize; ++r)
	{
		for(size_t c = 0; c < mGridSize; ++c)
		{
			mDots[mGridSize*r + c].x = x;
			mDots[mGridSize*r + c].y = y;
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

	for(size_t i = 0; i < mGridSize * mGridSize; ++i) {
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

void GUIPatternPassword::Resize(size_t n) {
	if(mGridSize == n)
		return;

	delete[] mDots;
	delete[] mConnectedDots;

	mGridSize = n;
	mDots = new Dot[n*n];
	mConnectedDots = new int[n*n];

	ResetActiveDots();
	CalculateDotPositions();
	mTrackingTouch = false;
	mNeedRender = true;
}

static int pow(int x, int i)
{
	int result = 1;
	if (i<0)
		return 0;
	while(i-- > 0)
		result *= x;
	return result;
}

static bool IsInCircle(int x, int y, int ox, int oy, int r)
{
	return pow(x - ox, 2) + pow(y - oy, 2) <= pow(r, 2);
}

int GUIPatternPassword::InDot(int x, int y)
{
	for(size_t i = 0; i < mGridSize * mGridSize; ++i) {
		if(IsInCircle(x, y, mDots[i].x + mDotRadius, mDots[i].y + mDotRadius, mDotRadius*3))
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
	if(mConnectedDotsLen >= mGridSize * mGridSize)
	{
		LOGERR("mConnectedDots in GUIPatternPassword has overflown!\n");
		return;
	}

	mConnectedDots[mConnectedDotsLen++] = dot_idx;
	mDots[dot_idx].active = true;
}

void GUIPatternPassword::ConnectIntermediateDots(int next_dot_idx)
{
	if(mConnectedDotsLen == 0)
		return;

	const int prev_dot_idx = mConnectedDots[mConnectedDotsLen-1];

	int px = prev_dot_idx % mGridSize;
	int py = prev_dot_idx / mGridSize;

	int nx = next_dot_idx % mGridSize;
	int ny = next_dot_idx / mGridSize;

	/*
	 * We connect all dots that are in a straight line between the previous dot
	 * and the next one. This is simple for 3x3, but is more complicated for
	 * larger grids.
	 *
	 * Weirdly, Android doesn't do the logical thing when it comes to connecting
	 * dots between two points. Rather than simply adding all points that lie
	 * on the line between the start and end points, it instead only connects
	 * dots that are adjacent in only three directions -- horizontal, vertical
	 * and diagonal (45°).
	 *
	 * So we can just iterate over the correct axes, taking care to ensure that
	 * the order in which the intermediate points are added to the pattern is
	 * correct.
	 */

	int x = px;
	int y = py;

	int Dx = (nx > px) ? 1 : -1;
	int Dy = (ny > py) ? 1 : -1;

	// Vertical lines.
	if(px == nx)
		Dx = 0;

	// Horizontal lines.
	else if(py == ny)
		Dy = 0;

	// Diagonal lines (|∆x| = |∆y|).
	else if(abs(px - nx) == abs(py - ny))
		;

	// No valid intermediate dots.
	else
		return;

	// Iterate along axis, adding dots in the correct order.
	while((Dy == 0 || y != ny - Dy) && (Dx == 0 || x != nx - Dx)) {
		x += Dx;
		y += Dy;

		int idx = mGridSize * y + x;
		if(!DotUsed(idx))
			ConnectDot(idx);
	}
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

int GUIPatternPassword::NotifyVarChange(const std::string& varName, const std::string& value)
{
	if(!isConditionTrue())
		return 0;

	if(varName == mSizeVar) {
		Resize(atoi(value.c_str()));
		mUpdate = true;
	}
	return 0;
}

static unsigned int getSDKVersion(void) {
	unsigned int sdkver = 23;
	string sdkverstr = TWFunc::System_Property_Get("ro.build.version.sdk");
	if (!sdkverstr.empty()) {
		sdkver = (unsigned int)strtoull(sdkverstr.c_str(), NULL, 10);
		sdkver = (sdkver != 0) ? sdkver : 23;
	}
	LOGINFO("sdk version is %u\n", sdkver);
	return sdkver;
}

std::string GUIPatternPassword::GeneratePassphrase()
{
	char pattern[mConnectedDotsLen];
	for(size_t i = 0; i < mConnectedDotsLen; i++) {
		 pattern[i] = (char) mConnectedDots[i];
	}

	std::stringstream pass;
	char buffer[3] = {0};

	if ((mGridSize == 3) || (getSDKVersion() >= 23)) {
		// Marshmallow uses a consistent method
		for (size_t i = 0; i < mConnectedDotsLen; i++) {
			buffer[0] = (pattern[i] & 0xff) + '1';
			pass << std::string(buffer);
		}
	} else {
		/*
		 * Okay, rant time for pre-Marshmallow ROMs.
		 * It turns out that Android and CyanogenMod have *two* separate methods
		 * for generating passphrases from patterns. This is a legacy issue, as
		 * Android only supports 3x3 grids, and so we need to support both.
		 * Luckily, CyanogenMod is in the same boat as us and needs to support
		 * Android's 3x3 encryption style.
		 *
		 * In order to generate a 3x3 passphrase, add 1 to each dot index
		 * and concatenate the string representation of the integers. No
		 * padding should be added.
		 *
		 * For *all* other NxN passphrases (until a 16x16 grid comes along),
		 * they are generated by taking "%.2x" for each dot index and
		 * concatenating the results (without adding 1).
		 */
		for (size_t i = 0; i < mConnectedDotsLen; i++) {
			snprintf(buffer, 3, "%.2x", pattern[i] & 0xff);
			pass << std::string(buffer);
		}
	}

	return pass.str();
}

void GUIPatternPassword::PatternDrawn()
{
	if(!mPassVar.empty() && mConnectedDotsLen > 0)
		DataManager::SetValue(mPassVar, GeneratePassphrase());

	if(mAction)
		mAction->doActions();
}
