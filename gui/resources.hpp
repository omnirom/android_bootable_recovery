/*
	Copyright 2017 TeamWin
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

// resources.hpp - Base classes for resource management of GUI

#ifndef _RESOURCE_HEADER
#define _RESOURCE_HEADER

#include <string>
#include <vector>
#include <map>
#include "rapidxml.hpp"
#include "../zipwrap.hpp"

extern "C" {
#include "../minuitwrp/minui.h"
}

// Base Objects
class Resource
{
public:
	Resource(xml_node<>* node, ZipWrap* pZip);
	virtual ~Resource() {}

public:
	std::string GetName() { return mName; }

private:
	std::string mName;

protected:
	static int ExtractResource(ZipWrap* pZip, std::string folderName, std::string fileName, std::string fileExtn, std::string destFile);
	static void LoadImage(ZipWrap* pZip, std::string file, gr_surface* surface);
	static void CheckAndScaleImage(gr_surface source, gr_surface* destination, int retain_aspect);
};

class FontResource : public Resource
{
public:
	FontResource(xml_node<>* node, ZipWrap* pZip);
	virtual ~FontResource();

public:
	void* GetResource() { return mFont; }
	int GetHeight() { return gr_ttf_getMaxFontHeight(mFont); }
	void Override(xml_node<>* node, ZipWrap* pZip);

protected:
	void* mFont;

private:
	void LoadFont(xml_node<>* node, ZipWrap* pZip);
	void DeleteFont();

private:
	int origFontSize;
	void* origFont;
};

class ImageResource : public Resource
{
public:
	ImageResource(xml_node<>* node, ZipWrap* pZip);
	virtual ~ImageResource();

public:
	gr_surface GetResource() { return mSurface; }
	int GetWidth() { return gr_get_width(mSurface); }
	int GetHeight() { return gr_get_height(mSurface); }

protected:
	gr_surface mSurface;
};

class AnimationResource : public Resource
{
public:
	AnimationResource(xml_node<>* node, ZipWrap* pZip);
	virtual ~AnimationResource();

public:
	gr_surface GetResource() { return mSurfaces.empty() ? NULL : mSurfaces.at(0); }
	gr_surface GetResource(int entry) { return mSurfaces.empty() ? NULL : mSurfaces.at(entry); }
	int GetWidth() { return gr_get_width(GetResource()); }
	int GetHeight() { return gr_get_height(GetResource()); }
	int GetResourceCount() { return mSurfaces.size(); }

protected:
	std::vector<gr_surface> mSurfaces;
};

class ResourceManager
{
public:
	ResourceManager();
	virtual ~ResourceManager();
	void AddStringResource(std::string resource_source, std::string resource_name, std::string value);
	void LoadResources(xml_node<>* resList, ZipWrap* pZip, std::string resource_source);

public:
	FontResource* FindFont(const std::string& name) const;
	ImageResource* FindImage(const std::string& name) const;
	AnimationResource* FindAnimation(const std::string& name) const;
	std::string FindString(const std::string& name) const;
	std::string FindString(const std::string& name, const std::string& default_string) const;
	void DumpStrings() const;

private:
	struct string_resource_struct {
		std::string value;
		std::string source;
	};
	std::vector<FontResource*> mFonts;
	std::vector<ImageResource*> mImages;
	std::vector<AnimationResource*> mAnimations;
	std::map<std::string, string_resource_struct> mStrings;
};

#endif  // _RESOURCE_HEADER
