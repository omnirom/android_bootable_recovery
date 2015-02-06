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

const int SCROLLING_SPEED_DECREMENT = 6; // friction
const int SCROLLING_FLOOR = 10;	// minimum pixels for scrolling to start or stop
const int SCROLLING_MULTIPLIER = 1; // initial speed of kinetic scrolling
const float SCROLLING_SPEED_LIMIT = 2.5; // maximum number of items to scroll per update

GUIScrollList::GUIScrollList(xml_node<>* node) : GUIObject(node)
{
	xml_attribute<>* attr;
	xml_node<>* child;
	int header_separator_color_specified = 0, header_separator_height_specified = 0, header_text_color_specified = 0, header_background_color_specified = 0;

	firstDisplayedItem = mItemSpacing = mFontHeight = mSeparatorH = y_offset = scrollingSpeed = 0;
	maxIconWidth = maxIconHeight =  mHeaderIconHeight = mHeaderIconWidth = 0;
	mHeaderSeparatorH = mHeaderIsStatic = mHeaderH = actualItemHeight = 0;
	mBackground = mFont = mHeaderIcon = NULL;
	mBackgroundW = mBackgroundH = 0;
	mFastScrollW = mFastScrollLineW = mFastScrollRectW = mFastScrollRectH = 0;
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
	selectedItem = NO_ITEM;

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
			mHeaderSeparatorH = scale_theme_y(atoi(parsevalue.c_str()));
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
			mItemSpacing = scale_theme_y(atoi(parsevalue.c_str()));
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
			mSeparatorH = scale_theme_y(atoi(parsevalue.c_str()));
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
			mFastScrollW = scale_theme_x(atoi(parsevalue.c_str()));
		}

		attr = child->first_attribute("linew");
		if (attr) {
			string parsevalue = gui_parse_text(attr->value());
			mFastScrollLineW = scale_theme_x(atoi(parsevalue.c_str()));
		}

		attr = child->first_attribute("rectw");
		if (attr) {
			string parsevalue = gui_parse_text(attr->value());
			mFastScrollRectW = scale_theme_x(atoi(parsevalue.c_str()));
		}

		attr = child->first_attribute("recth");
		if (attr) {
			string parsevalue = gui_parse_text(attr->value());
			mFastScrollRectH = scale_theme_y(atoi(parsevalue.c_str()));
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

	mHeaderH += mItemSpacing + mHeaderSeparatorH;
	actualItemHeight = mFontHeight + mItemSpacing + mSeparatorH;
	if (mHeaderH < actualItemHeight)
		mHeaderH = actualItemHeight;

	if (actualItemHeight / 3 > 6)
		touchDebounce = actualItemHeight / 3;

	if (mBackground && mBackground->GetResource())
	{
		mBackgroundW = gr_get_width(mBackground->GetResource());
		mBackgroundH = gr_get_height(mBackground->GetResource());
	}
}

GUIScrollList::~GUIScrollList()
{
}

void GUIScrollList::SetMaxIconSize(int w, int h)
{
	if (w > maxIconWidth)
		maxIconWidth = w;
	if (h > maxIconHeight)
		maxIconHeight = h;
	if (maxIconHeight > mFontHeight) {
		actualItemHeight = maxIconHeight + mItemSpacing + mSeparatorH;
		if (actualItemHeight > mHeaderH)
			mHeaderH = actualItemHeight;
	}
}

void GUIScrollList::SetVisibleListLocation(size_t list_index)
{
	// This will make sure that the item indicated by list_index is visible on the screen
	size_t lines = GetDisplayItemCount(), listSize = GetItemCount();

	if (list_index <= (unsigned)firstDisplayedItem) {
		// list_index is above the currently displayed items, put the selected item at the very top
		firstDisplayedItem = list_index;
		y_offset = 0;
	} else if (list_index >= firstDisplayedItem + lines) {
		// list_index is below the currently displayed items, put the selected item at the very bottom
		firstDisplayedItem = list_index - lines + 1;
		if (GetDisplayRemainder() != 0) {
			// There's a partial row displayed, set the scrolling offset so that the selected item really is at the very bottom
			firstDisplayedItem--;
			y_offset = GetDisplayRemainder() - actualItemHeight;
		} else {
			// There's no partial row so zero out the offset
			y_offset = 0;
		}
	}
	scrollingSpeed = 0; // stop kinetic scrolling on setting visible location
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
	size_t lines = GetDisplayItemCount();

	size_t listSize = GetItemCount();
	int listW = mRenderW;

	if (listSize <= lines) {
		hasScroll = false;
		scrollingSpeed = 0;
		lines = listSize;
		y_offset = 0;
	} else {
		hasScroll = true;
		listW -= mFastScrollW; // space for fast scroll
		lines++;
		if (lines < listSize)
			lines++;
	}

	void* fontResource = NULL;
	if (mFont)  fontResource = mFont->GetResource();

	int yPos = mRenderY + mHeaderH + y_offset;
	int fontOffsetY = (int)((actualItemHeight - mFontHeight) / 2);

	// render all visible items
	for (size_t line = 0; line < lines; line++)
	{
		size_t itemindex = line + firstDisplayedItem;
		if (itemindex >= listSize)
			break;

		// get item data
		Resource* icon;
		std::string label;
		if (GetListItem(itemindex, icon, label))
			break;

		if (hasHighlightColor && itemindex == selectedItem) {
			// Highlight the item background of the selected item
			gr_color(mHighlightColor.red, mHighlightColor.green, mHighlightColor.blue, 255);
			int HighlightHeight = actualItemHeight;
			if (yPos + HighlightHeight > mRenderY + mRenderH) {
				HighlightHeight = mRenderY + mRenderH - yPos;
			}
			gr_fill(mRenderX, yPos, mRenderW, HighlightHeight);
		}

		if (hasFontHighlightColor && itemindex == selectedItem) {
			// Use the highlight color for the font
			gr_color(mFontHighlightColor.red, mFontHighlightColor.green, mFontHighlightColor.blue, 255);
		} else {
			// Set the color for the font
			gr_color(mFontColor.red, mFontColor.green, mFontColor.blue, 255);
		}

		if (icon && icon->GetResource()) {
			int currentIconHeight = gr_get_height(icon->GetResource());
			int currentIconWidth = gr_get_width(icon->GetResource());
			int currentIconOffsetY = (int)((actualItemHeight - currentIconHeight) / 2);
			int currentIconOffsetX = (maxIconWidth - currentIconWidth) / 2;
			int rect_y = 0, image_y = (yPos + currentIconOffsetY);
			if (image_y + currentIconHeight > mRenderY + mRenderH)
				rect_y = mRenderY + mRenderH - image_y;
			else
				rect_y = currentIconHeight;
			gr_blit(icon->GetResource(), 0, 0, currentIconWidth, rect_y, mRenderX + currentIconOffsetX, image_y);
		}

		gr_textExWH(mRenderX + maxIconWidth + 5, yPos + fontOffsetY, label.c_str(), fontResource, mRenderX + listW, mRenderY + mRenderH);

		// Add the separator
		if (yPos + actualItemHeight < mRenderH + mRenderY) {
			gr_color(mSeparatorColor.red, mSeparatorColor.green, mSeparatorColor.blue, 255);
			gr_fill(mRenderX, yPos + actualItemHeight - mSeparatorH, listW, mSeparatorH);
		}

		// Move the yPos
		yPos += actualItemHeight;
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
	lines = GetDisplayItemCount();
	if (hasScroll) {
		int startX = listW + mRenderX;
		int fWidth = mRenderW - listW;
		int fHeight = mRenderH - mHeaderH;

		// line
		gr_color(mFastScrollLineColor.red, mFastScrollLineColor.green, mFastScrollLineColor.blue, 255);
		gr_fill(startX + fWidth/2, mRenderY + mHeaderH, mFastScrollLineW, mRenderH - mHeaderH);

		// rect
		int pct = 0;
		if (GetDisplayRemainder() != 0) {
			// Properly handle the percentage if a partial line is present
			int partial_line_size = actualItemHeight - GetDisplayRemainder();
			pct = ((firstDisplayedItem*actualItemHeight - y_offset)*100)/(listSize*actualItemHeight-((lines + 1)*actualItemHeight) + partial_line_size);
		} else {
			pct = ((firstDisplayedItem*actualItemHeight - y_offset)*100)/(listSize*actualItemHeight-lines*actualItemHeight);
		}
		int mFastScrollRectX = startX + (fWidth - mFastScrollRectW)/2;
		int mFastScrollRectY = mRenderY+mHeaderH + ((fHeight - mFastScrollRectH)*pct)/100;

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
	int maxScrollDistance = actualItemHeight * SCROLLING_SPEED_LIMIT;
	if (scrollingSpeed == 0) {
		// Do nothing
		return 0;
	} else if (scrollingSpeed > 0) {
		if (scrollingSpeed < maxScrollDistance)
			y_offset += scrollingSpeed;
		else
			y_offset += maxScrollDistance;
		scrollingSpeed -= SCROLLING_SPEED_DECREMENT;
	} else if (scrollingSpeed < 0) {
		if (abs(scrollingSpeed) < maxScrollDistance)
			y_offset += scrollingSpeed;
		else
			y_offset -= maxScrollDistance;
		scrollingSpeed += SCROLLING_SPEED_DECREMENT;
	}
	if (abs(scrollingSpeed) < SCROLLING_FLOOR)
		scrollingSpeed = 0;
	HandleScrolling();
	mUpdate = 1;

	return 0;
}

size_t GUIScrollList::HitTestItem(int x, int y)
{
	// We only care about y position
	if (y < mRenderY || y - mRenderY <= mHeaderH || y - mRenderY > mRenderH)
		return NO_ITEM;

	int startSelection = (y - mRenderY - mHeaderH);

	// Locate the correct item
	size_t actualSelection = firstDisplayedItem;
	int selectY = y_offset;
	while (selectY + actualItemHeight < startSelection) {
		selectY += actualItemHeight;
		actualSelection++;
	}

	if (actualSelection < GetItemCount())
		return actualSelection;

	return NO_ITEM;
}

int GUIScrollList::NotifyTouch(TOUCH_STATE state, int x, int y)
{
	if(!isConditionTrue())
		return -1;

	switch (state)
	{
	case TOUCH_START:
		if (hasScroll && x >= mRenderX + mRenderW - mFastScrollW)
			fastScroll = 1; // Initial touch is in the fast scroll region
		if (scrollingSpeed != 0) {
			selectedItem = NO_ITEM; // this allows the user to tap the list to stop the scrolling without selecting the item they tap
			scrollingSpeed = 0; // stop scrolling on a new touch
		} else if (!fastScroll) {
			// find out which item the user touched
			selectedItem = HitTestItem(x, y);
		}
		if (selectedItem != NO_ITEM)
			mUpdate = 1;
		lastY = last2Y = y;
		break;

	case TOUCH_DRAG:
		if (fastScroll)
		{
			int pct = ((y-mRenderY-mHeaderH)*100)/(mRenderH-mHeaderH);
			int totalSize = GetItemCount();
			int lines = GetDisplayItemCount();

			float l = float((totalSize-lines)*pct)/100;
			if(l + lines >= totalSize)
			{
				firstDisplayedItem = totalSize - lines;
				if (GetDisplayRemainder() != 0) {
					// There's a partial row displayed, set the scrolling offset so that the last item really is at the very bottom
					firstDisplayedItem--;
					y_offset = GetDisplayRemainder() - actualItemHeight;
				} else {
					// There's no partial row so zero out the offset
					y_offset = 0;
				}
			}
			else
			{
				if (l < 0)
					l = 0;
				firstDisplayedItem = l;
				y_offset = -(l - int(l))*actualItemHeight;
				if (GetDisplayRemainder() != 0) {
					// There's a partial row displayed, make sure y_offset doesn't go past the max
					if (firstDisplayedItem == totalSize - lines - 1 && y_offset < GetDisplayRemainder() - actualItemHeight)
						y_offset = GetDisplayRemainder() - actualItemHeight;
				} else if (firstDisplayedItem == totalSize - lines)
					y_offset = 0;
			}

			selectedItem = NO_ITEM;
			mUpdate = 1;
			scrollingSpeed = 0; // prevent kinetic scrolling when using fast scroll
			break;
		}

		// Provide some debounce on initial touches
		if (selectedItem != NO_ITEM && abs(y - lastY) < touchDebounce) {
			mUpdate = 1;
			break;
		}

		selectedItem = NO_ITEM; // nothing is selected because we dragged too far
		// Handle scrolling
		if (hasScroll) {
			y_offset += y - lastY; // adjust the scrolling offset based on the difference between the starting touch and the current touch
			last2Y = lastY; // keep track of previous y locations so that we can tell how fast to scroll for kinetic scrolling
			lastY = y; // update last touch to the current touch so we can tell how far and what direction we scroll for the next touch event

			HandleScrolling();
		} else
			y_offset = 0;
		mUpdate = 1;
		break;

	case TOUCH_RELEASE:
		fastScroll = 0;
		if (selectedItem != NO_ITEM) {
			// We've selected an item!
			NotifySelect(selectedItem);
			mUpdate = 1;

			DataManager::Vibrate("tw_button_vibrate");
			selectedItem = NO_ITEM;
		} else {
			// Start kinetic scrolling
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

void GUIScrollList::HandleScrolling()
{
	// handle dragging downward, scrolling upward
	// the offset should always be <= 0 and > -actualItemHeight, adjust the first display row and offset as needed
	while(firstDisplayedItem && y_offset > 0) {
		firstDisplayedItem--;
		y_offset -= actualItemHeight;
	}
	if (firstDisplayedItem == 0 && y_offset > 0) {
		y_offset = 0; // user kept dragging downward past the top of the list, so always reset the offset to 0 since we can't scroll any further in this direction
		scrollingSpeed = 0; // stop kinetic scrolling
	}

	// handle dragging upward, scrolling downward
	int totalSize = GetItemCount();
	int lines = GetDisplayItemCount(); // number of full lines our list can display at once
	int bottom_offset = GetDisplayRemainder() - actualItemHeight; // extra display area that can display a partial line for per pixel scrolling

	// the offset should always be <= 0 and > -actualItemHeight, adjust the first display row and offset as needed
	while (firstDisplayedItem + lines + (bottom_offset ? 1 : 0) < totalSize && abs(y_offset) > actualItemHeight) {
		firstDisplayedItem++;
		y_offset += actualItemHeight;
	}
	// Check if we dragged too far, set the list at the bottom and adjust offset as needed
	if (bottom_offset != 0 && firstDisplayedItem + lines + 1 >= totalSize && y_offset <= bottom_offset) {
		firstDisplayedItem = totalSize - lines - 1;
		y_offset = bottom_offset;
		scrollingSpeed = 0; // stop kinetic scrolling
	} else if (firstDisplayedItem + lines >= totalSize && y_offset < 0) {
		firstDisplayedItem = totalSize - lines;
		y_offset = 0;
		scrollingSpeed = 0; // stop kinetic scrolling
	}
}

int GUIScrollList::GetDisplayItemCount()
{
	return (mRenderH - mHeaderH) / (actualItemHeight);
}

int GUIScrollList::GetDisplayRemainder()
{
	return (mRenderH - mHeaderH) % actualItemHeight;
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
			firstDisplayedItem = 0;
			y_offset = 0;
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
		scrollingSpeed = 0; // stop kinetic scrolling on page changes
		mUpdate = 1;
	}
}
