/*
	Copyright 2013 bigbiff/Dees_Troy TeamWin
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

#include <string.h>

extern "C" {
#include "../twcommon.h"
#include "../minuitwrp/minui.h"
}

#include "rapidxml.hpp"
#include "objects.hpp"
#include "../data.hpp"

#define SCROLLING_SPEED_DECREMENT 6
#define SCROLLING_FLOOR 10
#define SCROLLING_MULTIPLIER 6

GUIScrollList::GUIScrollList(xml_node<>* node) : GUIObject(node)
{
	xml_attribute<>* attr;
	xml_node<>* child;
	int header_separator_color_specified = 0, header_separator_height_specified = 0, header_text_color_specified = 0, header_background_color_specified = 0;

	mStart = mLineSpacing = startY = mFontHeight = mSeparatorH = scrollingY = scrollingSpeed = 0;
	maxIconWidth = maxIconHeight =  mHeaderIconHeight = mHeaderIconWidth = 0;
	mHeaderSeparatorH = mHeaderIsStatic = mHeaderH = actualLineHeight = 0;
	mBackground = mFont = mHeaderIcon = NULL;
	mBackgroundW = mBackgroundH = 0;
	mFastScrollW = mFastScrollLineW = mFastScrollRectW = mFastScrollRectH = 0;
	mFastScrollRectX = -1;
	lastY = last2Y = fastScroll = 0;
	mUpdate = 0;
	touchDebounce = 6;
	ConvertStrToColor("black", &mBackgroundColor);
	ConvertStrToColor("black", &mHeaderBackgroundColor);
	ConvertStrToColor("black", &mSeparatorColor);
	ConvertStrToColor("black", &mHeaderSeparatorColor);
	ConvertStrToColor("white", &mFontColor);
	ConvertStrToColor("white", &mHeaderFontColor);
	ConvertStrToColor("white", &mFastScrollLineColor);
	ConvertStrToColor("white", &mFastScrollRectColor);
	hasHighlightColor = false;
	hasFontHighlightColor = false;
	isHighlighted = false;
	startSelection = -1;

	// Load header text
	child = node->first_node("header");
	if (child)
	{
		attr = child->first_attribute("icon");
		if (attr)
			mHeaderIcon = PageManager::FindResource(attr->value());

		attr = child->first_attribute("background");
		if (attr)
		{
			std::string color = attr->value();
			ConvertStrToColor(color, &mHeaderBackgroundColor);
			header_background_color_specified = -1;
		}
		attr = child->first_attribute("textcolor");
		if (attr)
		{
			std::string color = attr->value();
			ConvertStrToColor(color, &mHeaderFontColor);
			header_text_color_specified = -1;
		}
		attr = child->first_attribute("separatorcolor");
		if (attr)
		{
			std::string color = attr->value();
			ConvertStrToColor(color, &mHeaderSeparatorColor);
			header_separator_color_specified = -1;
		}
		attr = child->first_attribute("separatorheight");
		if (attr) {
			string parsevalue = gui_parse_text(attr->value());
			mHeaderSeparatorH = atoi(parsevalue.c_str());
			header_separator_height_specified = -1;
		}
	}
	child = node->first_node("text");
	if (child)  mHeaderText = child->value();

	memset(&mHighlightColor, 0, sizeof(COLOR));
	child = node->first_node("highlight");
	if (child) {
		attr = child->first_attribute("color");
		if (attr) {
			hasHighlightColor = true;
			std::string color = attr->value();
			ConvertStrToColor(color, &mHighlightColor);
		}
	}

	// Simple way to check for static state
	mLastHeaderValue = gui_parse_text(mHeaderText);
	if (mLastHeaderValue != mHeaderText)
		mHeaderIsStatic = 0;
	else
		mHeaderIsStatic = -1;

	child = node->first_node("background");
	if (child)
	{
		attr = child->first_attribute("resource");
		if (attr)
			mBackground = PageManager::FindResource(attr->value());
		attr = child->first_attribute("color");
		if (attr)
		{
			std::string color = attr->value();
			ConvertStrToColor(color, &mBackgroundColor);
			if (!header_background_color_specified)
				ConvertStrToColor(color, &mHeaderBackgroundColor);
		}
	}

	// Load the placement
	LoadPlacement(node->first_node("placement"), &mRenderX, &mRenderY, &mRenderW, &mRenderH);
	SetActionPos(mRenderX, mRenderY, mRenderW, mRenderH);

	// Load the font, and possibly override the color
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
			ConvertStrToColor(color, &mFontColor);
			if (!header_text_color_specified)
				ConvertStrToColor(color, &mHeaderFontColor);
		}

		attr = child->first_attribute("spacing");
		if (attr) {
			string parsevalue = gui_parse_text(attr->value());
			mLineSpacing = atoi(parsevalue.c_str());
		}

		attr = child->first_attribute("highlightcolor");
		memset(&mFontHighlightColor, 0, sizeof(COLOR));
		if (attr)
		{
			std::string color = attr->value();
			ConvertStrToColor(color, &mFontHighlightColor);
			hasFontHighlightColor = true;
		}
	}

	// Load the separator if it exists
	child = node->first_node("separator");
	if (child)
	{
		attr = child->first_attribute("color");
		if (attr)
		{
			std::string color = attr->value();
			ConvertStrToColor(color, &mSeparatorColor);
			if (!header_separator_color_specified)
				ConvertStrToColor(color, &mHeaderSeparatorColor);
		}

		attr = child->first_attribute("height");
		if (attr) {
			string parsevalue = gui_parse_text(attr->value());
			mSeparatorH = atoi(parsevalue.c_str());
			if (!header_separator_height_specified)
				mHeaderSeparatorH = mSeparatorH;
		}
	}

	// Fast scroll colors
	child = node->first_node("fastscroll");
	if (child)
	{
		attr = child->first_attribute("linecolor");
		if(attr)
			ConvertStrToColor(attr->value(), &mFastScrollLineColor);

		attr = child->first_attribute("rectcolor");
		if(attr)
			ConvertStrToColor(attr->value(), &mFastScrollRectColor);

		attr = child->first_attribute("w");
		if (attr) {
			string parsevalue = gui_parse_text(attr->value());
			mFastScrollW = atoi(parsevalue.c_str());
		}

		attr = child->first_attribute("linew");
		if (attr) {
			string parsevalue = gui_parse_text(attr->value());
			mFastScrollLineW = atoi(parsevalue.c_str());
		}

		attr = child->first_attribute("rectw");
		if (attr) {
			string parsevalue = gui_parse_text(attr->value());
			mFastScrollRectW = atoi(parsevalue.c_str());
		}

		attr = child->first_attribute("recth");
		if (attr) {
			string parsevalue = gui_parse_text(attr->value());
			mFastScrollRectH = atoi(parsevalue.c_str());
		}
	}

	// Retrieve the line height
	mFontHeight = gr_getMaxFontHeight(mFont ? mFont->GetResource() : NULL);
	mHeaderH = mFontHeight;

	if (mHeaderIcon && mHeaderIcon->GetResource())
	{
		mHeaderIconWidth = gr_get_width(mHeaderIcon->GetResource());
		mHeaderIconHeight = gr_get_height(mHeaderIcon->GetResource());
		if (mHeaderIconHeight > mHeaderH)
			mHeaderH = mHeaderIconHeight;
		if (mHeaderIconWidth > maxIconWidth)
			maxIconWidth = mHeaderIconWidth;
	}

	mHeaderH += mLineSpacing + mHeaderSeparatorH;
	actualLineHeight = mFontHeight + mLineSpacing + mSeparatorH;
	if (mHeaderH < actualLineHeight)
		mHeaderH = actualLineHeight;

	if (actualLineHeight / 3 > 6)
		touchDebounce = actualLineHeight / 3;

	if (mBackground && mBackground->GetResource())
	{
		mBackgroundW = gr_get_width(mBackground->GetResource());
		mBackgroundH = gr_get_height(mBackground->GetResource());
	}
}

GUIScrollList::~GUIScrollList()
{
	delete mHeaderIcon;
	delete mBackground;
	delete mFont;
}

void GUIScrollList::SetMaxIconSize(int w, int h)
{
	if (w > maxIconWidth)
		maxIconWidth = w;
	if (h > maxIconHeight)
		maxIconHeight = h;
	if (maxIconHeight > mFontHeight) {
		actualLineHeight = maxIconHeight + mLineSpacing + mSeparatorH;
		if (actualLineHeight > mHeaderH)
			mHeaderH = actualLineHeight;
	}
}

void GUIScrollList::SetVisibleListLocation(size_t list_index)
{
	// This will make sure that the item indicated by list_index is visible on the screen
	size_t lines = (mRenderH - mHeaderH) / (actualLineHeight), listSize = GetListSize();

	if (list_index <= (unsigned)mStart) {
		// list_index is above the currently displayed items, put the selected item at the very top
		mStart = list_index;
		scrollingY = 0;
	} else if (list_index >= mStart + lines) {
		// list_index is below the currently displayed items, put the selected item at the very bottom
		mStart = list_index - lines + 1;
		if ((mRenderH - mHeaderH) % actualLineHeight != 0) {
			// There's a partial row displayed, set the scrolling offset so that the selected item really is at the very bottom
			mStart--;
			scrollingY = ((mRenderH - mHeaderH) % actualLineHeight) - actualLineHeight;
		} else {
			// There's no partial row so zero out the offset
			scrollingY = 0;
		}
	}

	mUpdate = 1;
}

int GUIScrollList::Render(void)
{
	if(!isConditionTrue())
		return 0;

	// First step, fill background
	gr_color(mBackgroundColor.red, mBackgroundColor.green, mBackgroundColor.blue, 255);
	gr_fill(mRenderX, mRenderY + mHeaderH, mRenderW, mRenderH - mHeaderH);

	// Next, render the background resource (if it exists)
	if (mBackground && mBackground->GetResource())
	{
		int mBackgroundX = mRenderX + ((mRenderW - mBackgroundW) / 2);
		int mBackgroundY = mRenderY + ((mRenderH - mBackgroundH) / 2);
		gr_blit(mBackground->GetResource(), 0, 0, mBackgroundW, mBackgroundH, mBackgroundX, mBackgroundY);
	}

	// This tells us how many lines we can actually render
	size_t lines = (mRenderH - mHeaderH) / (actualLineHeight);

	size_t listSize = GetListSize();
	int listW = mRenderW;
	int mFastScrollRectY = -1;

	if (listSize < lines) {
		lines = listSize;
		scrollingY = 0;
		mFastScrollRectX = -1;
	} else {
		listW -= mFastScrollW; // space for fast scroll
		lines++;
		if (lines < listSize)
			lines++;
	}

	void* fontResource = NULL;
	if (mFont)  fontResource = mFont->GetResource();

	int yPos = mRenderY + mHeaderH + scrollingY;
	int fontOffsetY = (int)((actualLineHeight - mFontHeight) / 2);
	int currentIconHeight = 0, currentIconWidth = 0;
	int currentIconOffsetY = 0, currentIconOffsetX = 0;
	size_t actualSelection = mStart;

	if (isHighlighted) {
		int selectY = scrollingY;

		// Locate the correct line for highlighting
		while (selectY + actualLineHeight < startSelection) {
			selectY += actualLineHeight;
			actualSelection++;
		}
		if (hasHighlightColor) {
			// Highlight the area
			gr_color(mHighlightColor.red, mHighlightColor.green, mHighlightColor.blue, 255);
			int HighlightHeight = actualLineHeight;
			if (mRenderY + mHeaderH + selectY + actualLineHeight > mRenderH + mRenderY) {
				HighlightHeight = actualLineHeight - (mRenderY + mHeaderH + selectY + actualLineHeight - mRenderH - mRenderY);
			}
			gr_fill(mRenderX, mRenderY + mHeaderH + selectY, mRenderW, HighlightHeight);
		}
	}

	// render all visible items
	for (size_t line = 0; line < lines; line++)
	{
		size_t itemindex = line + mStart;
		if (itemindex >= listSize)
			break;

		// get item data
		Resource* icon;
		std::string label;
		if (GetListItem(itemindex, icon, label))
			break;

		if (isHighlighted && hasFontHighlightColor && itemindex == actualSelection) {
			// Use the highlight color for the font
			gr_color(mFontHighlightColor.red, mFontHighlightColor.green, mFontHighlightColor.blue, 255);
		} else {
			// Set the color for the font
			gr_color(mFontColor.red, mFontColor.green, mFontColor.blue, 255);
		}

		if (icon && icon->GetResource()) {
			currentIconHeight = gr_get_height(icon->GetResource());
			currentIconWidth = gr_get_width(icon->GetResource());
			currentIconOffsetY = (int)((actualLineHeight - currentIconHeight) / 2);
			currentIconOffsetX = (maxIconWidth - currentIconWidth) / 2;
			int rect_y = 0, image_y = (yPos + currentIconOffsetY);
			if (image_y + currentIconHeight > mRenderY + mRenderH)
				rect_y = mRenderY + mRenderH - image_y;
			else
				rect_y = currentIconHeight;
			gr_blit(icon->GetResource(), 0, 0, currentIconWidth, rect_y, mRenderX + currentIconOffsetX, image_y);
		}

		gr_textExWH(mRenderX + maxIconWidth + 5, yPos + fontOffsetY, label.c_str(), fontResource, mRenderX + listW, mRenderY + mRenderH);

		// Add the separator
		if (yPos + actualLineHeight < mRenderH + mRenderY) {
			gr_color(mSeparatorColor.red, mSeparatorColor.green, mSeparatorColor.blue, 255);
			gr_fill(mRenderX, yPos + actualLineHeight - mSeparatorH, listW, mSeparatorH);
		}

		// Move the yPos
		yPos += actualLineHeight;
	}

	// Render the Header (last so that it overwrites the top most row for per pixel scrolling)
	// First step, fill background
	gr_color(mHeaderBackgroundColor.red, mHeaderBackgroundColor.green, mHeaderBackgroundColor.blue, 255);
	gr_fill(mRenderX, mRenderY, mRenderW, mHeaderH);

	// Now, we need the header (icon + text)
	yPos = mRenderY;
	{
		Resource* headerIcon;
		int mIconOffsetX = 0;

		// render the icon if it exists
		headerIcon = mHeaderIcon;
		if (headerIcon && headerIcon->GetResource())
		{
			gr_blit(headerIcon->GetResource(), 0, 0, mHeaderIconWidth, mHeaderIconHeight, mRenderX + ((mHeaderIconWidth - maxIconWidth) / 2), (yPos + (int)((mHeaderH - mHeaderIconHeight) / 2)));
			mIconOffsetX = maxIconWidth;
		}

		// render the text
		gr_color(mHeaderFontColor.red, mHeaderFontColor.green, mHeaderFontColor.blue, 255);
		gr_textExWH(mRenderX + mIconOffsetX + 5, yPos + (int)((mHeaderH - mFontHeight) / 2), mLastHeaderValue.c_str(), fontResource, mRenderX + mRenderW, mRenderY + mRenderH);

		// Add the separator
		gr_color(mHeaderSeparatorColor.red, mHeaderSeparatorColor.green, mHeaderSeparatorColor.blue, 255);
		gr_fill(mRenderX, yPos + mHeaderH - mHeaderSeparatorH, mRenderW, mHeaderSeparatorH);
	}

	// render fast scroll
	lines = (mRenderH - mHeaderH) / (actualLineHeight);
	if(mFastScrollW > 0 && listSize > lines) {
		int startX = listW + mRenderX;
		int fWidth = mRenderW - listW;
		int fHeight = mRenderH - mHeaderH;

		// line
		gr_color(mFastScrollLineColor.red, mFastScrollLineColor.green, mFastScrollLineColor.blue, 255);
		gr_fill(startX + fWidth/2, mRenderY + mHeaderH, mFastScrollLineW, mRenderH - mHeaderH);

		// rect
		int pct = 0;
		if ((mRenderH - mHeaderH) % actualLineHeight != 0) {
			// Properly handle the percentage if a partial line is present
			int partial_line_size = actualLineHeight - ((mRenderH - mHeaderH) % actualLineHeight);
			pct = ((mStart*actualLineHeight - scrollingY)*100)/(listSize*actualLineHeight-((lines + 1)*actualLineHeight) + partial_line_size);
		} else {
			pct = ((mStart*actualLineHeight - scrollingY)*100)/(listSize*actualLineHeight-lines*actualLineHeight);
		}
		mFastScrollRectX = startX + (fWidth - mFastScrollRectW)/2;
		mFastScrollRectY = mRenderY+mHeaderH + ((fHeight - mFastScrollRectH)*pct)/100;

		gr_color(mFastScrollRectColor.red, mFastScrollRectColor.green, mFastScrollRectColor.blue, 255);
		gr_fill(mFastScrollRectX, mFastScrollRectY, mFastScrollRectW, mFastScrollRectH);
	}
	mUpdate = 0;
	return 0;
}

int GUIScrollList::Update(void)
{
	if(!isConditionTrue())
		return 0;

	if (!mHeaderIsStatic) {
		std::string newValue = gui_parse_text(mHeaderText);
		if (mLastHeaderValue != newValue) {
			mLastHeaderValue = newValue;
			mUpdate = 1;
		}
	}

	// Handle kinetic scrolling
	if (scrollingSpeed == 0) {
		// Do nothing
	} else if (scrollingSpeed > 0) {
		if (scrollingSpeed < ((int) (actualLineHeight * 2.5))) {
			scrollingY += scrollingSpeed;
			scrollingSpeed -= SCROLLING_SPEED_DECREMENT;
		} else {
			scrollingY += ((int) (actualLineHeight * 2.5));
			scrollingSpeed -= SCROLLING_SPEED_DECREMENT;
		}
		while (mStart && scrollingY > 0) {
			mStart--;
			scrollingY -= actualLineHeight;
		}
		if (mStart == 0 && scrollingY > 0) {
			scrollingY = 0;
			scrollingSpeed = 0;
		} else if (scrollingSpeed < SCROLLING_FLOOR)
			scrollingSpeed = 0;
		mUpdate = 1;
	} else if (scrollingSpeed < 0) {
		int totalSize = GetListSize();
		int lines = (mRenderH - mHeaderH) / (actualLineHeight);

		if (totalSize > lines) {
			int bottom_offset = ((int)(mRenderH) - mHeaderH) - (lines * actualLineHeight);

			bottom_offset -= actualLineHeight;

			if (abs(scrollingSpeed) < ((int) (actualLineHeight * 2.5))) {
				scrollingY += scrollingSpeed;
				scrollingSpeed += SCROLLING_SPEED_DECREMENT;
			} else {
				scrollingY -= ((int) (actualLineHeight * 2.5));
				scrollingSpeed += SCROLLING_SPEED_DECREMENT;
			}
			while (mStart + lines + (bottom_offset ? 1 : 0) < totalSize && abs(scrollingY) > actualLineHeight) {
				mStart++;
				scrollingY += actualLineHeight;
			}
			if (bottom_offset != 0 && mStart + lines + 1 >= totalSize && scrollingY <= bottom_offset) {
				mStart = totalSize - lines - 1;
				scrollingY = bottom_offset;
			} else if (mStart + lines >= totalSize && scrollingY < 0) {
				mStart = totalSize - lines;
				scrollingY = 0;
			} else if (scrollingSpeed * -1 < SCROLLING_FLOOR)
				scrollingSpeed = 0;
			mUpdate = 1;
		}
	}

	return 0;
}

int GUIScrollList::GetSelection(int x, int y)
{
	// We only care about y position
	if (y < mRenderY || y - mRenderY <= mHeaderH || y - mRenderY > mRenderH) return -1;
	return (y - mRenderY - mHeaderH);
}

int GUIScrollList::NotifyTouch(TOUCH_STATE state, int x, int y)
{
	if(!isConditionTrue())
		return -1;

	int selection = 0;

	switch (state)
	{
	case TOUCH_START:
		if (scrollingSpeed != 0) {
			startSelection = -1; // this allows the user to tap the list to stop the scrolling without selecting the item they tap
			scrollingSpeed = 0; // reset the scrolling speed to 0 on a new touch
		} else
			startSelection = GetSelection(x,y);
		isHighlighted = (startSelection > -1);
		if (isHighlighted)
			mUpdate = 1;
		startY = lastY = last2Y = y;

		if(mFastScrollRectX != -1 && x >= mRenderX + mRenderW - mFastScrollW)
			fastScroll = 1;
		break;

	case TOUCH_DRAG:
		// Check if we dragged out of the selection window
		if (GetSelection(x, y) == -1) {
			last2Y = lastY = 0;
			if (isHighlighted) {
				isHighlighted = false;
				mUpdate = 1;
			}
			break;
		}

		// Fast scroll
		if(fastScroll)
		{
			int pct = ((y-mRenderY-mHeaderH)*100)/(mRenderH-mHeaderH);
			int totalSize = GetListSize();
			int lines = (mRenderH - mHeaderH) / (actualLineHeight);

			float l = float((totalSize-lines)*pct)/100;
			if(l + lines >= totalSize)
			{
				mStart = totalSize - lines;
				if ((mRenderH - mHeaderH) % actualLineHeight != 0) {
					// There's a partial row displayed, set the scrolling offset so that the last item really is at the very bottom
					mStart--;
					scrollingY = ((mRenderH - mHeaderH) % actualLineHeight) - actualLineHeight;
				} else {
					// There's no partial row so zero out the offset
					scrollingY = 0;
				}
			}
			else
			{
				mStart = l;
				scrollingY = -(l - int(l))*actualLineHeight;
				if ((mRenderH - mHeaderH) % actualLineHeight != 0) {
					// There's a partial row displayed, make sure scrollingY doesn't go past the max
					if (mStart == totalSize - lines - 1 && scrollingY < ((mRenderH - mHeaderH) % actualLineHeight) - actualLineHeight)
						scrollingY = ((mRenderH - mHeaderH) % actualLineHeight) - actualLineHeight;
				} else if (mStart == totalSize - lines)
					scrollingY = 0;
			}

			startSelection = -1;
			mUpdate = 1;
			scrollingSpeed = 0; // prevent kinetic scrolling when using fast scroll
			isHighlighted = false;
			break;
		}

		// Provide some debounce on initial touches
		if (startSelection != -1 && abs(y - startY) < touchDebounce) {
			isHighlighted = true;
			mUpdate = 1;
			break;
		}

		isHighlighted = false;
		last2Y = lastY;
		lastY = y;
		startSelection = -1;

		// Handle scrolling
		scrollingY += y - startY;
		startY = y;
		while(mStart && scrollingY > 0) {
			mStart--;
			scrollingY -= actualLineHeight;
		}
		if (mStart == 0 && scrollingY > 0)
			scrollingY = 0;
		{
			int totalSize = GetListSize();
			int lines = (mRenderH - mHeaderH) / (actualLineHeight);

			if (totalSize > lines) {
				int bottom_offset = ((int)(mRenderH) - mHeaderH) - (lines * actualLineHeight);

				bottom_offset -= actualLineHeight;

				while (mStart + lines + (bottom_offset ? 1 : 0) < totalSize && abs(scrollingY) > actualLineHeight) {
					mStart++;
					scrollingY += actualLineHeight;
				}
				if (bottom_offset != 0 && mStart + lines + 1 >= totalSize && scrollingY <= bottom_offset) {
					mStart = totalSize - lines - 1;
					scrollingY = bottom_offset;
				} else if (mStart + lines >= totalSize && scrollingY < 0) {
					mStart = totalSize - lines;
					scrollingY = 0;
				}
			} else
				scrollingY = 0;
		}
		mUpdate = 1;
		break;

	case TOUCH_RELEASE:
		isHighlighted = false;
		fastScroll = 0;
		if (startSelection >= 0)
		{
			// We've selected an item!
			std::string str;

			size_t listSize = GetListSize();
			int selectY = scrollingY;
			size_t actualSelection = mStart;

			// Move the selection to the proper place in the array
			while (selectY + actualLineHeight < startSelection) {
				selectY += actualLineHeight;
				actualSelection++;
			}

			if (actualSelection < listSize)
			{
				// WE SELECTED SOMETHING!
				NotifySelect(actualSelection);
				mUpdate = 1;

				DataManager::Vibrate("tw_button_vibrate");
			}
		} else {
			// This is for kinetic scrolling
			scrollingSpeed = lastY - last2Y;
			if (abs(scrollingSpeed) > SCROLLING_FLOOR)
				scrollingSpeed *= SCROLLING_MULTIPLIER;
			else
				scrollingSpeed = 0;
		}
	case TOUCH_REPEAT:
	case TOUCH_HOLD:
		break;
	}
	return 0;
}

int GUIScrollList::NotifyVarChange(const std::string& varName, const std::string& value)
{
	GUIObject::NotifyVarChange(varName, value);

	if(!isConditionTrue())
		return 0;

	if (!mHeaderIsStatic) {
		std::string newValue = gui_parse_text(mHeaderText);
		if (mLastHeaderValue != newValue) {
			mLastHeaderValue = newValue;
			mStart = 0;
			scrollingY = 0;
			scrollingSpeed = 0; // stop kinetic scrolling on variable changes
			mUpdate = 1;
		}
	}
	return 0;
}

int GUIScrollList::SetRenderPos(int x, int y, int w /* = 0 */, int h /* = 0 */)
{
	mRenderX = x;
	mRenderY = y;
	if (w || h)
	{
		mRenderW = w;
		mRenderH = h;
	}
	SetActionPos(mRenderX, mRenderY, mRenderW, mRenderH);
	mUpdate = 1;
	return 0;
}

void GUIScrollList::SetPageFocus(int inFocus)
{
	if (inFocus) {
		NotifyVarChange("", ""); // This forces a check for the header text
		mUpdate = 1;
	}
}
