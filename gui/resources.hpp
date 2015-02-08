// resources.hpp - Base classes for resource management of GUI

#ifndef _RESOURCE_HEADER
#define _RESOURCE_HEADER

#include "../minzip/Zip.h"

extern "C" {
#include "../minuitwrp/minui.h"
}

// Base Objects
class Resource
{
public:
	Resource(xml_node<>* node, ZipArchive* pZip);
	virtual ~Resource() {}

public:
	std::string GetName() { return mName; }
	virtual bool loadedOK() = 0;

private:
	std::string mName;

protected:
	static int ExtractResource(ZipArchive* pZip, std::string folderName, std::string fileName, std::string fileExtn, std::string destFile);
};

class FontResource : public Resource
{
public:
	enum Type
	{
		TYPE_TWRP,
#ifndef TW_DISABLE_TTF
		TYPE_TTF,
#endif
	};

	FontResource(xml_node<>* node, ZipArchive* pZip);
	virtual ~FontResource();

public:
	void* GetResource() { return mFont; }
	virtual bool loadedOK() { return mFont != NULL; }

protected:
	void* mFont;
	Type m_type;
};

class ImageResource : public Resource
{
public:
	ImageResource(xml_node<>* node, ZipArchive* pZip);
	virtual ~ImageResource();

public:
	gr_surface GetResource() { return mSurface; }
	virtual bool loadedOK() { return mSurface != NULL; }

protected:
	gr_surface mSurface;
};

class AnimationResource : public Resource
{
public:
	AnimationResource(xml_node<>* node, ZipArchive* pZip);
	virtual ~AnimationResource();

public:
	gr_surface GetResource() { return mSurfaces.empty() ? NULL : mSurfaces.at(0); }
	gr_surface GetResource(int entry) { return mSurfaces.empty() ? NULL : mSurfaces.at(entry); }
	virtual int GetResourceCount() { return mSurfaces.size(); }
	virtual bool loadedOK() { return !mSurfaces.empty(); }

protected:
	std::vector<gr_surface> mSurfaces;
};

class ResourceManager
{
public:
	ResourceManager(xml_node<>* resList, ZipArchive* pZip);
	virtual ~ResourceManager();
	void LoadResources(xml_node<>* resList, ZipArchive* pZip);

public:
	Resource* FindResource(std::string name);

private:
	std::vector<Resource*> mResources;
};

#endif  // _RESOURCE_HEADER
