// resources.hpp - Base classes for resource management of GUI

#ifndef _RESOURCE_HEADER
#define _RESOURCE_HEADER

#ifdef HAVE_SELINUX
#include "../minzip/Zip.h"
#else
#include "../minzipold/Zip.h"
#endif

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
};

typedef enum {
	TOUCH_START = 0, 
	TOUCH_DRAG = 1,
	TOUCH_RELEASE = 2,
	TOUCH_HOLD = 3,
	TOUCH_REPEAT = 4
} TOUCH_STATE;

class FontResource : public Resource
{
public:
	FontResource(xml_node<>* node, ZipArchive* pZip);
	virtual ~FontResource();

public:
	virtual void* GetResource(void) { return mFont; }

protected:
	void* mFont;
};

class ImageResource : public Resource
{
public:
	ImageResource(xml_node<>* node, ZipArchive* pZip);
	virtual ~ImageResource();

public:
	virtual void* GetResource(void) { return mSurface; }

protected:
	gr_surface mSurface;
};

class AnimationResource : public Resource
{
public:
	AnimationResource(xml_node<>* node, ZipArchive* pZip);
	virtual ~AnimationResource();

public:
	virtual void* GetResource(void) { return mSurfaces.at(0); }
	virtual void* GetResource(int entry) { return mSurfaces.at(entry); }
	virtual int GetResourceCount(void) { return mSurfaces.size(); }

protected:
	std::vector<gr_surface> mSurfaces;
};

class ResourceManager
{
public:
	ResourceManager(xml_node<>* resList, ZipArchive* pZip);
	virtual ~ResourceManager();

public:
	Resource* FindResource(std::string name);

private:
	std::vector<Resource*> mResources;
};

#endif  // _RESOURCE_HEADER
