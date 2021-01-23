/*
	Copyright 2012 bigbiff/Dees_Troy TeamWin
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
#include <algorithm>
#ifdef __ANDROID_API_M__
#include <vector>
#ifdef __ANDROID_API_N__
#include <android-base/strings.h>
#else
#include <base/strings.h>
#endif
#else
#endif
extern "C" {
#include "../twcommon.h"
}
#include "../minuitwrp/minui.h"

#include "rapidxml.hpp"
#include "objects.hpp"
#include "../data.hpp"
#include "../twrp-functions.hpp"
#include "../adbbu/libtwadbbu.hpp"

int GUIFileSelector::mSortOrder = 0;

GUIFileSelector::GUIFileSelector(xml_node<>* node) : GUIScrollList(node)
{
	xml_attribute<>* attr;
	xml_node<>* child;

	mFolderIcon = mFileIcon = NULL;
	mShowFolders = mShowFiles = mShowNavFolders = 1;
	mUpdate = 0;
	mPathVar = "cwd";
	updateFileList = false;

	// Load filter for filtering files (e.g. *.zip for only zips)
	child = FindNode(node, "filter");
	if (child) {
		attr = child->first_attribute("extn");
		if (attr)
			mExtn = attr->value();
		attr = child->first_attribute("folders");
		if (attr)
			mShowFolders = atoi(attr->value());
		attr = child->first_attribute("files");
		if (attr)
			mShowFiles = atoi(attr->value());
		attr = child->first_attribute("nav");
		if (attr)
			mShowNavFolders = atoi(attr->value());
	}
	child = FindNode(node, "prfxfilter");
	if (child) {
		attr = child->first_attribute("prfx");
		if (attr)
			mPrfx = attr->value();
	}

	// Handle the path variable
	child = FindNode(node, "path");
	if (child) {
		attr = child->first_attribute("name");
		if (attr)
			mPathVar = attr->value();
		attr = child->first_attribute("default");
		if (attr) {
			mPathDefault = attr->value();
			DataManager::SetValue(mPathVar, attr->value());
		}
	}

	// Handle the result variable
	child = FindNode(node, "data");
	if (child) {
		attr = child->first_attribute("name");
		if (attr)
			mVariable = attr->value();
		attr = child->first_attribute("default");
		if (attr)
			DataManager::SetValue(mVariable, attr->value());
	}

	// Handle the sort variable
	child = FindNode(node, "sort");
	if (child) {
		attr = child->first_attribute("name");
		if (attr)
			mSortVariable = attr->value();
		attr = child->first_attribute("default");
		if (attr)
			DataManager::SetValue(mSortVariable, attr->value());

		DataManager::GetValue(mSortVariable, mSortOrder);
	}

	// Handle the selection variable
	child = FindNode(node, "selection");
	if (child && (attr = child->first_attribute("name")))
		mSelection = attr->value();
	else
		mSelection = "0";

	// Get folder and file icons if present
	child = FindNode(node, "icon");
	if (child) {
		mFolderIcon = LoadAttrImage(child, "folder");
		mFileIcon = LoadAttrImage(child, "file");
	}
	int iconWidth = 0, iconHeight = 0;
	if (mFolderIcon && mFolderIcon->GetResource() && mFileIcon && mFileIcon->GetResource()) {
		iconWidth = std::max(mFolderIcon->GetWidth(), mFileIcon->GetWidth());
		iconHeight = std::max(mFolderIcon->GetHeight(), mFileIcon->GetHeight());
	} else if (mFolderIcon && mFolderIcon->GetResource()) {
		iconWidth = mFolderIcon->GetWidth();
		iconHeight = mFolderIcon->GetHeight();
	} else if (mFileIcon && mFileIcon->GetResource()) {
		iconWidth = mFileIcon->GetWidth();
		iconHeight = mFileIcon->GetHeight();
	}
	SetMaxIconSize(iconWidth, iconHeight);

	// Fetch the file/folder list
	std::string value;
	DataManager::GetValue(mPathVar, value);
	GetFileList(value);
}

GUIFileSelector::~GUIFileSelector()
{
}

int GUIFileSelector::Update(void)
{
	if (!isConditionTrue())
		return 0;

	GUIScrollList::Update();

	// Update the file list if needed
	if (updateFileList) {
		string value;
		DataManager::GetValue(mPathVar, value);
		if (GetFileList(value) == 0) {
			updateFileList = false;
			mUpdate = 1;
		} else
			return 0;
	}

	if (mUpdate) {
		mUpdate = 0;
		if (Render() == 0)
			return 2;
	}
	return 0;
}

int GUIFileSelector::NotifyVarChange(const std::string& varName, const std::string& value)
{
	GUIScrollList::NotifyVarChange(varName, value);

	if (!isConditionTrue())
		return 0;

	if (varName.empty()) {
		// Always clear the data variable so we know to use it
		DataManager::SetValue(mVariable, "");
	}
	if (varName == mPathVar || varName == mSortVariable) {
		if (varName == mSortVariable) {
			DataManager::GetValue(mSortVariable, mSortOrder);
		} else {
			// Reset the list to the top
			SetVisibleListLocation(0);
			if (value.empty())
				DataManager::SetValue(mPathVar, mPathDefault);
		}
		updateFileList = true;
		mUpdate = 1;
		return 0;
	}
	return 0;
}

bool GUIFileSelector::fileSort(FileData d1, FileData d2)
{
	if (d1.fileName == ".")
		return -1;
	if (d2.fileName == ".")
		return 0;
	if (d1.fileName == "..")
		return -1;
	if (d2.fileName == "..")
		return 0;

	switch (mSortOrder) {
		case 3: // by size largest first
			if (d1.fileSize == d2.fileSize || d1.fileType == DT_DIR) // some directories report a different size than others - but this is not the size of the files inside the directory, so we just sort by name on directories
				return (strcasecmp(d1.fileName.c_str(), d2.fileName.c_str()) < 0);
			return d1.fileSize < d2.fileSize;
		case -3: // by size smallest first
			if (d1.fileSize == d2.fileSize || d1.fileType == DT_DIR) // some directories report a different size than others - but this is not the size of the files inside the directory, so we just sort by name on directories
				return (strcasecmp(d1.fileName.c_str(), d2.fileName.c_str()) > 0);
			return d1.fileSize > d2.fileSize;
		case 2: // by last modified date newest first
			if (d1.lastModified == d2.lastModified)
				return (strcasecmp(d1.fileName.c_str(), d2.fileName.c_str()) < 0);
			return d1.lastModified < d2.lastModified;
		case -2: // by date oldest first
			if (d1.lastModified == d2.lastModified)
				return (strcasecmp(d1.fileName.c_str(), d2.fileName.c_str()) > 0);
			return d1.lastModified > d2.lastModified;
		case -1: // by name descending
			return (strcasecmp(d1.fileName.c_str(), d2.fileName.c_str()) > 0);
		default: // should be a 1 - sort by name ascending
			return (strcasecmp(d1.fileName.c_str(), d2.fileName.c_str()) < 0);
	}
	return 0;
}

int GUIFileSelector::GetFileList(const std::string folder)
{
	DIR* d;
	struct dirent* de;
	struct stat st;

	// Clear all data
	mFolderList.clear();
	mFileList.clear();

	d = opendir(folder.c_str());
	if (d == NULL) {
		LOGINFO("Unable to open '%s'\n", folder.c_str());
		if (folder != "/" && (mShowNavFolders != 0 || mShowFiles != 0)) {
			size_t found;
			found = folder.find_last_of('/');
			if (found != string::npos) {
				string new_folder = folder.substr(0, found);

				if (new_folder.length() < 2)
					new_folder = "/";
				DataManager::SetValue(mPathVar, new_folder);
			}
		}
		return -1;
	}

	while ((de = readdir(d)) != NULL) {
		FileData data;
		bool match = false;

		data.fileName = de->d_name;
		if (data.fileName == ".")
			continue;
		if (data.fileName == ".." && folder == "/")
			continue;

		data.fileType = de->d_type;

		std::string path = folder + "/" + data.fileName;
		stat(path.c_str(), &st);
		data.protection = st.st_mode;
		data.userId = st.st_uid;
		data.groupId = st.st_gid;
		data.fileSize = st.st_size;
		data.lastAccess = st.st_atime;
		data.lastModified = st.st_mtime;
		data.lastStatChange = st.st_ctime;

		if (data.fileType == DT_UNKNOWN) {
			data.fileType = TWFunc::Get_D_Type_From_Stat(path);
		}
		if (data.fileType == DT_DIR) {
			if (mShowNavFolders || (data.fileName != "." && data.fileName != ".."))
				mFolderList.push_back(data);
		} else if (data.fileType == DT_REG || data.fileType == DT_LNK || data.fileType == DT_BLK) {
#ifdef __ANDROID_API_M__
			std::vector<std::string> mExtnResults = android::base::Split(mExtn, ";");
			for (const std::string& mExtnElement : mExtnResults)
			{
				std::string mExtnName = android::base::Trim(mExtnElement);
				if (mExtnName.empty() || (data.fileName.length() >= mExtnName.length() && data.fileName.substr(data.fileName.length() - mExtnName.length()) == mExtnName)) {
					if (mExtnName == ".ab" && twadbbu::Check_ADB_Backup_File(path))
						mFolderList.push_back(data);
					else
						mFileList.push_back(data);
					match = true;
					break;
				}
			}

			if (!match) {
				std::vector<std::string> mPrfxResults = android::base::Split(mPrfx, ";");
				for (const std::string& mPrfxElement : mPrfxResults)
				{
					std::string mPrfxName = android::base::Trim(mPrfxElement);
					if (!mPrfxName.empty() && data.fileName.length() >= mPrfxName.length() && data.fileName.substr(0, mPrfxName.length()) == mPrfxName) {
						mFileList.push_back(data);
					}
#else //On android 5.1 we can't use android::base::Trim and Split so just use the first extension written in the list
			std::size_t seppos = mExtn.find_first_of(";");
			std::string mExtnf;
			if (seppos!=std::string::npos){
				mExtnf = mExtn.substr(0, seppos);
			} else {
				mExtnf = mExtn;
			}
			if (mExtnf.empty() || (data.fileName.length() >= mExtnf.length() && data.fileName.substr(data.fileName.length() - mExtnf.length()) == mExtnf)) {
				if (mExtnf == ".ab" && twadbbu::Check_ADB_Backup_File(path))
					mFolderList.push_back(data);
				else
					mFileList.push_back(data);
				match = true;
			}

			if (!match) {
				std::size_t seppos = mPrfx.find_first_of(";");
				std::string mPrfxf;
				if (seppos!=std::string::npos){
					mPrfxf = mPrfx.substr(0, seppos);
				} else {
					mPrfxf = mPrfx;
				}
				if (!mPrfxf.empty() && data.fileName.length() >= mPrfxf.length() && data.fileName.substr(0, mPrfxf.length()) == mPrfxf) {
					mFileList.push_back(data);
#endif
				}
			}
		}
	}
	closedir(d);

	std::sort(mFolderList.begin(), mFolderList.end(), fileSort);
	std::sort(mFileList.begin(), mFileList.end(), fileSort);

	return 0;
}

void GUIFileSelector::SetPageFocus(int inFocus)
{
	GUIScrollList::SetPageFocus(inFocus);
	if (inFocus) {
		std::string value;
		DataManager::GetValue(mPathVar, value);
		if (value.empty())
			DataManager::SetValue(mPathVar, mPathDefault);
		updateFileList = true;
		mUpdate = 1;
	}
}

size_t GUIFileSelector::GetItemCount()
{
	size_t folderSize = mShowFolders ? mFolderList.size() : 0;
	size_t fileSize = mShowFiles ? mFileList.size() : 0;
	return folderSize + fileSize;
}

void GUIFileSelector::RenderItem(size_t itemindex, int yPos, bool selected)
{
	size_t folderSize = mShowFolders ? mFolderList.size() : 0;

	ImageResource* icon;
	std::string text;

	if (itemindex < folderSize) {
		text = mFolderList.at(itemindex).fileName;
		icon = mFolderIcon;
		if (text == "..")
			text = gui_lookup("up_a_level", "(Up A Level)");
	} else {
		text = mFileList.at(itemindex - folderSize).fileName;
		icon = mFileIcon;
	}

	RenderStdItem(yPos, selected, icon, text.c_str());
}

void GUIFileSelector::NotifySelect(size_t item_selected)
{
	size_t folderSize = mShowFolders ? mFolderList.size() : 0;
	size_t fileSize = mShowFiles ? mFileList.size() : 0;

	if (item_selected < folderSize + fileSize) {
		// We've selected an item!
		std::string str;
		if (item_selected < folderSize) {
			std::string cwd;

			str = mFolderList.at(item_selected).fileName;
			if (mSelection != "0")
				DataManager::SetValue(mSelection, str);
			DataManager::GetValue(mPathVar, cwd);

			// Ignore requests to do nothing
			if (str == ".")	 return;
			if (str == "..") {
				if (cwd != "/") {
					size_t found;
					found = cwd.find_last_of('/');
					cwd = cwd.substr(0,found);

					if (cwd.length() < 2)   cwd = "/";
				}
			} else {
				// Add a slash if we're not the root folder
				if (cwd != "/")	 cwd += "/";
				cwd += str;
			}

			if (mShowNavFolders == 0 && (mShowFiles == 0 || mExtn == ".ab")) {
				// this is probably the restore list and we need to save chosen location to mVariable instead of mPathVar
				DataManager::SetValue(mVariable, cwd);
			} else {
				// We are changing paths, so we need to set mPathVar
				DataManager::SetValue(mPathVar, cwd);
			}
		} else if (!mVariable.empty()) {
			str = mFileList.at(item_selected - folderSize).fileName;
			if (mSelection != "0")
				DataManager::SetValue(mSelection, str);

			std::string cwd;
			DataManager::GetValue(mPathVar, cwd);
			if (cwd != "/")
				cwd += "/";
			DataManager::SetValue(mVariable, cwd + str);
		}
	}
	mUpdate = 1;
}
