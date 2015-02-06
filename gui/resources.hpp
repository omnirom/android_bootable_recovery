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
	virtual void* GetResource(void) = 0;
	std::string GetName(void) { return mName; }

private:
	std::string mName;

protected:
	static int ExtractResource(ZipArchive* pZip, std::string folderName, std::string fileName, std::string fileExtn, std::string destFile);
	static void LoadImage(ZipArchive* pZip, std::string file, gr_surface* source);
	static void CheckAndScaleImage(gr_surface source, gr_surface* destination, int retain_aspect);
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
	virtual void* GetResource(void) { return mFont; }

protected:
	void* mFont;
	Type m_type;
};

class ImageResource : public Resource
{
public:
	ImageResource(xml_node<>* node, ZipArchive* pZip, int retain_aspect);
	virtual ~ImageResource();

public:
	virtual void* GetResource(void) { return mSurface; }

protected:
	gr_surface mSurface;
};

class AnimationResource : public Resource
{
public:
	AnimationResource(xml_node<>* node, ZipArchive* pZip, int retain_aspect);
	virtual ~AnimationResource();

public:
	virtual void* GetResource(void) { return mSurfaces.empty() ? NULL : mSurfaces.at(0); }
	virtual void* GetResource(int entry) { return mSurfaces.empty() ? NULL : mSurfaces.at(entry); }
	virtual int GetResourceCount(void) { return mSurfaces.size(); }

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
