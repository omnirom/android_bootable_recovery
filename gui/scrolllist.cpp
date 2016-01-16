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

const float SCROLLING_SPEED_DECREMENT = 0.9; // friction
const int SCROLLING_FLOOR = 2; // minimum pixels for scrolling to stop

GUIScrollList::GUIScrollList(xml_node<>* node) : GUIObject(node)
{
	xml_attribute<>* attr;
	xml_node<>* child;

	firstDisplayedItem = mItemSpacing = mFontHeight = mSeparatorH = y_offset = scrollingSpeed = 0;
	maxIconWidth = maxIconHeight =  mHeaderIconHeight = mHeaderIconWidth = 0;
	mHeaderSeparatorH = mHeaderH = actualItemHeight = 0;
	mHeaderIsStatic = false;
	mBackground = mHeaderIcon = NULL;
	mFont = NULL;
	mFastScrollW = mFastScrollLineW = mFastScrollRectW = mFastScrollRectH = 0;
	mFastScrollRectCurrentY = mFastScrollRectCurrentH = mFastScrollRectTouchY = 0;
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
	allowSelection = true;
	selectedItem = NO_ITEM;

	// Load header text
	// note: node can be NULL for the emergency console
	child = node ? node->first_node("text") : NULL;
	if (child)  mHeaderText = child->value();
	// Simple way to check for static state
	mLastHeaderValue = gui_parse_text(mHeaderText);
	mHeaderIsStatic = (mLastHeaderValue == mHeaderText);

	mHighlightColor = LoadAttrColor(FindNode(node, "highlight"), "color", &hasHighlightColor);

	child = FindNode(node, "background");
	if (child)
	{
		mBackground = LoadAttrImage(child, "resource");
		mBackgroundColor = LoadAttrColor(child, "color");
	}

	// Load the placement
	LoadPlacement(FindNode(node, "placement"), &mRenderX, &mRenderY, &mRenderW, &mRenderH);
	SetActionPos(mRenderX, mRenderY, mRenderW, mRenderH);

	// Load the font, and possibly override the color
	child = FindNode(node, "font");
	if (child)
	{
		mFont = LoadAttrFont(child, "resource");
		mFontColor = LoadAttrColor(child, "color");
		mFontHighlightColor = LoadAttrColor(child, "highlightcolor", mFontColor);
		mItemSpacing = LoadAttrIntScaleY(child, "spacing");
	}

	// Load the separator if it exists
	child = FindNode(node, "separator");
	if (child)
	{
		mSeparatorColor = LoadAttrColor(child, "color");
		mSeparatorH = LoadAttrIntScaleY(child, "height");
	}

	// Fast scroll
	child = FindNode(node, "fastscroll");
	if (child)
	{
		mFastScrollLineColor = LoadAttrColor(child, "linecolor");
		mFastScrollRectColor = LoadAttrColor(child, "rectcolor");

		mFastScrollW = LoadAttrIntScaleX(child, "w");
		mFastScrollLineW = LoadAttrIntScaleX(child, "linew");
		mFastScrollRectW = LoadAttrIntScaleX(child, "rectw");
		mFastScrollRectH = LoadAttrIntScaleY(child, "recth");
	}

	// Retrieve the line height
	mFontHeight = mFont->GetHeight();
	actualItemHeight = mFontHeight + mItemSpacing + mSeparatorH;

	// Load the header if it exists
	child = FindNode(node, "header");
	if (child)
	{
		mHeaderH = mFontHeight;
		mHeaderIcon = LoadAttrImage(child, "icon");
		mHeaderBackgroundColor = LoadAttrColor(child, "background", mBackgroundColor);
		mHeaderFontColor = LoadAttrColor(child, "textcolor", mFontColor);
		mHeaderSeparatorColor = LoadAttrColor(child, "separatorcolor", mSeparatorColor);
		mHeaderSeparatorH = LoadAttrIntScaleY(child, "separatorheight", mSeparatorH);

		if (mHeaderIcon && mHeaderIcon->GetResource())
		{
			mHeaderIconWidth = mHeaderIcon->GetWidth();
			mHeaderIconHeight = mHeaderIcon->GetHeight();
			if (mHeaderIconHeight > mHeaderH)
				mHeaderH = mHeaderIconHeight;
			if (mHeaderIconWidth > maxIconWidth)
				maxIconWidth = mHeaderIconWidth;
		}

		mHeaderH += mItemSpacing + mHeaderSeparatorH;
		if (mHeaderH < actualItemHeight)
			mHeaderH = actualItemHeight;
	}

	if (actualItemHeight / 3 > 6)
		touchDebounce = actualItemHeight / 3;
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
		if (mHeaderH > 0 && actualItemHeight > mHeaderH)
			mHeaderH = actualItemHeight;
	}
}

void GUIScrollList::SetVisibleListLocation(size_t list_index)
{
	// This will make sure that the item indicated by list_index is visible on the screen
	size_t lines = GetDisplayItemCount();

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
		if (firstDisplayedItem < 0)
			firstDisplayedItem = 0;
	}
	scrollingSpeed = 0; // stop kinetic scrolling on setting visible location
	mUpdate = 1;
}

int GUIScrollList::Render(void)
{
	if(!isConditionTrue())
		return 0;

	// First step, fill background
	gr_color(mBackgroundColor.red, mBackgroundColor.green, mBackgroundColor.blue, mBackgroundColor.alpha);
	gr_fill(mRenderX, mRenderY + mHeaderH, mRenderW, mRenderH - mHeaderH);

	// don't paint outside of the box
	gr_clip(mRenderX, mRenderY, mRenderW, mRenderH);

	// Next, render the background resource (if it exists)
	if (mBackground && mBackground->GetResource())
	{
		int BackgroundW = mBackground->GetWidth();
		int BackgroundH = mBackground->GetHeight();
		int BackgroundX = mRenderX + ((mRenderW - BackgroundW) / 2);
		int BackgroundY = mRenderY + ((mRenderH - BackgroundH) / 2);
		gr_blit(mBackground->GetResource(), 0, 0, BackgroundW, BackgroundH, BackgroundX, BackgroundY);
	}

	// This tells us how many full lines we can actually render
	size_t lines = GetDisplayItemCount();

	size_t listSize = GetItemCount();
	int listW = mRenderW; // this is only used for the separators - the list items are rendered in the full width of the list

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

	int yPos = mRenderY + mHeaderH + y_offset;

	// render all visible items
	for (size_t line = 0; line < lines; line++)
	{
		size_t itemindex = line + firstDisplayedItem;
		if (itemindex >= listSize)
			break;

		RenderItem(itemindex, yPos, itemindex == selectedItem);

		// Add the separator
		gr_color(mSeparatorColor.red, mSeparatorColor.green, mSeparatorColor.blue, mSeparatorColor.alpha);
		gr_fill(mRenderX, yPos + actualItemHeight - mSeparatorH, listW, mSeparatorH);

		// Move the yPos
		yPos += actualItemHeight;
	}

	// Render the Header (last so that it overwrites the top most row for per pixel scrolling)
	yPos = mRenderY;
	if (mHeaderH > 0) {
		// First step, fill background
		gr_color(mHeaderBackgroundColor.red, mHeaderBackgroundColor.green, mHeaderBackgroundColor.blue, mHeaderBackgroundColor.alpha);
		gr_fill(mRenderX, mRenderY, mRenderW, mHeaderH);

		int IconOffsetX = 0;

		// render the icon if it exists
		if (mHeaderIcon && mHeaderIcon->GetResource())
		{
			gr_blit(mHeaderIcon->GetResource(), 0, 0, mHeaderIconWidth, mHeaderIconHeight, mRenderX + ((mHeaderIconWidth - maxIconWidth) / 2), (yPos + (int)((mHeaderH - mHeaderIconHeight) / 2)));
			IconOffsetX = maxIconWidth;
		}

		// render the text
		gr_color(mHeaderFontColor.red, mHeaderFontColor.green, mHeaderFontColor.blue, mHeaderFontColor.alpha);
		gr_textEx_scaleW(mRenderX + IconOffsetX + 5, yPos + (int)(mHeaderH / 2), mLastHeaderValue.c_str(), mFont->GetResource(), mRenderW, TEXT_ONLY_RIGHT, 0);

		// Add the separator
		gr_color(mHeaderSeparatorColor.red, mHeaderSeparatorColor.green, mHeaderSeparatorColor.blue, mHeaderSeparatorColor.alpha);
		gr_fill(mRenderX, yPos + mHeaderH - mHeaderSeparatorH, mRenderW, mHeaderSeparatorH);
	}

	// reset clipping
	gr_noclip();

	// render fast scroll
	if (hasScroll) {
		int fWidth = mRenderW - listW;
		int fHeight = mRenderH - mHeaderH;
		int centerX = listW + mRenderX + fWidth / 2;

		// first determine the total list height and where we are in the list
		int totalHeight = GetItemCount() * actualItemHeight; // total height of the full list in pixels
		int topPos = firstDisplayedItem * actualItemHeight - y_offset;

		// now scale it proportionally to the scrollbar height
		int boxH = fHeight * fHeight / totalHeight; // proportional height of the displayed portion
		boxH = std::max(boxH, mFastScrollRectH); // but keep a minimum height
		int boxY = (fHeight - boxH) * topPos / (totalHeight - fHeight); // pixels relative to top of list
		int boxW = mFastScrollRectW;

		int x = centerX - boxW / 2;
		int y = mRenderY + mHeaderH + boxY;

		// line above and below box (needs to be split because box can be transparent)
		gr_color(mFastScrollLineColor.red, mFastScrollLineColor.green, mFastScrollLineColor.blue, mFastScrollLineColor.alpha);
		gr_fill(centerX - mFastScrollLineW / 2, mRenderY + mHeaderH, mFastScrollLineW, boxY);
		gr_fill(centerX - mFastScrollLineW / 2, y + boxH, mFastScrollLineW, fHeight - boxY - boxH);

		// box
		gr_color(mFastScrollRectColor.red, mFastScrollRectColor.green, mFastScrollRectColor.blue, mFastScrollRectColor.alpha);
		gr_fill(x, y, boxW, boxH);

		mFastScrollRectCurrentY = boxY;
		mFastScrollRectCurrentH = boxH;
	}
	mUpdate = 0;
	return 0;
}

void GUIScrollList::RenderItem(size_t itemindex __unused, int yPos, bool selected)
{
	RenderStdItem(yPos, selected, NULL, "implement RenderItem!");
}

void GUIScrollList::RenderStdItem(int yPos, bool selected, ImageResource* icon, const char* text, int iconAndTextH)
{
	if (hasHighlightColor && selected) {
		// Highlight the item background of the selected item
		gr_color(mHighlightColor.red, mHighlightColor.green, mHighlightColor.blue, mHighlightColor.alpha);
		gr_fill(mRenderX, yPos, mRenderW, actualItemHeight);
	}

	if (selected) {
		// Use the highlight color for the font
		gr_color(mFontHighlightColor.red, mFontHighlightColor.green, mFontHighlightColor.blue, mFontHighlightColor.alpha);
	} else {
		// Set the color for the font
		gr_color(mFontColor.red, mFontColor.green, mFontColor.blue, mFontColor.alpha);
	}

	if (!iconAndTextH)
		iconAndTextH = actualItemHeight;

	// render icon
	if (icon && icon->GetResource()) {
		int iconH = icon->GetHeight();
		int iconW = icon->GetWidth();
		int iconY = yPos + (iconAndTextH - iconH) / 2;
		int iconX = mRenderX + (maxIconWidth - iconW) / 2;
		gr_blit(icon->GetResource(), 0, 0, iconW, iconH, iconX, iconY);
	}

	// render label text
	int textX = mRenderX + maxIconWidth + 5;
	int textY = yPos + (iconAndTextH / 2);
	gr_textEx_scaleW(textX, textY, text, mFont->GetResource(), mRenderW, TEXT_ONLY_RIGHT, 0);
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
	// maximum number of items to scroll per update
	float maxItemsScrolledPerFrame = std::max(2.5, float(GetDisplayItemCount() / 4) + 0.5);

	int maxScrollDistance = actualItemHeight * maxItemsScrolledPerFrame;
	int oldScrollingSpeed = scrollingSpeed;
	if (scrollingSpeed == 0) {
		// Do nothing
		return 0;
	} else if (scrollingSpeed > 0) {
		if (scrollingSpeed < maxScrollDistance)
			y_offset += scrollingSpeed;
		else
			y_offset += maxScrollDistance;
		scrollingSpeed *= SCROLLING_SPEED_DECREMENT;
		if (scrollingSpeed == oldScrollingSpeed)
			--scrollingSpeed;
	} else if (scrollingSpeed < 0) {
		if (abs(scrollingSpeed) < maxScrollDistance)
			y_offset += scrollingSpeed;
		else
			y_offset -= maxScrollDistance;
		scrollingSpeed *= SCROLLING_SPEED_DECREMENT;
		if (scrollingSpeed == oldScrollingSpeed)
			++scrollingSpeed;
	}
	if (abs(scrollingSpeed) < SCROLLING_FLOOR)
		scrollingSpeed = 0;
	HandleScrolling();
	mUpdate = 1;

	return 0;
}

size_t GUIScrollList::HitTestItem(int x __unused, int y)
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
		if (hasScroll && x >= mRenderX + mRenderW - mFastScrollW) {
			fastScroll = 1; // Initial touch is in the fast scroll region
			int fastScrollBoxTop = mFastScrollRectCurrentY + mRenderY + mHeaderH;
			int fastScrollBoxBottom = fastScrollBoxTop + mFastScrollRectCurrentH;
			if (y >= fastScrollBoxTop && y < fastScrollBoxBottom)
				// user grabbed the fastscroll bar
				// try to keep the initially touched part of the scrollbar under the finger
				mFastScrollRectTouchY = y - fastScrollBoxTop;
			else
				// user tapped outside the fastscroll bar
				// center fastscroll rect on the initial touch position
				mFastScrollRectTouchY = mFastScrollRectCurrentH / 2;
		}

		if (scrollingSpeed != 0) {
			selectedItem = NO_ITEM; // this allows the user to tap the list to stop the scrolling without selecting the item they tap
			scrollingSpeed = 0; // stop scrolling on a new touch
		} else if (!fastScroll && allowSelection) {
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
			int relY = y - mRenderY - mHeaderH; // touch position relative to window
			int windowH = mRenderH - mHeaderH;
			int totalHeight = GetItemCount() * actualItemHeight; // total height of the full list in pixels

			// calculate new top position of the fastscroll bar relative to window
			int newY = relY - mFastScrollRectTouchY;
			// keep it fully inside the list
			newY = std::min(std::max(newY, 0), windowH - mFastScrollRectCurrentH);

			// now compute the new scroll position for the list
			int newTopPos = newY * (totalHeight - windowH) / (windowH - mFastScrollRectCurrentH); // new top pixel of list
			newTopPos = std::min(newTopPos, totalHeight - windowH); // account for rounding errors
			firstDisplayedItem = newTopPos / actualItemHeight;
			y_offset = - newTopPos % actualItemHeight;

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
		if (fastScroll)
			mUpdate = 1; // get rid of touch effects on the fastscroll bar
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
			if (abs(scrollingSpeed) < touchDebounce)
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

bool GUIScrollList::AddLines(std::vector<std::string>* origText, std::vector<std::string>* origColor, size_t* lastCount, std::vector<std::string>* rText, std::vector<std::string>* rColor)
{
	if (*lastCount == origText->size())
		return false; // nothing to add

	size_t prevCount = *lastCount;
	*lastCount = origText->size();

	// Due to word wrap, figure out what / how the newly added text needs to be added to the render vector that is word wrapped
	// Note, that multiple consoles on different GUI pages may be different widths or use different fonts, so the word wrapping
	// may different in different console windows
	for (size_t i = prevCount; i < *lastCount; i++) {
		string curr_line = origText->at(i);
		string curr_color;
		if (origColor)
			curr_color = origColor->at(i);
		for(;;) {
			size_t line_char_width = gr_ttf_maxExW(curr_line.c_str(), mFont->GetResource(), mRenderW);
			if (line_char_width < curr_line.size()) {
				//string left = curr_line.substr(0, line_char_width);
				size_t wrap_pos = curr_line.find_last_of(" ,./:-_;", line_char_width - 1);
				if (wrap_pos == string::npos)
					wrap_pos = line_char_width;
				else if (wrap_pos < line_char_width - 1)
					wrap_pos++;
				rText->push_back(curr_line.substr(0, wrap_pos));
				if (origColor)
					rColor->push_back(curr_color);
				curr_line = curr_line.substr(wrap_pos);
				/* After word wrapping, delete any leading spaces. Note that the word wrapping is not smart enough to know not
				 * to wrap in the middle of something like ... so some of the ... could appear on the following line. */
				curr_line.erase(0, curr_line.find_first_not_of(" "));
			} else {
				rText->push_back(curr_line);
				if (origColor)
					rColor->push_back(curr_color);
				break;
			}
		}
	}
	return true;
}
