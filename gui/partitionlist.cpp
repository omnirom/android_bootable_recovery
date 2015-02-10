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
#include "../partitions.hpp"

GUIPartitionList::GUIPartitionList(xml_node<>* node) : GUIScrollList(node)
{
	xml_attribute<>* attr;
	xml_node<>* child;

	int mIconWidth = 0, mIconHeight = 0, mSelectedIconHeight = 0, mSelectedIconWidth = 0, mUnselectedIconHeight = 0, mUnselectedIconWidth = 0;
	mIconSelected = mIconUnselected = NULL;
	mUpdate = 0;
	updateList = false;

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

	if (mIconSelected && mIconSelected->GetResource())
	{
		mSelectedIconWidth = gr_get_width(mIconSelected->GetResource());
		mSelectedIconHeight = gr_get_height(mIconSelected->GetResource());
		if (mSelectedIconHeight > mIconHeight)
			mIconHeight = mSelectedIconHeight;
		mIconWidth = mSelectedIconWidth;
	}

	if (mIconUnselected && mIconUnselected->GetResource())
	{
		mUnselectedIconWidth = gr_get_width(mIconUnselected->GetResource());
		mUnselectedIconHeight = gr_get_height(mIconUnselected->GetResource());
		if (mUnselectedIconHeight > mIconHeight)
			mIconHeight = mUnselectedIconHeight;
		if (mUnselectedIconWidth > mIconWidth)
			mIconWidth = mUnselectedIconWidth;
	}
	SetMaxIconSize(mIconWidth, mIconHeight);

	child = node->first_node("listtype");
	if (child && (attr = child->first_attribute("name"))) {
		ListType = attr->value();
		updateList = true;
	} else {
		mList.clear();
		LOGERR("No partition listtype specified for partitionlist GUI element\n");
		return;
	}
}

GUIPartitionList::~GUIPartitionList()
{
}

int GUIPartitionList::Update(void)
{
	if(!isConditionTrue())
		return 0;

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

	GUIScrollList::Update();

	if (updateList) {
		int listSize = 0;

		// Completely update the list if needed -- Used primarily for 
		// restore as the list for restore will change depending on what 
		// partitions were backed up
		mList.clear();
		PartitionManager.Get_Partition_List(ListType, &mList);
		SetVisibleListLocation(0);
		updateList = false;
		mUpdate = 1;
		if (ListType == "backup")
			MatchList();
	}

	if (mUpdate) {
		mUpdate = 0;
		if (Render() == 0)
			return 2;
	}

	return 0;
}

int GUIPartitionList::NotifyVarChange(const std::string& varName, const std::string& value)
{
	if(!isConditionTrue())
		return 0;

	GUIScrollList::NotifyVarChange(varName, value);

	if (varName == mVariable && !mUpdate)
	{
		if (ListType == "storage") {
			currentValue = value;
			SetStoragePosition();
		} else if (ListType == "backup") {
			MatchList();
		} else if (ListType == "restore") {
			updateList = true;
			SetVisibleListLocation(0);
		}

		mUpdate = 1;
		return 0;
	}
	return 0;
}

void GUIPartitionList::SetPageFocus(int inFocus)
{
	GUIScrollList::SetPageFocus(inFocus);
	if (inFocus) {
		if (ListType == "storage") {
			DataManager::GetValue(mVariable, currentValue);
			SetStoragePosition();
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

	for (i = 0; i < listSize; i++) {
		searchvalue = mList.at(i).Mount_Point + ";";
		pos = variablelist.find(searchvalue);
		if (pos != string::npos) {
			mList.at(i).selected = 1;
		} else {
			mList.at(i).selected = 0;
		}
	}
}

void GUIPartitionList::SetStoragePosition() {
	int listSize = mList.size();

	for (int i = 0; i < listSize; i++) {
		if (mList.at(i).Mount_Point == currentValue) {
			mList.at(i).selected = 1;
			SetVisibleListLocation(i);
		} else {
			mList.at(i).selected = 0;
			SetVisibleListLocation(0);
		}
	}
}

size_t GUIPartitionList::GetItemCount()
{
	return mList.size();
}

int GUIPartitionList::GetListItem(size_t item_index, Resource*& icon, std::string &text)
{
	text = mList.at(item_index).Display_Name;
	if (mList.at(item_index).selected)
		icon = mIconSelected;
	else
		icon = mIconUnselected;
	return 0;
}

void GUIPartitionList::NotifySelect(size_t item_selected)
{
	if (item_selected < mList.size()) {
		int listSize = mList.size();
		if (ListType == "mount") {
			if (!mList.at(item_selected).selected) {
				if (PartitionManager.Mount_By_Path(mList.at(item_selected).Mount_Point, true)) {
					mList.at(item_selected).selected = 1;
					PartitionManager.Add_MTP_Storage(mList.at(item_selected).Mount_Point);
					mUpdate = 1;
				}
			} else {
				if (PartitionManager.UnMount_By_Path(mList.at(item_selected).Mount_Point, true)) {
					mList.at(item_selected).selected = 0;
					mUpdate = 1;
				}
			}
		} else if (!mVariable.empty()) {
			if (ListType == "storage") {
				int i;
				std::string str = mList.at(item_selected).Mount_Point;
				bool update_size = false;
				TWPartition* Part = PartitionManager.Find_Partition_By_Path(str);
				if (Part == NULL) {
					LOGERR("Unable to locate partition for '%s'\n", str.c_str());
					return;
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
						mList.at(item_selected).Display_Name = Part->Storage_Name + " (";
						mList.at(item_selected).Display_Name += free_space;
						mList.at(item_selected).Display_Name += "MB)";
					}
					mList.at(item_selected).selected = 1;
					mUpdate = 1;

					DataManager::SetValue(mVariable, str);
				}
			} else {
				if (ListType == "flashimg") { // only one item can be selected for flashing images
					for (int i=0; i<listSize; i++)
						mList.at(i).selected = 0;
				}
				if (mList.at(item_selected).selected)
					mList.at(item_selected).selected = 0;
				else
					mList.at(item_selected).selected = 1;

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
	}
}
