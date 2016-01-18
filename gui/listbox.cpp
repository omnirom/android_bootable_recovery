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
#include "pages.hpp"

extern std::vector<language_struct> Language_List;

GUIListBox::GUIListBox(xml_node<>* node) : GUIScrollList(node)
{
	xml_attribute<>* attr;
	xml_node<>* child;
	mIconSelected = mIconUnselected = NULL;
	mUpdate = 0;
	isCheckList = isTextParsed = false;

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
		if (mVariable == "tw_language") {
			std::vector<language_struct>::iterator iter;
			for (iter = Language_List.begin(); iter != Language_List.end(); iter++) {
				ListItem data;
				data.displayName = (*iter).displayvalue;
				data.variableValue = (*iter).filename;
				data.action = NULL;
				if (currentValue == (*iter).filename) {
					data.selected = 1;
					DataManager::SetValue("tw_language_display", (*iter).displayvalue);
				} else
					data.selected = 0;
				mListItems.push_back(data);
			}
		}
	}
	else
		allowSelection = false;		// allows using listbox as a read-only list or menu

	// Get the data for the list
	child = FindNode(node, "listitem");
	if (!child) return;
	while (child) {
		ListItem item;

		attr = child->first_attribute("name");
		if (!attr)
			continue;
		// We will parse display names when we get page focus to ensure that translating takes place
		item.displayName = attr->value();
		item.variableValue = gui_parse_text(child->value());
		item.selected = (child->value() == currentValue);
		item.action = NULL;
		xml_node<>* action = child->first_node("action");
		if (action) {
			item.action = new GUIAction(action);
			allowSelection = true;
		}
		xml_node<>* variable_name = child->first_node("data");
		if (variable_name) {
			attr = variable_name->first_attribute("variable");
			if (attr) {
				item.variableName = attr->value();
				item.selected = (DataManager::GetIntValue(item.variableName) != 0);
				allowSelection = true;
				isCheckList = true;
			}
		}

		LoadConditions(child, item.mConditions);

		mListItems.push_back(item);
		mVisibleItems.push_back(mListItems.size()-1);

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
		currentValue = value;
		mUpdate = 1;
	}

	std::vector<size_t> mVisibleItemsOld;
	std::swap(mVisibleItemsOld, mVisibleItems);
	for (size_t i = 0; i < mListItems.size(); i++) {
		ListItem& item = mListItems[i];
		// update per-item visibility condition
		bool itemVisible = UpdateConditions(item.mConditions, varName);
		if (itemVisible)
			mVisibleItems.push_back(i);

		if (isCheckList)
		{
			if (item.variableName == varName || varName.empty()) {
				std::string val;
				DataManager::GetValue(item.variableName, val);
				item.selected = (val != "0");
				mUpdate = 1;
			}
		}
		else if (varName == mVariable) {
			if (item.variableValue == currentValue) {
				item.selected = 1;
				SetVisibleListLocation(mVisibleItems.empty() ? 0 : mVisibleItems.size()-1);
			} else {
				item.selected = 0;
			}
		}
	}

	if (mVisibleItemsOld != mVisibleItems) {
		mUpdate = 1; // some item's visibility has changed
		if (firstDisplayedItem >= (int)mVisibleItems.size()) {
			// all items in the view area were removed - make last item visible
			SetVisibleListLocation(mVisibleItems.empty() ? 0 : mVisibleItems.size()-1);
		}
	}

	return 0;
}

void GUIListBox::SetPageFocus(int inFocus)
{
	GUIScrollList::SetPageFocus(inFocus);
	if (inFocus) {
		if (!isTextParsed) {
			isTextParsed = true;
			for (size_t i = 0; i < mListItems.size(); i++) {
				ListItem& item = mListItems[i];
				item.displayName = gui_parse_text(item.displayName);
			}
		}
		DataManager::GetValue(mVariable, currentValue);
		NotifyVarChange(mVariable, currentValue);
	}
}

size_t GUIListBox::GetItemCount()
{
	return mVisibleItems.size();
}

void GUIListBox::RenderItem(size_t itemindex, int yPos, bool selected)
{
	// note: the "selected" parameter above is for the currently touched item
	// don't confuse it with the more persistent "selected" flag per list item used below
	ListItem& item = mListItems[mVisibleItems[itemindex]];
	ImageResource* icon = item.selected ? mIconSelected : mIconUnselected;
	const std::string& text = item.displayName;

	RenderStdItem(yPos, selected, icon, text.c_str());
}

void GUIListBox::NotifySelect(size_t item_selected)
{
	if (!isCheckList) {
		// deselect all items, even invisible ones
		for (size_t i = 0; i < mListItems.size(); i++) {
			mListItems[i].selected = 0;
		}
	}

	ListItem& item = mListItems[mVisibleItems[item_selected]];

	if (isCheckList) {
		int selected = 1 - item.selected;
		item.selected = selected;
		DataManager::SetValue(item.variableName, selected ? "1" : "0");
	} else {
		item.selected = 1;
		string str = item.variableValue;	// [check] should this set currentValue instead?
		DataManager::SetValue(mVariable, str);
	}
	if (item.action)
		item.action->doActions();
	mUpdate = 1;
}
