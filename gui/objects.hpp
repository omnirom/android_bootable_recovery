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

// objects.hpp - Base classes for object manager of GUI

#ifndef _OBJECTS_HEADER
#define _OBJECTS_HEADER

#include "rapidxml.hpp"
#include <vector>
#include <string>
#include <map>
#include <set>
#include <time.h>

using namespace rapidxml;

#include "../data.hpp"
#include "resources.hpp"
#include "pages.hpp"
#include "../partitions.hpp"
#include "placement.h"

#ifndef TW_X_OFFSET
#define TW_X_OFFSET 0
#endif
#ifndef TW_Y_OFFSET
#define TW_Y_OFFSET 0
#endif

struct translate_later_struct {
	std::string resource_name; // Name of the string resource for looking up
	std::string default_value; // Default in case we don't find the string resource
	std::string color;         // Color for the console... normal, highlight, warning, error
	std::string format;        // Formatted extra variables like %i, %s
	std::string text;          // Final, translated, formatted text
	bool inline_format;        // Indicates if the final text includes an inlined format variable
};

class RenderObject
{
public:
	RenderObject() { mRenderX = 0; mRenderY = 0; mRenderW = 0; mRenderH = 0; mPlacement = TOP_LEFT; }
	virtual ~RenderObject() {}

public:
	// Render - Render the full object to the GL surface
	//  Return 0 on success, <0 on error
	virtual int Render(void) = 0;

	// Update - Update any UI component animations (called <= 30 FPS)
	//  Return 0 if nothing to update, 1 on success and contiue, >1 if full render required, and <0 on error
	virtual int Update(void) { return 0; }

	// GetRenderPos - Returns the current position of the object
	virtual int GetRenderPos(int& x, int& y, int& w, int& h) { x = mRenderX; y = mRenderY; w = mRenderW; h = mRenderH; return 0; }

	// SetRenderPos - Update the position of the object
	//  Return 0 on success, <0 on error
	virtual int SetRenderPos(int x, int y, int w = 0, int h = 0) { mRenderX = x; mRenderY = y; if (w || h) { mRenderW = w; mRenderH = h; } return 0; }

	// GetPlacement - Returns the current placement
	virtual int GetPlacement(Placement& placement) { placement = mPlacement; return 0; }

	// SetPlacement - Update the current placement
	virtual int SetPlacement(Placement placement) { mPlacement = placement; return 0; }

	// SetPageFocus - Notify when a page gains or loses focus
	// TODO: This should be named NotifyPageFocus for consistency
	virtual void SetPageFocus(int inFocus __unused) { return; }

protected:
	int mRenderX, mRenderY, mRenderW, mRenderH;
	Placement mPlacement;
};

class ActionObject
{
public:
	ActionObject() { mActionX = 0; mActionY = 0; mActionW = 0; mActionH = 0; }
	virtual ~ActionObject() {}

public:
	// NotifyTouch - Notify of a touch event
	//  Return 0 on success, >0 to ignore remainder of touch, and <0 on error
	virtual int NotifyTouch(TOUCH_STATE state __unused, int x __unused, int y __unused) { return 0; }

	// NotifyKey - Notify of a key press
	//  Return 0 on success (and consume key), >0 to pass key to next handler, and <0 on error
	virtual int NotifyKey(int key __unused, bool down __unused) { return 1; }

	virtual int GetActionPos(int& x, int& y, int& w, int& h) { x = mActionX; y = mActionY; w = mActionW; h = mActionH; return 0; }

	//  Return 0 on success, <0 on error
	virtual int SetActionPos(int x, int y, int w = 0, int h = 0);

	// IsInRegion - Checks if the request is handled by this object
	//  Return 1 if this object handles the request, 0 if not
	virtual int IsInRegion(int x, int y) { return ((x < mActionX || x >= mActionX + mActionW || y < mActionY || y >= mActionY + mActionH) ? 0 : 1); }

protected:
	int mActionX, mActionY, mActionW, mActionH;
};

class GUIObject
{
public:
	GUIObject(xml_node<>* node);
	virtual ~GUIObject();

public:
	bool IsConditionVariable(std::string var);
	bool isConditionTrue();
	bool isConditionValid();

	// NotifyVarChange - Notify of a variable change
	//  Returns 0 on success, <0 on error
	virtual int NotifyVarChange(const std::string& varName, const std::string& value);

protected:
	class Condition
	{
	public:
		Condition() {
			mLastResult = true;
		}

		std::string mVar1;
		std::string mVar2;
		std::string mCompareOp;
		std::string mLastVal;
		bool mLastResult;
	};

	std::vector<Condition> mConditions;

protected:
	static void LoadConditions(xml_node<>* node, std::vector<Condition>& conditions);
	static bool isMounted(std::string vol);
	static bool isConditionTrue(Condition* condition);
	static bool UpdateConditions(std::vector<Condition>& conditions, const std::string& varName);

	bool mConditionsResult;
};

class InputObject
{
public:
	InputObject() { HasInputFocus = 0; }
	virtual ~InputObject() {}

public:
	// NotifyCharInput - Notify of character input (usually from the onscreen or hardware keyboard)
	//  Return 0 on success (and consume key), >0 to pass key to next handler, and <0 on error
	virtual int NotifyCharInput(int ch __unused) { return 1; }

	virtual int SetInputFocus(int focus) { HasInputFocus = focus; return 1; }

protected:
	int HasInputFocus;
};

// Derived Objects
// GUIText - Used for static text
class GUIText : public GUIObject, public RenderObject, public ActionObject
{
public:
	// w and h may be ignored, in which case, no bounding box is applied
	GUIText(xml_node<>* node);

public:
	// Render - Render the full object to the GL surface
	//  Return 0 on success, <0 on error
	virtual int Render(void);

	// Update - Update any UI component animations (called <= 30 FPS)
	//  Return 0 if nothing to update, 1 on success and contiue, >1 if full render required, and <0 on error
	virtual int Update(void);

	// Retrieve the size of the current string (dynamic strings may change per call)
	virtual int GetCurrentBounds(int& w, int& h);

	// Notify of a variable change
	virtual int NotifyVarChange(const std::string& varName, const std::string& value);

	// Set maximum width in pixels
	virtual int SetMaxWidth(unsigned width);

	// Set number of characters to skip (for scrolling)
	virtual int SkipCharCount(unsigned skip);

public:
	bool isHighlighted;
	bool scaleWidth;
	unsigned maxWidth;

protected:
	std::string mText;
	std::string mLastValue;
	COLOR mColor;
	COLOR mHighlightColor;
	FontResource* mFont;
	int mIsStatic;
	int mVarChanged;
	int mFontHeight;
	unsigned charSkip;
};

// GUIImage - Used for static image
class GUIImage : public GUIObject, public RenderObject
{
public:
	GUIImage(xml_node<>* node);

public:
	// Render - Render the full object to the GL surface
	//  Return 0 on success, <0 on error
	virtual int Render(void);

	// SetRenderPos - Update the position of the object
	//  Return 0 on success, <0 on error
	virtual int SetRenderPos(int x, int y, int w = 0, int h = 0);

public:
	bool isHighlighted;

protected:
	ImageResource* mImage;
	ImageResource* mHighlightImage;
};

// GUIFill - Used for fill colors
class GUIFill : public GUIObject, public RenderObject
{
public:
	GUIFill(xml_node<>* node);

public:
	// Render - Render the full object to the GL surface
	//  Return 0 on success, <0 on error
	virtual int Render(void);

protected:
	COLOR mColor;
};

// GUIAction - Used for standard actions
class GUIAction : public GUIObject, public ActionObject
{
	friend class ActionThread;

public:
	GUIAction(xml_node<>* node);

public:
	virtual int NotifyTouch(TOUCH_STATE state, int x, int y);
	virtual int NotifyKey(int key, bool down);
	virtual int NotifyVarChange(const std::string& varName, const std::string& value);

	int doActions();

protected:
	class Action
	{
	public:
		std::string mFunction;
		std::string mArg;
	};

	std::vector<Action> mActions;
	std::map<int, bool> mKeys;

protected:
	enum ThreadType { THREAD_NONE, THREAD_ACTION, THREAD_CANCEL };

	int getKeyByName(std::string key);
	int doAction(Action action);
	ThreadType getThreadType(const Action& action);
	void simulate_progress_bar(void);
	int flash_zip(std::string filename, int* wipe_cache);
	void reinject_after_flash();
	void operation_start(const string operation_name);
	void operation_end(const int operation_status);
	time_t Start;

	// map action name to function pointer
	typedef int (GUIAction::*execFunction)(std::string);
	typedef std::map<std::string, execFunction> mapFunc;
	static mapFunc mf;
	static std::set<std::string> setActionsRunningInCallerThread;

	// GUI actions
	int reboot(std::string arg);
	int home(std::string arg);
	int key(std::string arg);
	int page(std::string arg);
	int reload(std::string arg);
	int readBackup(std::string arg);
	int set(std::string arg);
	int clear(std::string arg);
	int mount(std::string arg);
	int unmount(std::string arg);
	int restoredefaultsettings(std::string arg);
	int copylog(std::string arg);
	int compute(std::string arg);
	int setguitimezone(std::string arg);
	int overlay(std::string arg);
	int queuezip(std::string arg);
	int cancelzip(std::string arg);
	int queueclear(std::string arg);
	int sleep(std::string arg);
	int appenddatetobackupname(std::string arg);
	int generatebackupname(std::string arg);
	int checkpartitionlist(std::string arg);
	int getpartitiondetails(std::string arg);
	int screenshot(std::string arg);
	int setbrightness(std::string arg);

	// (originally) threaded actions
	int fileexists(std::string arg);
	int flash(std::string arg);
	int wipe(std::string arg);
	int refreshsizes(std::string arg);
	int nandroid(std::string arg);
	int fixpermissions(std::string arg);
	int dd(std::string arg);
	int partitionsd(std::string arg);
	int installhtcdumlock(std::string arg);
	int htcdumlockrestoreboot(std::string arg);
	int htcdumlockreflashrecovery(std::string arg);
	int cmd(std::string arg);
	int terminalcommand(std::string arg);
	int killterminal(std::string arg);
	int reinjecttwrp(std::string arg);
	int checkbackupname(std::string arg);
	int decrypt(std::string arg);
	int adbsideload(std::string arg);
	int adbsideloadcancel(std::string arg);
	int openrecoveryscript(std::string arg);
	int installsu(std::string arg);
	int fixsu(std::string arg);
	int decrypt_backup(std::string arg);
	int repair(std::string arg);
	int resize(std::string arg);
	int changefilesystem(std::string arg);
	int startmtp(std::string arg);
	int stopmtp(std::string arg);
	int flashimage(std::string arg);
	int cancelbackup(std::string arg);
	int checkpartitionlifetimewrites(std::string arg);
	int mountsystemtoggle(std::string arg);
	int setlanguage(std::string arg);
	int twcmd(std::string arg);

	int simulate;
};

class GUIButton : public GUIObject, public RenderObject, public ActionObject
{
public:
	GUIButton(xml_node<>* node);
	virtual ~GUIButton();

public:
	// Render - Render the full object to the GL surface
	//  Return 0 on success, <0 on error
	virtual int Render(void);

	// Update - Update any UI component animations (called <= 30 FPS)
	//  Return 0 if nothing to update, 1 on success and contiue, >1 if full render required, and <0 on error
	virtual int Update(void);

	// SetPos - Update the position of the render object
	//  Return 0 on success, <0 on error
	virtual int SetRenderPos(int x, int y, int w = 0, int h = 0);

	// NotifyTouch - Notify of a touch event
	//  Return 0 on success, >0 to ignore remainder of touch, and <0 on error
	virtual int NotifyTouch(TOUCH_STATE state, int x, int y);

protected:
	GUIImage* mButtonImg;
	ImageResource* mButtonIcon;
	GUIText* mButtonLabel;
	GUIAction* mAction;
	int mTextX, mTextY, mTextW, mTextH;
	int mIconX, mIconY, mIconW, mIconH;
	bool mRendered;
	bool hasHighlightColor;
	bool renderHighlight;
	bool hasFill;
	COLOR mFillColor;
	COLOR mHighlightColor;
	Placement TextPlacement;
};

class GUICheckbox: public GUIObject, public RenderObject, public ActionObject
{
public:
	GUICheckbox(xml_node<>* node);
	virtual ~GUICheckbox();

public:
	// Render - Render the full object to the GL surface
	//  Return 0 on success, <0 on error
	virtual int Render(void);

	// Update - Update any UI component animations (called <= 30 FPS)
	//  Return 0 if nothing to update, 1 on success and contiue, >1 if full render required, and <0 on error
	virtual int Update(void);

	// SetPos - Update the position of the render object
	//  Return 0 on success, <0 on error
	virtual int SetRenderPos(int x, int y, int w = 0, int h = 0);

	// NotifyTouch - Notify of a touch event
	//  Return 0 on success, >0 to ignore remainder of touch, and <0 on error
	virtual int NotifyTouch(TOUCH_STATE state, int x, int y);

protected:
	ImageResource* mChecked;
	ImageResource* mUnchecked;
	GUIText* mLabel;
	int mTextX, mTextY;
	int mCheckX, mCheckY, mCheckW, mCheckH;
	int mLastState;
	bool mRendered;
	std::string mVarName;
};

class GUIScrollList : public GUIObject, public RenderObject, public ActionObject
{
public:
	GUIScrollList(xml_node<>* node);
	virtual ~GUIScrollList();

public:
	// Render - Render the full object to the GL surface
	//  Return 0 on success, <0 on error
	virtual int Render(void);

	// Update - Update any UI component animations (called <= 30 FPS)
	//  Return 0 if nothing to update, 1 on success and contiue, >1 if full render required, and <0 on error
	virtual int Update(void);

	// NotifyTouch - Notify of a touch event
	//  Return 0 on success, >0 to ignore remainder of touch, and <0 on error
	virtual int NotifyTouch(TOUCH_STATE state, int x, int y);

	// NotifyVarChange - Notify of a variable change
	virtual int NotifyVarChange(const std::string& varName, const std::string& value);

	// SetPos - Update the position of the render object
	//  Return 0 on success, <0 on error
	virtual int SetRenderPos(int x, int y, int w = 0, int h = 0);

	// SetPageFocus - Notify when a page gains or loses focus
	virtual void SetPageFocus(int inFocus);

protected:
	// derived classes need to implement these
	// get number of items
	virtual size_t GetItemCount() { return 0; }
	// render a single item in rect (mRenderX, yPos, mRenderW, actualItemHeight)
	virtual void RenderItem(size_t itemindex, int yPos, bool selected);
	// an item was selected
	virtual void NotifySelect(size_t item_selected __unused) {}

	// render a standard-layout list item with optional icon and text
	void RenderStdItem(int yPos, bool selected, ImageResource* icon, const char* text, int iconAndTextH = 0);

	enum { NO_ITEM = (size_t)-1 };
	// returns item index at coordinates or NO_ITEM if there is no item there
	size_t HitTestItem(int x, int y);

	// Called by the derived class to set the max icon size so we can calculate the proper actualItemHeight and center smaller icons if the icon size varies
	void SetMaxIconSize(int w, int h);

	// This will make sure that the item indicated by list_index is visible on the screen
	void SetVisibleListLocation(size_t list_index);

	// Handle scrolling changes for drags and kinetic scrolling
	void HandleScrolling();

	// Returns many full rows the list is capable of displaying
	int GetDisplayItemCount();

	// Returns the size in pixels of a partial item or row size
	int GetDisplayRemainder();

protected:
	// Background
	COLOR mBackgroundColor;
	ImageResource* mBackground; // background image, if any, automatically centered

	// Header
	COLOR mHeaderBackgroundColor;
	COLOR mHeaderFontColor;
	std::string mHeaderText; // Original header text without parsing any variables
	std::string mLastHeaderValue; // Header text after parsing variables
	bool mHeaderIsStatic; // indicates if the header is static (no need to check for changes in NotifyVarChange)
	int mHeaderH; // actual header height including font, icon, padding, and separator heights
	ImageResource* mHeaderIcon;
	int mHeaderIconHeight, mHeaderIconWidth; // width and height of the header icon if present
	int mHeaderSeparatorH; // Height of the separator between header and list items
	COLOR mHeaderSeparatorColor; // color of the header separator

	// Per-item layout
	FontResource* mFont;
	COLOR mFontColor;
	bool hasHighlightColor; // indicates if a highlight color was set
	COLOR mHighlightColor; // background row highlight color
	COLOR mFontHighlightColor;
	int mFontHeight;
	int actualItemHeight; // Actual height of each item in pixels including max icon size, font size, and padding
	int maxIconWidth, maxIconHeight; // max icon width and height for the list, set by derived class in SetMaxIconSize
	int mItemSpacing; // stores the spacing or padding on the y axis, part of the actualItemHeight
	int mSeparatorH; // Height of the separator between items
	COLOR mSeparatorColor; // color of the separator that is between items

	// Scrollbar
	int mFastScrollW; // width of the fastscroll area
	int mFastScrollLineW; // width of the line for fastscroll rendering
	int mFastScrollRectW; // width of the rectangle for fastscroll
	int mFastScrollRectH; // minimum height of the rectangle for fastscroll
	COLOR mFastScrollLineColor;
	COLOR mFastScrollRectColor;

	// Scrolling and dynamic state
	int mFastScrollRectCurrentY; // current top of fastscroll rect relative to list top
	int mFastScrollRectCurrentH; // current height of fastscroll rect
	int mFastScrollRectTouchY; // offset from top of fastscroll rect where the user initially touched
	bool hasScroll; // indicates that we have enough items in the list to scroll
	int firstDisplayedItem; // this item goes at the top of the display list - may only be partially visible
	int scrollingSpeed; // on a touch release, this is set based on the difference in the y-axis between the last 2 touches and indicates how fast the kinetic scrolling will go
	int y_offset; // this is how many pixels offset in the y axis for per pixel scrolling, is always <= 0 and should never be < -actualItemHeight
	bool allowSelection; // true if touched item can be selected, false for pure read-only lists and the console
	size_t selectedItem; // selected item index after the initial touch, set to -1 if we are scrolling
	int touchDebounce; // debounce for touches, minimum of 6 pixels but may be larger calculated based actualItemHeight / 3
	int lastY, last2Y; // last 2 touch locations, used for tracking kinetic scroll speed
	int fastScroll; // indicates that the inital touch was inside the fastscroll region - makes for easier fast scrolling as the touches don't have to stay within the fast scroll region and you drag your finger
	int mUpdate; // indicates that a change took place and we need to re-render
	bool AddLines(std::vector<std::string>* origText, std::vector<std::string>* origColor, size_t* lastCount, std::vector<std::string>* rText, std::vector<std::string>* rColor);
};

class GUIFileSelector : public GUIScrollList
{
public:
	GUIFileSelector(xml_node<>* node);
	virtual ~GUIFileSelector();

public:
	// Update - Update any UI component animations (called <= 30 FPS)
	//  Return 0 if nothing to update, 1 on success and contiue, >1 if full render required, and <0 on error
	virtual int Update(void);

	// NotifyVarChange - Notify of a variable change
	virtual int NotifyVarChange(const std::string& varName, const std::string& value);

	// SetPageFocus - Notify when a page gains or loses focus
	virtual void SetPageFocus(int inFocus);

	virtual size_t GetItemCount();
	virtual void RenderItem(size_t itemindex, int yPos, bool selected);
	virtual void NotifySelect(size_t item_selected);

protected:
	struct FileData {
		std::string fileName;
		unsigned char fileType;	 // Uses d_type format from struct dirent
		mode_t protection;		  // Uses mode_t format from stat
		uid_t userId;
		gid_t groupId;
		off_t fileSize;
		time_t lastAccess;		  // Uses time_t format from stat
		time_t lastModified;		// Uses time_t format from stat
		time_t lastStatChange;	  // Uses time_t format from stat
	};

protected:
	virtual int GetFileList(const std::string folder);
	static bool fileSort(FileData d1, FileData d2);

protected:
	std::vector<FileData> mFolderList;
	std::vector<FileData> mFileList;
	std::string mPathVar; // current path displayed, saved in the data manager
	std::string mPathDefault; // default value for the path if none is set in mPathVar
	std::string mExtn; // used for filtering the file list, for example, *.zip
	std::string mVariable; // set when the user selects an item, pull path like /path/to/foo
	std::string mSortVariable; // data manager variable used to change the sorting of files
	std::string mSelection; // set when the user selects an item without the full path like selecting /path/to/foo would just be set to foo
	int mShowFolders, mShowFiles; // indicates if the list should show folders and/or files
	int mShowNavFolders; // indicates if the list should include the "up a level" item and allow you to traverse folders (nav folders are disabled for the restore list, for instance)
	static int mSortOrder; // must be static because it is used by the static function fileSort
	ImageResource* mFolderIcon;
	ImageResource* mFileIcon;
	bool updateFileList;
};

class GUIListBox : public GUIScrollList
{
public:
	GUIListBox(xml_node<>* node);
	virtual ~GUIListBox();

public:
	// Update - Update any UI component animations (called <= 30 FPS)
	//  Return 0 if nothing to update, 1 on success and contiue, >1 if full render required, and <0 on error
	virtual int Update(void);

	// NotifyVarChange - Notify of a variable change
	virtual int NotifyVarChange(const std::string& varName, const std::string& value);

	// SetPageFocus - Notify when a page gains or loses focus
	virtual void SetPageFocus(int inFocus);

	virtual size_t GetItemCount();
	virtual void RenderItem(size_t itemindex, int yPos, bool selected);
	virtual void NotifySelect(size_t item_selected);

protected:
	struct ListItem {
		std::string displayName;
		std::string variableName;
		std::string variableValue;
		unsigned int selected;
		GUIAction* action;
		std::vector<Condition> mConditions;
	};

protected:
	std::vector<ListItem> mListItems;
	std::vector<size_t> mVisibleItems; // contains indexes in mListItems of visible items only
	std::string mVariable;
	std::string currentValue;
	ImageResource* mIconSelected;
	ImageResource* mIconUnselected;
	bool isCheckList;
	bool isTextParsed;
};

class GUIPartitionList : public GUIScrollList
{
public:
	GUIPartitionList(xml_node<>* node);
	virtual ~GUIPartitionList();

public:
	// Update - Update any UI component animations (called <= 30 FPS)
	//  Return 0 if nothing to update, 1 on success and contiue, >1 if full render required, and <0 on error
	virtual int Update();

	// NotifyVarChange - Notify of a variable change
	virtual int NotifyVarChange(const std::string& varName, const std::string& value);

	// SetPageFocus - Notify when a page gains or loses focus
	virtual void SetPageFocus(int inFocus);

	virtual size_t GetItemCount();
	virtual void RenderItem(size_t itemindex, int yPos, bool selected);
	virtual void NotifySelect(size_t item_selected);

protected:
	void MatchList();
	void SetPosition();

protected:
	std::vector<PartitionList> mList;
	std::string ListType;
	std::string mVariable;
	std::string selectedList;
	std::string currentValue;
	std::string mLastValue;
	ImageResource* mIconSelected;
	ImageResource* mIconUnselected;
	bool updateList;
};

class GUITextBox : public GUIScrollList
{
public:
	GUITextBox(xml_node<>* node);

public:
	// Update - Update any UI component animations (called <= 30 FPS)
	//  Return 0 if nothing to update, 1 on success and contiue, >1 if full render required, and <0 on error
	virtual int Update(void);

	// NotifyVarChange - Notify of a variable change
	virtual int NotifyVarChange(const std::string& varName, const std::string& value);

	// ScrollList interface
	virtual size_t GetItemCount();
	virtual void RenderItem(size_t itemindex, int yPos, bool selected);
	virtual void NotifySelect(size_t item_selected);
protected:

	size_t mLastCount;
	bool mIsStatic;
	std::vector<std::string> mLastValue; // Parsed text - parsed for variables but not word wrapped
	std::vector<std::string> mText;      // Original text - not parsed for variables and not word wrapped
	std::vector<std::string> rText;      // Rendered text - what we actually see

};

class GUIConsole : public GUIScrollList
{
public:
	GUIConsole(xml_node<>* node);

public:
	// Render - Render the full object to the GL surface
	//  Return 0 on success, <0 on error
	virtual int Render(void);

	// Update - Update any UI component animations (called <= 30 FPS)
	//  Return 0 if nothing to update, 1 on success and contiue, >1 if full render required, and <0 on error
	virtual int Update(void);

	// IsInRegion - Checks if the request is handled by this object
	//  Return 1 if this object handles the request, 0 if not
	virtual int IsInRegion(int x, int y);

	// NotifyTouch - Notify of a touch event
	//  Return 0 on success, >0 to ignore remainder of touch, and <0 on error (Return error to allow other handlers)
	virtual int NotifyTouch(TOUCH_STATE state, int x, int y);

	// ScrollList interface
	virtual size_t GetItemCount();
	virtual void RenderItem(size_t itemindex, int yPos, bool selected);
	virtual void NotifySelect(size_t item_selected);
	static void Translate_Now();
protected:
	enum SlideoutState
	{
		hidden = 0,
		visible,
		request_hide,
		request_show
	};

	ImageResource* mSlideoutImage;
	size_t mLastCount; // lines from gConsole that are already split and copied into rConsole
	bool scrollToEnd; // true if we want to keep tracking the last line
	int mSlideoutX, mSlideoutY, mSlideoutW, mSlideoutH;
	int mSlideout;
	SlideoutState mSlideoutState;
	std::vector<std::string> rConsole;
	std::vector<std::string> rConsoleColor;

protected:
	int RenderSlideout(void);
	int RenderConsole(void);
};

class TerminalEngine;
class GUITerminal : public GUIScrollList, public InputObject
{
public:
	GUITerminal(xml_node<>* node);

public:
	// Update - Update any UI component animations (called <= 30 FPS)
	//  Return 0 if nothing to update, 1 on success and contiue, >1 if full render required, and <0 on error
	virtual int Update(void);

	// NotifyTouch - Notify of a touch event
	//  Return 0 on success, >0 to ignore remainder of touch, and <0 on error (Return error to allow other handlers)
	virtual int NotifyTouch(TOUCH_STATE state, int x, int y);

	// NotifyKey - Notify of a key press
	//  Return 0 on success (and consume key), >0 to pass key to next handler, and <0 on error
	virtual int NotifyKey(int key, bool down);

	// character input
	virtual int NotifyCharInput(int ch);

	// SetPageFocus - Notify when a page gains or loses focus
	virtual void SetPageFocus(int inFocus);

	// ScrollList interface
	virtual size_t GetItemCount();
	virtual void RenderItem(size_t itemindex, int yPos, bool selected);
	virtual void NotifySelect(size_t item_selected);
protected:
	void InitAndResize();

	TerminalEngine* engine; // non-visual parts of the terminal (text buffer etc.), not owned
	int updateCounter; // to track if anything changed in the back-end
	bool lastCondition; // to track if the condition became true and we might need to resize the terminal engine
};

// GUIAnimation - Used for animations
class GUIAnimation : public GUIObject, public RenderObject
{
public:
	GUIAnimation(xml_node<>* node);

public:
	// Render - Render the full object to the GL surface
	//  Return 0 on success, <0 on error
	virtual int Render(void);

	// Update - Update any UI component animations (called <= 30 FPS)
	//  Return 0 if nothing to update, 1 on success and contiue, >1 if full render required, and <0 on error
	virtual int Update(void);

protected:
	AnimationResource* mAnimation;
	int mFrame;
	int mFPS;
	int mLoop;
	int mRender;
	int mUpdateCount;
};

class GUIProgressBar : public GUIObject, public RenderObject, public ActionObject
{
public:
	GUIProgressBar(xml_node<>* node);

public:
	// Render - Render the full object to the GL surface
	//  Return 0 on success, <0 on error
	virtual int Render(void);

	// Update - Update any UI component animations (called <= 30 FPS)
	//  Return 0 if nothing to update, 1 on success and contiue, >1 if full render required, and <0 on error
	virtual int Update(void);

	// NotifyVarChange - Notify of a variable change
	//  Returns 0 on success, <0 on error
	virtual int NotifyVarChange(const std::string& varName, const std::string& value);

protected:
	ImageResource* mEmptyBar;
	ImageResource* mFullBar;
	std::string mMinValVar;
	std::string mMaxValVar;
	std::string mCurValVar;
	float mSlide;
	float mSlideInc;
	int mSlideFrames;
	int mLastPos;

protected:
	virtual int RenderInternal(void);	   // Does the actual render
};

class GUISlider : public GUIObject, public RenderObject, public ActionObject
{
public:
	GUISlider(xml_node<>* node);
	virtual ~GUISlider();

public:
	// Render - Render the full object to the GL surface
	//  Return 0 on success, <0 on error
	virtual int Render(void);

	// Update - Update any UI component animations (called <= 30 FPS)
	//  Return 0 if nothing to update, 1 on success and contiue, >1 if full render required, and <0 on error
	virtual int Update(void);

	// NotifyTouch - Notify of a touch event
	//  Return 0 on success, >0 to ignore remainder of touch, and <0 on error
	virtual int NotifyTouch(TOUCH_STATE state, int x, int y);

protected:
	GUIAction* sAction;
	GUIText* sSliderLabel;
	ImageResource* sSlider;
	ImageResource* sSliderUsed;
	ImageResource* sTouch;
	int sTouchW, sTouchH;
	int sCurTouchX;
	int sUpdate;
};

// these are ASCII codes reported via NotifyCharInput
// other special keys (arrows etc.) are reported via NotifyKey
#define KEYBOARD_ACTION 13	// CR
#define KEYBOARD_BACKSPACE 8	// Backspace
#define KEYBOARD_TAB 9		// Tab
#define KEYBOARD_SWIPE_LEFT 21	// Ctrl+U to delete line, same as in readline (used by shell etc.)
#define KEYBOARD_SWIPE_RIGHT 11	// Ctrl+K, same as in readline

class GUIKeyboard : public GUIObject, public RenderObject, public ActionObject
{
public:
	GUIKeyboard(xml_node<>* node);
	virtual ~GUIKeyboard();

public:
	virtual int Render(void);
	virtual int Update(void);
	virtual int NotifyTouch(TOUCH_STATE state, int x, int y);
	virtual int SetRenderPos(int x, int y, int w = 0, int h = 0);
	virtual void SetPageFocus(int inFocus);

protected:
	struct Key
	{
		int key; // positive: ASCII/Unicode code; negative: Linux key code (KEY_*)
		int longpresskey;
		int end_x;
		int layout;
	};
	int ParseKey(const char* keyinfo, Key& key, int& Xindex, int keyWidth, bool longpress);
	void LoadKeyLabels(xml_node<>* parent, int layout);
	void DrawKey(Key& key, int keyX, int keyY, int keyW, int keyH);
	int KeyCharToCtrlChar(int key);

	enum {
		MAX_KEYBOARD_LAYOUTS = 5,
		MAX_KEYBOARD_ROWS = 9,
		MAX_KEYBOARD_KEYS = 20
	};
	struct Layout
	{
		ImageResource* keyboardImg;
		Key keys[MAX_KEYBOARD_ROWS][MAX_KEYBOARD_KEYS];
		int row_end_y[MAX_KEYBOARD_ROWS];
		bool is_caps;
		int revert_layout;
	};
	Layout layouts[MAX_KEYBOARD_LAYOUTS];

	struct KeyLabel
	{
		int key; // same as in struct Key
		int layout_from; // 1-based; 0 for labels that apply to all layouts
		int layout_to; // same as Key.layout
		string text; // key label text
		ImageResource* image; // image (overrides text if defined)
	};
	std::vector<KeyLabel> mKeyLabels;

	// Find key at screen coordinates
	Key* HitTestKey(int x, int y);

	bool mRendered;
	std::string mVariable;
	int currentLayout;
	bool CapsLockOn;
	static bool CtrlActive; // all keyboards share a common Control key state so that the Control key can be on a separate keyboard instance
	int highlightRenderCount;
	Key* currentKey;
	bool hasHighlight, hasCapsHighlight, hasCtrlHighlight;
	COLOR mHighlightColor;
	COLOR mCapsHighlightColor;
	COLOR mCtrlHighlightColor;
	COLOR mFontColor; // for centered key labels
	COLOR mFontColorSmall; // for centered key labels
	FontResource* mFont; // for main key labels
	FontResource* mSmallFont; // for key labels like "?123"
	FontResource* mLongpressFont; // for the small longpress label in the upper right corner
	int longpressOffsetX, longpressOffsetY; // distance of the longpress label from the key corner
	COLOR mLongpressFontColor;
	COLOR mBackgroundColor; // keyboard background color
	COLOR mKeyColorAlphanumeric; // key background color
	COLOR mKeyColorOther; // key background color
	int mKeyMarginX, mKeyMarginY; // space around key boxes - applied to left/right and top/bottom
};

// GUIInput - Used for keyboard input
class GUIInput : public GUIObject, public RenderObject, public ActionObject, public InputObject
{
public:
	GUIInput(xml_node<>* node);
	virtual ~GUIInput();

public:
	// Render - Render the full object to the GL surface
	//  Return 0 on success, <0 on error
	virtual int Render(void);

	// Update - Update any UI component animations (called <= 30 FPS)
	//  Return 0 if nothing to update, 1 on success and contiue, >1 if full render required, and <0 on error
	virtual int Update(void);

	// Notify of a variable change
	virtual int NotifyVarChange(const std::string& varName, const std::string& value);

	// NotifyTouch - Notify of a touch event
	//  Return 0 on success, >0 to ignore remainder of touch, and <0 on error
	virtual int NotifyTouch(TOUCH_STATE state, int x, int y);

	virtual int NotifyKey(int key, bool down);
	virtual int NotifyCharInput(int ch);

protected:
	virtual int GetSelection(int x, int y);

	// Handles displaying the text properly when chars are added, deleted, or for scrolling
	virtual int HandleTextLocation(int x);

protected:
	GUIText* mInputText;
	GUIAction* mAction;
	ImageResource* mBackground;
	ImageResource* mCursor;
	FontResource* mFont;
	std::string mText;
	std::string mLastValue;
	std::string mVariable;
	std::string mMask;
	std::string mMaskVariable;
	COLOR mBackgroundColor;
	COLOR mCursorColor;
	int scrollingX;
	int lastX;
	int mCursorLocation;
	int mBackgroundX, mBackgroundY, mBackgroundW, mBackgroundH;
	int mFontY;
	unsigned skipChars;
	unsigned mFontHeight;
	unsigned CursorWidth;
	bool mRendered;
	bool HasMask;
	bool DrawCursor;
	bool isLocalChange;
	bool HasAllowed;
	bool HasDisabled;
	std::string AllowedList;
	std::string DisabledList;
	unsigned MinLen;
	unsigned MaxLen;
};

class HardwareKeyboard
{
public:
	HardwareKeyboard();
	virtual ~HardwareKeyboard();

public:
	// called by the input handler for key events
	int KeyDown(int key_code);
	int KeyUp(int key_code);

	// called by the input handler when holding a key down
	int KeyRepeat();

	// called by multi-key actions to suppress key-release notifications
	void ConsumeKeyRelease(int key);

	bool IsKeyDown(int key_code);
private:
	int mLastKey;
	int mLastKeyChar;
	std::set<int> mPressedKeys;
};

class GUISliderValue: public GUIObject, public RenderObject, public ActionObject
{
public:
	GUISliderValue(xml_node<>* node);
	virtual ~GUISliderValue();

public:
	// Render - Render the full object to the GL surface
	//  Return 0 on success, <0 on error
	virtual int Render(void);

	// Update - Update any UI component animations (called <= 30 FPS)
	//  Return 0 if nothing to update, 1 on success and contiue, >1 if full render required, and <0 on error
	virtual int Update(void);

	// SetPos - Update the position of the render object
	//  Return 0 on success, <0 on error
	virtual int SetRenderPos(int x, int y, int w = 0, int h = 0);

	// NotifyTouch - Notify of a touch event
	//  Return 0 on success, >0 to ignore remainder of touch, and <0 on error
	virtual int NotifyTouch(TOUCH_STATE state, int x, int y);

	// Notify of a variable change
	virtual int NotifyVarChange(const std::string& varName, const std::string& value);

	// SetPageFocus - Notify when a page gains or loses focus
	virtual void SetPageFocus(int inFocus);

protected:
	int measureText(const std::string& str);
	int valueFromPct(float pct);
	float pctFromValue(int value);
	void loadValue(bool force = false);

	std::string mVariable;
	int mMax;
	int mMin;
	int mValue;
	char *mValueStr;
	float mValuePct;
	std::string mMaxStr;
	std::string mMinStr;
	FontResource *mFont;
	GUIText* mLabel;
	int mLabelW;
	COLOR mTextColor;
	COLOR mLineColor;
	COLOR mSliderColor;
	bool mShowRange;
	bool mShowCurr;
	int mLineX;
	int mLineY;
	int mLineH;
	int mLinePadding;
	int mPadding;
	int mSliderY;
	int mSliderW;
	int mSliderH;
	bool mRendered;
	int mFontHeight;
	GUIAction *mAction;
	bool mChangeOnDrag;
	int mLineW;
	bool mDragging;
	ImageResource *mBackgroundImage;
	ImageResource *mHandleImage;
	ImageResource *mHandleHoverImage;
};

class MouseCursor : public RenderObject
{
public:
	MouseCursor(int posX, int posY);
	virtual ~MouseCursor();

	virtual int Render(void);
	virtual int Update(void);
	virtual int SetRenderPos(int x, int y, int w = 0, int h = 0);

	void Move(int deltaX, int deltaY);
	void GetPos(int& x, int& y);
	void LoadData(xml_node<>* node);
	void ResetData(int resX, int resY);

private:
	int m_resX;
	int m_resY;
	bool m_moved;
	float m_speedMultiplier;
	COLOR m_color;
	ImageResource *m_image;
	bool m_present;
};

class GUIPatternPassword : public GUIObject, public RenderObject, public ActionObject
{
public:
	GUIPatternPassword(xml_node<>* node);
	virtual ~GUIPatternPassword();

public:
	virtual int Render(void);
	virtual int Update(void);
	virtual int NotifyTouch(TOUCH_STATE state, int x, int y);
	virtual int NotifyVarChange(const std::string& varName, const std::string& value);
	virtual int SetRenderPos(int x, int y, int w = 0, int h = 0);

protected:
	void CalculateDotPositions();
	void ResetActiveDots();
	void ConnectDot(int dot_idx);
	void ConnectIntermediateDots(int dot_idx);
	void Resize(size_t size);
	int InDot(int x, int y);
	bool DotUsed(int dot_idx);
	std::string GeneratePassphrase();
	void PatternDrawn();

	struct Dot {
		int x;
		int y;
		bool active;
	};

	std::string mSizeVar;
	size_t mGridSize;

	Dot* mDots;
	int* mConnectedDots;
	size_t mConnectedDotsLen;
	int mCurLineX;
	int mCurLineY;
	bool mTrackingTouch;
	bool mNeedRender;

	COLOR mDotColor;
	COLOR mActiveDotColor;
	COLOR mLineColor;
	ImageResource *mDotImage;
	ImageResource *mActiveDotImage;
	gr_surface mDotCircle;
	gr_surface mActiveDotCircle;
	int mDotRadius;
	int mLineWidth;

	std::string mPassVar;
	GUIAction *mAction;
	int mUpdate;
};


// Helper APIs
xml_node<>* FindNode(xml_node<>* parent, const char* nodename, int depth = 0);
std::string LoadAttrString(xml_node<>* element, const char* attrname, const char* defaultvalue = "");
int LoadAttrInt(xml_node<>* element, const char* attrname, int defaultvalue = 0);
int LoadAttrIntScaleX(xml_node<>* element, const char* attrname, int defaultvalue = 0);
int LoadAttrIntScaleY(xml_node<>* element, const char* attrname, int defaultvalue = 0);
COLOR LoadAttrColor(xml_node<>* element, const char* attrname, bool* found_color, COLOR defaultvalue = COLOR(0,0,0,0));
COLOR LoadAttrColor(xml_node<>* element, const char* attrname, COLOR defaultvalue = COLOR(0,0,0,0));
FontResource* LoadAttrFont(xml_node<>* element, const char* attrname);
ImageResource* LoadAttrImage(xml_node<>* element, const char* attrname);
AnimationResource* LoadAttrAnimation(xml_node<>* element, const char* attrname);

bool LoadPlacement(xml_node<>* node, int* x, int* y, int* w = NULL, int* h = NULL, Placement* placement = NULL);

#endif  // _OBJECTS_HEADER

