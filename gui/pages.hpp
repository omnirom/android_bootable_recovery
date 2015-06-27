// pages.hpp - Base classes for page manager of GUI

#ifndef _PAGES_HEADER_HPP
#define _PAGES_HEADER_HPP

#include "../minzip/Zip.h"
#include <vector>
#include <map>
#include "rapidxml.hpp"
using namespace rapidxml;

enum TOUCH_STATE {
	TOUCH_START = 0,
	TOUCH_DRAG = 1,
	TOUCH_RELEASE = 2,
	TOUCH_HOLD = 3,
	TOUCH_REPEAT = 4
};

struct COLOR {
	unsigned char red;
	unsigned char green;
	unsigned char blue;
	unsigned char alpha;
	COLOR() : red(0), green(0), blue(0), alpha(0) {}
	COLOR(unsigned char r, unsigned char g, unsigned char b, unsigned char a = 255)
		: red(r), green(g), blue(b), alpha(a) {}
};

// Utility Functions
int ConvertStrToColor(std::string str, COLOR* color);
int gui_forceRender(void);
int gui_changePage(std::string newPage);
int gui_changeOverlay(std::string newPage);
std::string gui_parse_text(string inText);

class Resource;
class ResourceManager;
class RenderObject;
class ActionObject;
class InputObject;
class MouseCursor;
class GUIObject;
class HardwareKeyboard;

class Page
{
public:
	Page(xml_node<>* page, std::vector<xml_node<>*> *templates);
	virtual ~Page();

	std::string GetName(void)   { return mName; }

public:
	virtual int Render(void);
	virtual int Update(void);
	virtual int NotifyTouch(TOUCH_STATE state, int x, int y);
	virtual int NotifyKey(int key, bool down);
	virtual int NotifyKeyboard(int key);
	virtual int SetKeyBoardFocus(int inFocus);
	virtual int NotifyVarChange(std::string varName, std::string value);
	virtual void SetPageFocus(int inFocus);

protected:
	std::string mName;
	std::vector<GUIObject*> mObjects;
	std::vector<RenderObject*> mRenders;
	std::vector<ActionObject*> mActions;
	std::vector<InputObject*> mInputs;

	ActionObject* mTouchStart;
	COLOR mBackground;

protected:
	bool ProcessNode(xml_node<>* page, std::vector<xml_node<>*> *templates, int depth);
};

class PageSet
{
public:
	PageSet(char* xmlFile);
	virtual ~PageSet();

public:
	int Load(ZipArchive* package);
	int CheckInclude(ZipArchive* package, xml_document<> *parentDoc);

	Page* FindPage(std::string name);
	int SetPage(std::string page);
	int SetOverlay(Page* page);
	const ResourceManager* GetResources();

	// Helper routine for identifing if we're the current page
	int IsCurrentPage(Page* page);

	// These are routing routines
	int Render(void);
	int Update(void);
	int NotifyTouch(TOUCH_STATE state, int x, int y);
	int NotifyKey(int key, bool down);
	int NotifyKeyboard(int key);
	int SetKeyBoardFocus(int inFocus);
	int NotifyVarChange(std::string varName, std::string value);

	std::vector<xml_node<>*> styles;

protected:
	int LoadPages(xml_node<>* pages);
	int LoadVariables(xml_node<>* vars);

protected:
	char* mXmlFile;
	xml_document<> mDoc;
	ResourceManager* mResources;
	std::vector<Page*> mPages;
	std::vector<xml_node<>*> templates;
	Page* mCurrentPage;
	std::vector<Page*> mOverlays; // Special case for popup dialogs and the lock screen
	std::vector<xml_document<>*> mIncludedDocs;
};

class PageManager
{
public:
	// Used by GUI
	static int LoadPackage(std::string name, std::string package, std::string startpage);
	static PageSet* SelectPackage(std::string name);
	static int ReloadPackage(std::string name, std::string package);
	static void ReleasePackage(std::string name);

	// Used for actions and pages
	static int ChangePage(std::string name);
	static int ChangeOverlay(std::string name);
	static const ResourceManager* GetResources();

	// Used for console-only mode
	static int SwitchToConsole(void);
	static int EndConsole(void);

	// Helper to identify if a particular page is the active page
	static int IsCurrentPage(Page* page);

	// These are routing routines
	static int Render(void);
	static int Update(void);
	static int NotifyTouch(TOUCH_STATE state, int x, int y);
	static int NotifyKey(int key, bool down);
	static int NotifyKeyboard(int key);
	static int SetKeyBoardFocus(int inFocus);
	static int NotifyVarChange(std::string varName, std::string value);

	static MouseCursor *GetMouseCursor();
	static void LoadCursorData(xml_node<>* node);

	static HardwareKeyboard *GetHardwareKeyboard();

	static xml_node<>* FindStyle(std::string name);

protected:
	static PageSet* FindPackage(std::string name);

protected:
	static std::map<std::string, PageSet*> mPageSets;
	static PageSet* mCurrentSet;
	static PageSet* mBaseSet;
	static MouseCursor *mMouseCursor;
	static HardwareKeyboard *mHardwareKeyboard;
};

#endif  // _PAGES_HEADER_HPP
