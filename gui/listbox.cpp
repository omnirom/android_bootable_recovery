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
	mUpdate = 0;

	// Get the icons, if any
	child = FindNode(node, "icon");
	if (child) {
		mIconSelected = LoadAttrImage(child, "selected");
		mIconUnselected = LoadAttrImage(child, "unselected");
	}
	int iconWidth = std::max(mIconSelected->GetWidth(), mIconUnselected->GetWidth());
	int iconHeight = std::max(mIconSelected->GetHeight(), mIconUnselected->GetHeight());
	SetMaxIconSize(iconWidth, iconHeight);

	// Handle the result variable
	child = FindNode(node, "data");
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
	else
		allowSelection = false;		// allows using listbox as a read-only list

	// Get the data for the list
	child = FindNode(node, "listitem");
	if (!child) return;
	while (child) {
		ListData data;

		attr = child->first_attribute("name");
		if (!attr)
			continue;
		data.displayName = gui_parse_text(attr->value());
		data.variableValue = gui_parse_text(child->value());
		if (child->value() == currentValue) {
			data.selected = 1;
		} else {
			data.selected = 0;
		}
		data.action = NULL;
		xml_node<>* action = child->first_node("action");
		if (action) {
			data.action = new GUIAction(action);
			allowSelection = true;
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
	GUIScrollList::NotifyVarChange(varName, value);

	if(!isConditionTrue())
		return 0;

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

void GUIListBox::RenderItem(size_t itemindex, int yPos, bool selected)
{
	// note: the "selected" parameter above is for the currently touched item
	// don't confuse it with the more persistent "selected" flag per list item used below
	ImageResource* icon = mList.at(itemindex).selected ? mIconSelected : mIconUnselected;
	const std::string& text = mList.at(itemindex).displayName;

	RenderStdItem(yPos, selected, icon, text.c_str());
}

void GUIListBox::NotifySelect(size_t item_selected)
{
	for (size_t i = 0; i < mList.size(); i++) {
		mList.at(i).selected = 0;
	}
	if (item_selected < mList.size()) {
		ListData& data = mList.at(item_selected);
		data.selected = 1;
		string str = data.variableValue;	// [check] should this set currentValue instead?
		DataManager::SetValue(mVariable, str);
		if (data.action)
			data.action->doActions();
	}
	mUpdate = 1;
}
