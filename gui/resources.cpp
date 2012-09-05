// resource.cpp - Source to manage GUI resources

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

#include <string>
#include <sstream>
#include <iostream>
#include <iomanip>

extern "C" {
#include "../common.h"
#include "../minuitwrp/minui.h"
#include "../recovery_ui.h"
}

#include "rapidxml.hpp"
#include "objects.hpp"

#define TMP_RESOURCE_NAME   "/tmp/extract.bin"

Resource::Resource(xml_node<>* node, ZipArchive* pZip)
{
    if (node && node->first_attribute("name"))
        mName = node->first_attribute("name")->value();
}


int Resource::ExtractResource(ZipArchive* pZip, 
                              std::string folderName, 
                              std::string fileName, 
                              std::string fileExtn, 
                              std::string destFile)
{
    if (!pZip)  return -1;

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

FontResource::FontResource(xml_node<>* node, ZipArchive* pZip)
 : Resource(node, pZip)
{
    std::string file;

    mFont = NULL;
    if (!node)  return;

    if (node->first_attribute("filename"))
        file = node->first_attribute("filename")->value();

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

FontResource::~FontResource()
{
}

ImageResource::ImageResource(xml_node<>* node, ZipArchive* pZip)
 : Resource(node, pZip)
{
    std::string file;

    mSurface = NULL;
    if (!node)  return;

    if (node->first_attribute("filename"))
        file = node->first_attribute("filename")->value();

    if (ExtractResource(pZip, "images", file, ".png", TMP_RESOURCE_NAME) == 0)
    {
        res_create_surface(TMP_RESOURCE_NAME, &mSurface);
        unlink(TMP_RESOURCE_NAME);
    } else if (ExtractResource(pZip, "images", file, "", TMP_RESOURCE_NAME) == 0)
    {
        // JPG includes the .jpg extension in the filename so extension should be blank
		res_create_surface(TMP_RESOURCE_NAME, &mSurface);
        unlink(TMP_RESOURCE_NAME);
    }
    else
		res_create_surface(file.c_str(), &mSurface);
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

    if (!node)  return;

    if (node->first_attribute("filename"))
        file = node->first_attribute("filename")->value();

    for ( ; ; )
    {
        std::ostringstream fileName;
        fileName << file << std::setfill ('0') << std::setw (3) << fileNum;

        gr_surface surface;
        if (pZip)
        {
            if (ExtractResource(pZip, "images", fileName.str(), ".png", TMP_RESOURCE_NAME) != 0)
                break;
    
            if (res_create_surface(TMP_RESOURCE_NAME, &surface))
                break;

            unlink(TMP_RESOURCE_NAME);
        }
        else
        {
            if (res_create_surface(fileName.str().c_str(), &surface))
                break;
        }
        mSurfaces.push_back(surface);
        fileNum++;
    }
}

AnimationResource::~AnimationResource()
{
    std::vector<gr_surface>::iterator it;

    for (it = mSurfaces.begin(); it != mSurfaces.end(); ++it)
    {
        res_free_surface(*it);
    }
    mSurfaces.clear();
}

Resource* ResourceManager::FindResource(std::string name)
{
    std::vector<Resource*>::iterator iter;

    for (iter = mResources.begin(); iter != mResources.end(); iter++)
    {
        if (name == (*iter)->GetName())
            return (*iter);
    }
    return NULL;
}

ResourceManager::ResourceManager(xml_node<>* resList, ZipArchive* pZip)
{
    xml_node<>* child;

    if (!resList)       return;

    child = resList->first_node("resource");
    while (child != NULL)
    {
        xml_attribute<>* attr = child->first_attribute("type");
        if (!attr)
            break;

		std::string type = attr->value();

        if (type == "font")
        {
            FontResource* res = new FontResource(child, pZip);
            if (res == NULL || res->GetResource() == NULL)
            {
                xml_attribute<>* attr_name = child->first_attribute("name");

				if (!attr_name) {
					std::string res_name = attr_name->value();
					LOGE("Resource (%s)-(%s) failed to load\n", type.c_str(), res_name.c_str());
				} else
					LOGE("Resource type (%s) failed to load\n", type.c_str());

                delete res;
            }
            else
            {
                mResources.push_back((Resource*) res);
            }
        }
        else if (type == "image")
        {
			ImageResource* res = new ImageResource(child, pZip);
            if (res == NULL || res->GetResource() == NULL)
            {
                xml_attribute<>* attr_name = child->first_attribute("name");

				if (!attr_name) {
					std::string res_name = attr_name->value();
					LOGE("Resource (%s)-(%s) failed to load\n", type.c_str(), res_name.c_str());
				} else
					LOGE("Resource type (%s) failed to load\n", type.c_str());

                delete res;
            }
            else
            {
				mResources.push_back((Resource*) res);
            }
        }
        else if (type == "animation")
        {
            AnimationResource* res = new AnimationResource(child, pZip);
            if (res == NULL || res->GetResource() == NULL)
            {
                xml_attribute<>* attr_name = child->first_attribute("name");

				if (!attr_name) {
					std::string res_name = attr_name->value();
					LOGE("Resource (%s)-(%s) failed to load\n", type.c_str(), res_name.c_str());
				} else
					LOGE("Resource type (%s) failed to load\n", type.c_str());

                delete res;
            }
            else
            {
                mResources.push_back((Resource*) res);
            }
        }
        else
        {
            LOGE("Resource type (%s) not supported.\n", type.c_str());
        }

        child = child->next_sibling("resource");
    }
}

ResourceManager::~ResourceManager()
{
    std::vector<Resource*>::iterator iter;

    for (iter = mResources.begin(); iter != mResources.end(); iter++)
    {
        delete *iter;
    }
    mResources.clear();
}

