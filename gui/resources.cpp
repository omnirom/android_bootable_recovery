// resource.cpp - Source to manage GUI resources

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <string>
#include <sstream>
#include <iostream>
#include <iomanip>

#include "../minzip/Zip.h"
extern "C" {
#include "../twcommon.h"
#include "../minuitwrp/minui.h"
#include "gui.h"
}

#include "rapidxml.hpp"
#include "objects.hpp"

#define TMP_RESOURCE_NAME   "/tmp/extract.bin"

Resource::Resource(xml_node<>* node, ZipArchive* pZip)
{
	if (node && node->first_attribute("name"))
		mName = node->first_attribute("name")->value();
}

int Resource::ExtractResource(ZipArchive* pZip, std::string folderName, std::string fileName, std::string fileExtn, std::string destFile)
{
	if (!pZip)
		return -1;

	std::string src = folderName + "/" + fileName + fileExtn;

	const ZipEntry* binary = mzFindZipEntry(pZip, src.c_str());
	if (binary == NULL) {
		return -1;
	}

	unlink(destFile.c_str());
	int fd = creat(destFile.c_str(), 0666);
	if (fd < 0)
		return -1;

	int ret = 0;
	if (!mzExtractZipEntryToFile(pZip, binary, fd))
		ret = -1;

	close(fd);
	return ret;
}

void Resource::LoadImage(ZipArchive* pZip, std::string file, gr_surface* source)
{
	if (ExtractResource(pZip, "images", file, ".png", TMP_RESOURCE_NAME) == 0)
	{
		res_create_surface(TMP_RESOURCE_NAME, source);
		unlink(TMP_RESOURCE_NAME);
	}
	else if (ExtractResource(pZip, "images", file, "", TMP_RESOURCE_NAME) == 0)
	{
		// JPG includes the .jpg extension in the filename so extension should be blank
		res_create_surface(TMP_RESOURCE_NAME, source);
		unlink(TMP_RESOURCE_NAME);
	}
	else if (!pZip)
	{
		// File name in xml may have included .png so try without adding .png
		res_create_surface(file.c_str(), source);
	}
}

void Resource::CheckAndScaleImage(gr_surface source, gr_surface* destination, int retain_aspect)
{
	if (!source) {
		*destination = NULL;
		return;
	}
	if (get_scale_w() != 0 && get_scale_h() != 0) {
		float scale_w = get_scale_w(), scale_h = get_scale_h();
		if (retain_aspect) {
			if (scale_w < scale_h)
				scale_h = scale_w;
			else
				scale_w = scale_h;
		}
		if (res_scale_surface(source, destination, scale_w, scale_h)) {
			LOGINFO("Error scaling image, using regular size.\n");
			*destination = source;
		}
	} else {
		*destination = source;
	}
}

FontResource::FontResource(xml_node<>* node, ZipArchive* pZip)
 : Resource(node, pZip)
{
	std::string file;
	xml_attribute<>* attr;

	mFont = NULL;
	if (!node)
		return;

	attr = node->first_attribute("filename");
	if (!attr)
		return;

	file = attr->value();

#ifndef TW_DISABLE_TTF
	if(file.size() >= 4 && file.compare(file.size()-4, 4, ".ttf") == 0)
	{
		m_type = TYPE_TTF;

		attr = node->first_attribute("size");
		if(!attr)
			return;

		int size = scale_theme_min(atoi(attr->value()));
		int dpi = 300;

		attr = node->first_attribute("dpi");
		if(attr)
			dpi = atoi(attr->value());

		if (ExtractResource(pZip, "fonts", file, "", TMP_RESOURCE_NAME) == 0)
		{
			mFont = gr_ttf_loadFont(TMP_RESOURCE_NAME, size, dpi);
			unlink(TMP_RESOURCE_NAME);
		}
		else
		{
			file = std::string(TWRES "fonts/") + file;
			mFont = gr_ttf_loadFont(file.c_str(), size, dpi);
		}
	}
	else
#endif
	{
		m_type = TYPE_TWRP;

		if(file.size() >= 4 && file.compare(file.size()-4, 4, ".ttf") == 0)
		{
			attr = node->first_attribute("fallback");
			if (!attr)
				return;

			file = attr->value();
		}

		if (ExtractResource(pZip, "fonts", file, ".dat", TMP_RESOURCE_NAME) == 0)
		{
			mFont = gr_loadFont(TMP_RESOURCE_NAME);
			unlink(TMP_RESOURCE_NAME);
		}
		else
		{
			mFont = gr_loadFont(file.c_str());
		}
	}
}

FontResource::~FontResource()
{
	if(mFont)
	{
#ifndef TW_DISABLE_TTF
		if(m_type == TYPE_TTF)
			gr_ttf_freeFont(mFont);
		else
#endif
			gr_freeFont(mFont);
	}
}

ImageResource::ImageResource(xml_node<>* node, ZipArchive* pZip)
 : Resource(node, pZip)
{
	std::string file;
	gr_surface temp_surface = NULL;

	mSurface = NULL;
	if (!node) {
		LOGERR("ImageResource node is NULL\n");
		return;
	}

	if (node->first_attribute("filename"))
		file = node->first_attribute("filename")->value();
	else {
		LOGERR("No filename specified for image resource.\n");
		return;
	}

	bool retain_aspect = (node->first_attribute("retainaspect") != NULL);
	// the value does not matter, if retainaspect is present, we assume that we want to retain it
	LoadImage(pZip, file, &temp_surface);
	CheckAndScaleImage(temp_surface, &mSurface, retain_aspect);
}

ImageResource::~ImageResource()
{
	if (mSurface)
		res_free_surface(mSurface);
}

AnimationResource::AnimationResource(xml_node<>* node, ZipArchive* pZip)
 : Resource(node, pZip)
{
	std::string file;
	int fileNum = 1;

	if (!node)
		return;

	if (node->first_attribute("filename"))
		file = node->first_attribute("filename")->value();
	else {
		LOGERR("No filename specified for image resource.\n");
		return;
	}

	bool retain_aspect = (node->first_attribute("retainaspect") != NULL);
	// the value does not matter, if retainaspect is present, we assume that we want to retain it
	for (;;)
	{
		std::ostringstream fileName;
		fileName << file << std::setfill ('0') << std::setw (3) << fileNum;

		gr_surface surface, temp_surface = NULL;
		LoadImage(pZip, fileName.str(), &temp_surface);
		CheckAndScaleImage(temp_surface, &surface, retain_aspect);
		if (surface) {
			mSurfaces.push_back(surface);
			fileNum++;
		} else
			break; // Done loading animation images
	}
}

AnimationResource::~AnimationResource()
{
	std::vector<gr_surface>::iterator it;

	for (it = mSurfaces.begin(); it != mSurfaces.end(); ++it)
		res_free_surface(*it);

	mSurfaces.clear();
}

FontResource* ResourceManager::FindFont(const std::string& name) const
{
	for (std::vector<FontResource*>::const_iterator it = mFonts.begin(); it != mFonts.end(); ++it)
		if (name == (*it)->GetName())
			return *it;
	return NULL;
}

ImageResource* ResourceManager::FindImage(const std::string& name) const
{
	for (std::vector<ImageResource*>::const_iterator it = mImages.begin(); it != mImages.end(); ++it)
		if (name == (*it)->GetName())
			return *it;
	return NULL;
}

AnimationResource* ResourceManager::FindAnimation(const std::string& name) const
{
	for (std::vector<AnimationResource*>::const_iterator it = mAnimations.begin(); it != mAnimations.end(); ++it)
		if (name == (*it)->GetName())
			return *it;
	return NULL;
}

std::string ResourceManager::FindString(const std::string& name) const
{
	std::map<std::string, std::string>::const_iterator it = mStrings.find(name);
	if (it != mStrings.end())
		return it->second;
	return "[" + name + ("]");
}

ResourceManager::ResourceManager()
{
}

void ResourceManager::LoadResources(xml_node<>* resList, ZipArchive* pZip)
{
	if (!resList)
		return;

	for (xml_node<>* child = resList->first_node(); child; child = child->next_sibling())
	{
		std::string type = child->name();
		if (type == "resource") {
			// legacy format : <resource type="...">
			xml_attribute<>* attr = child->first_attribute("type");
			type = attr ? attr->value() : "*unspecified*";
		}

		bool error = false;
		if (type == "font")
		{
			FontResource* res = new FontResource(child, pZip);
			if (res->GetResource())
				mFonts.push_back(res);
			else {
				error = true;
				delete res;
			}
		}
		else if (type == "image")
		{
			ImageResource* res = new ImageResource(child, pZip);
			if (res->GetResource())
				mImages.push_back(res);
			else {
				error = true;
				delete res;
			}
		}
		else if (type == "animation")
		{
			AnimationResource* res = new AnimationResource(child, pZip);
			if (res->GetResourceCount())
				mAnimations.push_back(res);
			else {
				error = true;
				delete res;
			}
		}
		else if (type == "string")
		{
			if (xml_attribute<>* attr = child->first_attribute("name"))
				mStrings[attr->value()] = child->value();
			else
				error = true;
		}
		else
		{
			LOGERR("Resource type (%s) not supported.\n", type.c_str());
			error = true;
		}

		if (error)
		{
			std::string res_name;
			if (child->first_attribute("name"))
				res_name = child->first_attribute("name")->value();
			if (res_name.empty() && child->first_attribute("filename"))
				res_name = child->first_attribute("filename")->value();

			if (!res_name.empty()) {
				LOGERR("Resource (%s)-(%s) failed to load\n", type.c_str(), res_name.c_str());
			} else
				LOGERR("Resource type (%s) failed to load\n", type.c_str());
		}
	}
}

ResourceManager::~ResourceManager()
{
	for (std::vector<FontResource*>::iterator it = mFonts.begin(); it != mFonts.end(); ++it)
		delete *it;

	for (std::vector<ImageResource*>::iterator it = mImages.begin(); it != mImages.end(); ++it)
		delete *it;

	for (std::vector<AnimationResource*>::iterator it = mAnimations.begin(); it != mAnimations.end(); ++it)
		delete *it;
}
