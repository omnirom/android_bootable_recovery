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

GUIListBox::GUIListBox(xml_node<>* node) : GUIScrollList(node)
{
	xml_attribute<>* attr;
	xml_node<>* child;
	mIconSelected = mIconUnselected = NULL;
	int mSelectedIconWidth = 0, mSelectedIconHeight = 0, mUnselectedIconWidth = 0, mUnselectedIconHeight = 0, mIconWidth = 0, mIconHeight = 0;
	mUpdate = 0;

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
		if (mSelectedIconHeight > mIconHeight)
			mIconHeight = mSelectedIconHeight;
		mIconWidth = mSelectedIconWidth;
	}

	if (mIconUnselected && mIconUnselected->GetResource()) {
		mUnselectedIconWidth = gr_get_width(mIconUnselected->GetResource());
		mUnselectedIconHeight = gr_get_height(mIconUnselected->GetResource());
		if (mUnselectedIconHeight > mIconHeight)
			mIconHeight = mUnselectedIconHeight;
		if (mUnselectedIconWidth > mIconWidth)
			mIconWidth = mUnselectedIconWidth;
	}
	SetMaxIconSize(mIconWidth, mIconHeight);

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
	while (child) {
		ListData data;

		attr = child->first_attribute("name");
		if (!attr) return;
		data.displayName = attr->value();

		data.variableValue = child->value();
		if (child->value() == currentValue) {
			data.selected = 1;
		} else {
			data.selected = 0;
		}

		mList.push_back(data);

		child = child->next_sibling("listitem");
	}
}

GUIListBox::~GUIListBox()
{
}

int GUIListBox::Update(void)
{
	if(!isConditionTrue())
		return 0;

	GUIScrollList::Update();

	if (mUpdate) {
		mUpdate = 0;
		if (Render() == 0)
			return 2;
	}
	return 0;
}

int GUIListBox::NotifyVarChange(const std::string& varName, const std::string& value)
{
	if(!isConditionTrue())
		return 0;

	GUIScrollList::NotifyVarChange(varName, value);

	// Check to see if the variable that we are using to store the list selected value has been updated
	if (varName == mVariable) {
		int i, listSize = mList.size();

		currentValue = value;

		for (i = 0; i < listSize; i++) {
			if (mList.at(i).variableValue == currentValue) {
				mList.at(i).selected = 1;
				SetVisibleListLocation(i);
			} else
				mList.at(i).selected = 0;
		}
		mUpdate = 1;
		return 0;
	}
	return 0;
}

void GUIListBox::SetPageFocus(int inFocus)
{
	GUIScrollList::SetPageFocus(inFocus);
	if (inFocus) {
		DataManager::GetValue(mVariable, currentValue);
		NotifyVarChange(mVariable, currentValue);
	}
}

size_t GUIListBox::GetItemCount()
{
	return mList.size();
}

int GUIListBox::GetListItem(size_t item_index, Resource*& icon, std::string &text)
{
	text = mList.at(item_index).displayName;
	if (mList.at(item_index).selected)
		icon = mIconSelected;
	else
		icon = mIconUnselected;
	return 0;
}

void GUIListBox::NotifySelect(size_t item_selected)
{
	for (size_t i = 0; i < mList.size(); i++) {
		mList.at(i).selected = 0;
	}
	if (item_selected < mList.size()) {
		mList.at(item_selected).selected = 1;
		string str = mList.at(item_selected).variableValue;
		DataManager::SetValue(mVariable, str);
	}
	mUpdate = 1;
}
