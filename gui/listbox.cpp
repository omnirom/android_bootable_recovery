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
#include <sys/stat.h>
#include <dirent.h>

extern "C" {
#include "../twcommon.h"
#include "../minuitwrp/minui.h"
}

#include "rapidxml.hpp"
#include "objects.hpp"
#include "../data.hpp"

GUIListBox::GUIListBox(xml_node<>* node) : GUIObject(node)
{
	xml_attribute<>* attr;
	xml_node<>* child;
	mIconSelected = mIconUnselected = NULL;
	int mLineHeight = 0;
	int mSelectedIconWidth = 0, mSelectedIconHeight = 0, mUnselectedIconWidth = 0, mUnselectedIconHeight = 0, mIconWidth = 0, mIconHeight = 0;

	scrollList = new GUIScrollList(node);

	// Get the icons, if any
	child = node->first_node("icon");
	if (child) {
		attr = child->first_attribute("selected");
		if (attr)
			mIconSelected = PageManager::FindResource(attr->value());
		attr = child->first_attribute("unselected");
		if (attr)
			mIconUnselected = PageManager::FindResource(attr->value());
	}
	if (mIconSelected && mIconSelected->GetResource()) {
		mSelectedIconWidth = gr_get_width(mIconSelected->GetResource());
		mSelectedIconHeight = gr_get_height(mIconSelected->GetResource());
		if (mSelectedIconHeight > mLineHeight)
			mIconHeight = mSelectedIconHeight;
		mIconWidth = mSelectedIconWidth;
	}

	if (mIconUnselected && mIconUnselected->GetResource()) {
		mUnselectedIconWidth = gr_get_width(mIconUnselected->GetResource());
		mUnselectedIconHeight = gr_get_height(mIconUnselected->GetResource());
		if (mUnselectedIconHeight > mLineHeight)
			mIconHeight = mUnselectedIconHeight;
		if (mUnselectedIconWidth > mIconWidth)
			mIconWidth = mUnselectedIconWidth;
	}
	scrollList->SetMaxIconSize(mIconWidth, mIconHeight);

	// Handle the result variable
	child = node->first_node("data");
	if (child) {
		attr = child->first_attribute("name");
		if (attr)
			mVariable = attr->value();
		attr = child->first_attribute("default");
		if (attr)
			DataManager::SetValue(mVariable, attr->value());
		// Get the currently selected value for the list
		DataManager::GetValue(mVariable, currentValue);
	}

	// Get the data for the list
	child = node->first_node("listitem");
	if (!child) return;
	int index = 0;
	while (child) {
		ListData data;
		ScrollListData sldata;

		attr = child->first_attribute("name");
		if (!attr) return;
		data.displayName = attr->value();
		sldata.displayName = attr->value();
		sldata.list_index = index;

		data.variableValue = child->value();
		if (child->value() == currentValue) {
			data.selected = 1;
			sldata.displayResource = mIconSelected;
		} else {
			data.selected = 0;
			sldata.displayResource = mIconUnselected;
		}

		mList.push_back(data);
		scrollList->mList.push_back(sldata);
		index++;

		child = child->next_sibling("listitem");
	}

	// Load the placement
	LoadPlacement(node->first_node("placement"), &mRenderX, &mRenderY, &mRenderW, &mRenderH);
	SetActionPos(mRenderX, mRenderY, mRenderW, mRenderH);
}

GUIListBox::~GUIListBox()
{
	delete scrollList;
	if (mIconSelected)
		delete mIconSelected;
	if (mIconUnselected)
		delete mIconUnselected;
}

int GUIListBox::Render(void)
{
	if(!isConditionTrue())
		return 0;

	int listSize = mList.size();

	for (int index = 0; index < listSize; index++) {
		int slindex = index;
		// Locate the matching item in the scrollList if needed
		if (scrollList->mList.at(slindex).list_index != slindex) {
			for (slindex = 0; index < listSize; index++) {
				if (scrollList->mList.at(slindex).list_index == slindex)
					break;
			}
		}
		// Set the appropriate icon resource in the scrollList
		if (mList.at(index).selected)
			scrollList->mList.at(slindex).displayResource = mIconSelected;
		else
			scrollList->mList.at(slindex).displayResource = mIconUnselected;
	}
	mUpdate = 0;
	return scrollList->Render();
}

int GUIListBox::Update(void)
{
	if(!isConditionTrue())
		return 0;

	scrollList->Update();

	if (mUpdate || scrollList->mUpdate) {
		mUpdate = 0;
		if (Render() == 0)
			return 2;
	}

	return 0;
}

int GUIListBox::GetSelection(int x, int y)
{
	return scrollList->GetSelection(x, y);
}

int GUIListBox::NotifyTouch(TOUCH_STATE state, int x, int y)
{
	if(!isConditionTrue())
		return -1;

	scrollList->NotifyTouch(state, x, y);
	if (state == TOUCH_RELEASE && scrollList->itemChosen >= 0) {
		for (unsigned i = 0; i < mList.size(); i++) {
			mList.at(i).selected = 0;
		}
		string str = mList.at(scrollList->itemChosen).variableValue;
		mList.at(scrollList->itemChosen).selected = 1;
		DataManager::SetValue(mVariable, str);
		mUpdate = 1;
	}
	return 0;
}

int GUIListBox::NotifyVarChange(const std::string& varName, const std::string& value)
{
	GUIObject::NotifyVarChange(varName, value);

	if(!isConditionTrue())
		return 0;

	scrollList->NotifyVarChange(varName, value);

	// Check to see if the variable that we are using to store the list selected value has been updated
	if (varName == mVariable) {
		int i, listSize = mList.size();

		currentValue = value;

		for (i = 0; i < listSize; i++) {
			if (mList.at(i).variableValue == currentValue) {
				mList.at(i).selected = 1;
				scrollList->SetVisibleListLocation(i);
			} else
				mList.at(i).selected = 0;
		}

		mUpdate = 1;
		return 0;
	}
	return 0;
}

int GUIListBox::SetRenderPos(int x, int y, int w /* = 0 */, int h /* = 0 */)
{
	return scrollList->SetRenderPos(x, y, w, h);
}

void GUIListBox::SetPageFocus(int inFocus)
{
	if (inFocus) {
		DataManager::GetValue(mVariable, currentValue);
		NotifyVarChange(mVariable, currentValue);
		mUpdate = 1;
	}
	scrollList->SetPageFocus(inFocus);
}
