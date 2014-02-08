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

#include <linux/input.h>
#include <pthread.h>
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
#include <dirent.h>
#include <ctype.h>

#include <algorithm>

extern "C" {
#include "../twcommon.h"
#include "../minuitwrp/minui.h"
}

#include "rapidxml.hpp"
#include "objects.hpp"
#include "../data.hpp"
#include "../twrp-functions.hpp"
#include "../partitions.hpp"

#define SCROLLING_SPEED_DECREMENT 6
#define SCROLLING_FLOOR 10
#define SCROLLING_MULTIPLIER 6

GUIPartitionList::GUIPartitionList(xml_node<>* node) : GUIObject(node)
{
	xml_attribute<>* attr;
	xml_node<>* child;
	int header_separator_color_specified = 0, header_separator_height_specified = 0, header_text_color_specified = 0, header_background_color_specified = 0;

	mStart = mLineSpacing = startY = mFontHeight = mSeparatorH = scrollingY = scrollingSpeed = 0;
	mIconWidth = mIconHeight = mSelectedIconHeight = mSelectedIconWidth = mUnselectedIconHeight = mUnselectedIconWidth = mHeaderIconHeight = mHeaderIconWidth = 0;
	mHeaderSeparatorH = mLineHeight = mHeaderIsStatic = mHeaderH = actualLineHeight = 0;
	mIconSelected = mIconUnselected = mBackground = mFont = mHeaderIcon = NULL;
	mBackgroundX = mBackgroundY = mBackgroundW = mBackgroundH = 0;
	mFastScrollW = mFastScrollLineW = mFastScrollRectW = mFastScrollRectH = 0;
	mFastScrollRectX = mFastScrollRectY = -1;
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
	updateList = false;
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
	mLastValue = gui_parse_text(mHeaderText);
	if (mLastValue != mHeaderText)
		mHeaderIsStatic = 0;
	else
		mHeaderIsStatic = -1;

	child = node->first_node("icon");
	if (child)
	{
		attr = child->first_attribute("selected");
		if (attr)
			mIconSelected = PageManager::FindResource(attr->value());
		attr = child->first_attribute("unselected");
		if (attr)
			mIconUnselected = PageManager::FindResource(attr->value());
	}
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

	// Handle the result variable
	child = node->first_node("data");
	if (child)
	{
		attr = child->first_attribute("name");
		if (attr)
			mVariable = attr->value();
		attr = child->first_attribute("selectedlist");
		if (attr)
			selectedList = attr->value();
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
	gr_getFontDetails(mFont ? mFont->GetResource() : NULL, &mFontHeight, NULL);
	mLineHeight = mFontHeight;
	mHeaderH = mFontHeight;

	if (mIconSelected && mIconSelected->GetResource())
	{
		mSelectedIconWidth = gr_get_width(mIconSelected->GetResource());
		mSelectedIconHeight = gr_get_height(mIconSelected->GetResource());
		if (mSelectedIconHeight > (int)mLineHeight)
			mLineHeight = mSelectedIconHeight;
		mIconWidth = mSelectedIconWidth;
	}

	if (mIconUnselected && mIconUnselected->GetResource())
	{
		mUnselectedIconWidth = gr_get_width(mIconUnselected->GetResource());
		mUnselectedIconHeight = gr_get_height(mIconUnselected->GetResource());
		if (mUnselectedIconHeight > (int)mLineHeight)
			mLineHeight = mUnselectedIconHeight;
		if (mUnselectedIconWidth > mIconWidth)
			mIconWidth = mUnselectedIconWidth;
	}

	if (mHeaderIcon && mHeaderIcon->GetResource())
	{
		mHeaderIconWidth = gr_get_width(mHeaderIcon->GetResource());
		mHeaderIconHeight = gr_get_height(mHeaderIcon->GetResource());
		if (mHeaderIconHeight > mHeaderH)
			mHeaderH = mHeaderIconHeight;
		if (mHeaderIconWidth > mIconWidth)
			mIconWidth = mHeaderIconWidth;
	}

	mHeaderH += mLineSpacing + mHeaderSeparatorH;
	actualLineHeight = mLineHeight + mLineSpacing + mSeparatorH;
	if (mHeaderH < actualLineHeight)
		mHeaderH = actualLineHeight;

	if (actualLineHeight / 3 > 6)
		touchDebounce = actualLineHeight / 3;

	if (mBackground && mBackground->GetResource())
	{
		mBackgroundW = gr_get_width(mBackground->GetResource());
		mBackgroundH = gr_get_height(mBackground->GetResource());
	}

	child = node->first_node("listtype");
	if (child) {
		attr = child->first_attribute("name");
		if (attr) {
			ListType = attr->value();
			PartitionManager.Get_Partition_List(ListType, &mList);
		} else {
			mList.clear();
			LOGERR("No partition listtype name specified for partitionlist GUI element\n");
			return;
		}
	} else {
		mList.clear();
		LOGERR("No partition listtype specified for partitionlist GUI element\n");
		return;
	}
}

GUIPartitionList::~GUIPartitionList()
{
}

int GUIPartitionList::Render(void)
{
	if(!isConditionTrue())
		return 0;

	// First step, fill background
	gr_color(mBackgroundColor.red, mBackgroundColor.green, mBackgroundColor.blue, 255);
	gr_fill(mRenderX, mRenderY + mHeaderH, mRenderW, mRenderH - mHeaderH);

	// Next, render the background resource (if it exists)
	if (mBackground && mBackground->GetResource())
	{
		mBackgroundX = mRenderX + ((mRenderW - mBackgroundW) / 2);
		mBackgroundY = mRenderY + ((mRenderH - mBackgroundH) / 2);
		gr_blit(mBackground->GetResource(), 0, 0, mBackgroundW, mBackgroundH, mBackgroundX, mBackgroundY);
	}

	// This tells us how many lines we can actually render
	int lines = (mRenderH - mHeaderH) / (actualLineHeight);
	int line;

	if (updateList) {
		mList.clear();
		PartitionManager.Get_Partition_List(ListType, &mList);
		updateList = false;
		if (ListType == "backup")
			MatchList();
	}

	int listSize = mList.size();
	int listW = mRenderW;

	if (listSize < lines) {
		lines = listSize;
		scrollingY = 0;
		mFastScrollRectX = mFastScrollRectY = -1;
	} else {
		lines++;
		if (lines < listSize)
			lines++;
		if (listSize >= lines)
			listW -= mFastScrollW; // space for fast scrollbar
		else
			mFastScrollRectX = mFastScrollRectY = -1; // no fast scrollbar
	}

	void* fontResource = NULL;
	if (mFont)  fontResource = mFont->GetResource();

	int yPos = mRenderY + mHeaderH + scrollingY;
	int fontOffsetY = (int)((actualLineHeight - mFontHeight) / 2);
	int currentIconHeight = 0, currentIconWidth = 0;
	int currentIconOffsetY = 0, currentIconOffsetX = 0;
	int UnselectedIconOffsetY = (int)((actualLineHeight - mUnselectedIconHeight) / 2), SelectedIconOffsetY = (int)((actualLineHeight - mSelectedIconHeight) / 2);
	int UnselectedIconOffsetX = (mIconWidth - mUnselectedIconWidth) / 2, SelectedIconOffsetX = (mIconWidth - mSelectedIconWidth) / 2;
	int actualSelection = mStart;

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

	for (line = 0; line < lines; line++)
	{
		Resource* icon;
		std::string label;

		if (line + mStart >= listSize)
			continue;

		label = mList.at(line + mStart).Display_Name;
		if (isHighlighted && hasFontHighlightColor && line + mStart == actualSelection) {
			// Use the highlight color for the font
			gr_color(mFontHighlightColor.red, mFontHighlightColor.green, mFontHighlightColor.blue, 255);
		} else {
			// Set the color for the font
			gr_color(mFontColor.red, mFontColor.green, mFontColor.blue, 255);
		}

		if (mList.at(line + mStart).selected != 0)
		{
			icon = mIconSelected;
			currentIconHeight = mSelectedIconHeight;
			currentIconWidth = mSelectedIconWidth;
			currentIconOffsetY = SelectedIconOffsetY;
			currentIconOffsetX = SelectedIconOffsetX;
		}
		else
		{
			icon = mIconUnselected;
			currentIconHeight = mSelectedIconHeight;
			currentIconWidth = mSelectedIconWidth;
			currentIconOffsetY = SelectedIconOffsetY;
			currentIconOffsetX = SelectedIconOffsetX;
		}

		if (icon && icon->GetResource())
		{
			int rect_y = 0, image_y = (yPos + currentIconOffsetY);
			if (image_y + currentIconHeight > mRenderY + mRenderH)
				rect_y = mRenderY + mRenderH - image_y;
			else
				rect_y = currentIconHeight;
			gr_blit(icon->GetResource(), 0, 0, currentIconWidth, rect_y, mRenderX + currentIconOffsetX, image_y);
		}
		gr_textExWH(mRenderX + mIconWidth + 5, yPos + fontOffsetY, label.c_str(), fontResource, mRenderX + listW, mRenderY + mRenderH);

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
			gr_blit(headerIcon->GetResource(), 0, 0, mHeaderIconWidth, mHeaderIconHeight, mRenderX + ((mHeaderIconWidth - mIconWidth) / 2), (yPos + (int)((mHeaderH - mHeaderIconHeight) / 2)));
			mIconOffsetX = mIconWidth;
		}

		// render the text
		gr_color(mHeaderFontColor.red, mHeaderFontColor.green, mHeaderFontColor.blue, 255);
		gr_textExWH(mRenderX + mIconOffsetX + 5, yPos + (int)((mHeaderH - mFontHeight) / 2), mLastValue.c_str(), fontResource, mRenderX + mRenderW, mRenderY + mRenderH);

		// Add the separator
		gr_color(mHeaderSeparatorColor.red, mHeaderSeparatorColor.green, mHeaderSeparatorColor.blue, 255);
		gr_fill(mRenderX, yPos + mHeaderH - mHeaderSeparatorH, mRenderW, mHeaderSeparatorH);
	}

	// render fast scroll
	lines = (mRenderH - mHeaderH) / (actualLineHeight);
	if(mFastScrollW > 0 && listSize > lines)
	{
		int startX = listW + mRenderX;
		int fWidth = mRenderW - listW;
		int fHeight = mRenderH - mHeaderH;

		// line
		gr_color(mFastScrollLineColor.red, mFastScrollLineColor.green, mFastScrollLineColor.blue, 255);
		gr_fill(startX + fWidth/2, mRenderY + mHeaderH, mFastScrollLineW, mRenderH - mHeaderH);

		// rect
		int pct = ((mStart*actualLineHeight - scrollingY)*100)/((listSize)*actualLineHeight-lines*actualLineHeight);
		mFastScrollRectX = startX + (fWidth - mFastScrollRectW)/2;
		mFastScrollRectY = mRenderY+mHeaderH + ((fHeight - mFastScrollRectH)*pct)/100;

		gr_color(mFastScrollRectColor.red, mFastScrollRectColor.green, mFastScrollRectColor.blue, 255);
		gr_fill(mFastScrollRectX, mFastScrollRectY, mFastScrollRectW, mFastScrollRectH);
	}

	mUpdate = 0;
	return 0;
}

int GUIPartitionList::Update(void)
{
	if(!isConditionTrue())
		return 0;

	if (!mHeaderIsStatic) {
		std::string newValue = gui_parse_text(mHeaderText);
		if (mLastValue != newValue) {
			mLastValue = newValue;
			mUpdate = 1;
		}
	}

	// Check for changes in mount points if the list type is mount and update the list and render if needed
	if (ListType == "mount") {
		int listSize = mList.size();
		for (int i = 0; i < listSize; i++) {
			if (PartitionManager.Is_Mounted_By_Path(mList.at(i).Mount_Point) && !mList.at(i).selected) {
				mList.at(i).selected = 1;
				mUpdate = 1;
			} else if (!PartitionManager.Is_Mounted_By_Path(mList.at(i).Mount_Point) && mList.at(i).selected) {
				mList.at(i).selected = 0;
				mUpdate = 1;
			}
		}
	}

	if (mUpdate)
	{
		mUpdate = 0;
		if (Render() == 0)
			return 2;
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
		int totalSize = mList.size();
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

int GUIPartitionList::GetSelection(int x, int y)
{
	// We only care about y position
	if (y < mRenderY || y - mRenderY <= mHeaderH || y - mRenderY > mRenderH) return -1;
	return (y - mRenderY - mHeaderH);
}

int GUIPartitionList::NotifyTouch(TOUCH_STATE state, int x, int y)
{
	if(!isConditionTrue())
		return -1;

	static int lastY = 0, last2Y = 0;
	int selection = 0;

	switch (state)
	{
	case TOUCH_START:
		if (scrollingSpeed != 0)
			startSelection = -1;
		else
			startSelection = GetSelection(x,y);
		isHighlighted = (startSelection > -1);
		if (isHighlighted)
			mUpdate = 1;
		startY = lastY = last2Y = y;
		scrollingSpeed = 0;
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
		if(mFastScrollRectX != -1 && x >= mRenderX + mRenderW - mFastScrollW)
		{
			int pct = ((y-mRenderY-mHeaderH)*100)/(mRenderH-mHeaderH);
			int totalSize = mList.size();
			int lines = (mRenderH - mHeaderH) / (actualLineHeight);

			float l = float((totalSize-lines)*pct)/100;
			if(l + lines >= totalSize)
			{
				mStart = totalSize - lines;
				scrollingY = 0;
			}
			else
			{
				mStart = l;
				scrollingY = -(l - int(l))*actualLineHeight;
			}

			startSelection = -1;
			mUpdate = 1;
			scrollingSpeed = 0;
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
			int totalSize = mList.size();
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
		if (startSelection >= 0)
		{
			// We've selected an item!
			int listSize = mList.size();
			int selectY = scrollingY, actualSelection = mStart;

			// Move the selection to the proper place in the array
			while (selectY + actualLineHeight < startSelection) {
				selectY += actualLineHeight;
				actualSelection++;
			}

			if (actualSelection < listSize && ListType == "mount") {
				DataManager::Vibrate("tw_button_vibrate");

				if (!mList.at(actualSelection).selected) {
					if (PartitionManager.Mount_By_Path(mList.at(actualSelection).Mount_Point, true)) {
						mList.at(actualSelection).selected = 1;
						mUpdate = 1;
					}
				} else {
					if (PartitionManager.UnMount_By_Path(mList.at(actualSelection).Mount_Point, true)) {
						mList.at(actualSelection).selected = 0;
						mUpdate = 1;
					}
				}
			} else if (actualSelection < listSize && !mVariable.empty()) {
				DataManager::Vibrate("tw_button_vibrate");

				if (ListType == "storage") {
					int i;
					std::string str = mList.at(actualSelection).Mount_Point;
					bool update_size = false;
					TWPartition* Part = PartitionManager.Find_Partition_By_Path(str);
					if (Part == NULL) {
						LOGERR("Unable to locate partition for '%s'\n", str.c_str());
						return 0;
					}
					if (!Part->Is_Mounted() && Part->Removable)
						update_size = true;
					if (!Part->Mount(true)) {
						// Do Nothing
					} else if (update_size && !Part->Update_Size(true)) {
						// Do Nothing
					} else {
						for (i=0; i<listSize; i++)
							mList.at(i).selected = 0;

						if (update_size) {
							char free_space[255];
							sprintf(free_space, "%llu", Part->Free / 1024 / 1024);
							mList.at(actualSelection).Display_Name = Part->Storage_Name + " (";
							mList.at(actualSelection).Display_Name += free_space;
							mList.at(actualSelection).Display_Name += "MB)";
						}
						mList.at(actualSelection).selected = 1;
						mUpdate = 1;
						
						DataManager::SetValue(mVariable, str);
					}
				} else {
					if (mList.at(actualSelection).selected)
						mList.at(actualSelection).selected = 0;
					else
						mList.at(actualSelection).selected = 1;

					int i;
					string variablelist;
					for (i=0; i<listSize; i++) {
						if (mList.at(i).selected) {
							variablelist += mList.at(i).Mount_Point + ";";
						}
					}

					mUpdate = 1;
					if (selectedList.empty())
						DataManager::SetValue(mVariable, variablelist);
					else
						DataManager::SetValue(selectedList, variablelist);
				}
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

int GUIPartitionList::NotifyVarChange(const std::string& varName, const std::string& value)
{
	GUIObject::NotifyVarChange(varName, value);

	if(!isConditionTrue())
		return 0;

	if (!mHeaderIsStatic) {
		std::string newValue = gui_parse_text(mHeaderText);
		if (mLastValue != newValue) {
			mLastValue = newValue;
			mStart = 0;
			scrollingY = 0;
			scrollingSpeed = 0;
			mUpdate = 1;
		}
	}
	if (varName == mVariable && !mUpdate)
	{
		if (ListType == "storage") {
			int i, listSize = mList.size(), selected_index = 0;

			currentValue = value;

			for (i=0; i<listSize; i++) {
				if (mList.at(i).Mount_Point == currentValue) {
					mList.at(i).selected = 1;
					selected_index = i;
				} else
					mList.at(i).selected = 0;
			}

			int lines = mRenderH / (mLineHeight + mLineSpacing);
			int line;

			if (selected_index > mStart + lines - 1) {
				mStart = selected_index;
			} else if (selected_index < mStart) {
				mStart = selected_index;
			}
		} else if (ListType == "backup") {
			MatchList();
		} else if (ListType == "restore") {
			updateList = true;
		}

		mUpdate = 1;
		return 0;
	}
	return 0;
}

int GUIPartitionList::SetRenderPos(int x, int y, int w /* = 0 */, int h /* = 0 */)
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

void GUIPartitionList::SetPageFocus(int inFocus)
{
	if (inFocus) {
		if (ListType == "storage") {
			int i, listSize = mList.size(), selected_index = 0;

			DataManager::GetValue(mVariable, currentValue);

			for (i=0; i<listSize; i++) {
				if (mList.at(i).Mount_Point == currentValue) {
					mList.at(i).selected = 1;
					selected_index = i;
				} else
					mList.at(i).selected = 0;
			}

			int lines = mRenderH / (mLineHeight + mLineSpacing);
			int line;

			if (selected_index > mStart + lines - 1) {
				mStart = selected_index;
			} else if (selected_index < mStart) {
				mStart = selected_index;
			}
		}
		updateList = true;
		mUpdate = 1;
	}
}

void GUIPartitionList::MatchList(void) {
	int i, listSize = mList.size();
	string variablelist, searchvalue;
	size_t pos;

	DataManager::GetValue(mVariable, variablelist);

	for (i=0; i<listSize; i++) {
		searchvalue = mList.at(i).Mount_Point + ";";
		pos = variablelist.find(searchvalue);
		if (pos != string::npos) {
			mList.at(i).selected = 1;
		} else {
			mList.at(i).selected = 0;
		}
	}
}
