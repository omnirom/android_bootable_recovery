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

#ifndef TW_X_OFFSET
#define TW_X_OFFSET 0
#endif
#ifndef TW_Y_OFFSET
#define TW_Y_OFFSET 0
#endif

class RenderObject
{
public:
	enum Placement {
		TOP_LEFT = 0,
		TOP_RIGHT = 1,
		BOTTOM_LEFT = 2,
		BOTTOM_RIGHT = 3,
		CENTER = 4,
		CENTER_X_ONLY = 5,
	};

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
	virtual void SetPageFocus(int inFocus) { return; }

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
	virtual int NotifyTouch(TOUCH_STATE state, int x, int y) { return 0; }

	// NotifyKey - Notify of a key press
	//  Return 0 on success (and consume key), >0 to pass key to next handler, and <0 on error
	virtual int NotifyKey(int key, bool down) { return 1; }

	// GetRenderPos - Returns the current position of the object
	virtual int GetActionPos(int& x, int& y, int& w, int& h) { x = mActionX; y = mActionY; w = mActionW; h = mActionH; return 0; }

	// SetRenderPos - Update the position of the object
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
	bool isMounted(std::string vol);
	bool isConditionTrue(Condition* condition);

	bool mConditionsResult;
};

class InputObject
{
public:
	InputObject() { HasInputFocus = 0; }
	virtual ~InputObject() {}

public:
	// NotifyKeyboard - Notify of keyboard input
	//  Return 0 on success (and consume key), >0 to pass key to next handler, and <0 on error
	virtual int NotifyKeyboard(int key) { return 1; }

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

protected:
	std::string mText;
	std::string mLastValue;
	COLOR mColor;
	COLOR mHighlightColor;
	Resource* mFont;
	int mIsStatic;
	int mVarChanged;
	int mFontHeight;
	unsigned maxWidth;
	unsigned charSkip;
	bool hasHighlightColor;

protected:
	std::string parseText(void);
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
	Resource* mImage;
	Resource* mHighlightImage;
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
	int getKeyByName(std::string key);
	int doAction(Action action);
	bool needsToRunInSeparateThread(const Action& action);
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
	int changefilesystem(std::string arg);
	int startmtp(std::string arg);
	int stopmtp(std::string arg);
	int flashimage(std::string arg);
	int cancelbackup(std::string arg);

	int simulate;
};

class ActionThread
{
public:
	ActionThread();
	~ActionThread();

	void threadActions(GUIAction *act);
	void run(void *data);
private:
	struct ThreadData
	{
		GUIAction *act;
	};

	pthread_t m_thread;
	bool m_thread_running;
	pthread_mutex_t m_act_lock;
};

class GUIConsole : public GUIObject, public RenderObject, public ActionObject
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

	// SetRenderPos - Update the position of the object
	//  Return 0 on success, <0 on error
	virtual int SetRenderPos(int x, int y, int w = 0, int h = 0);

	// IsInRegion - Checks if the request is handled by this object
	//  Return 1 if this object handles the request, 0 if not
	virtual int IsInRegion(int x, int y);

	// NotifyTouch - Notify of a touch event
	//  Return 0 on success, >0 to ignore remainder of touch, and <0 on error (Return error to allow other handlers)
	virtual int NotifyTouch(TOUCH_STATE state, int x, int y);

protected:
	enum SlideoutState
	{
		hidden = 0,
		visible,
		request_hide,
		request_show
	};

	Resource* mFont;
	Resource* mSlideoutImage;
	COLOR mForegroundColor;
	COLOR mBackgroundColor;
	COLOR mScrollColor;
	unsigned int mFontHeight;
	int mCurrentLine;
	unsigned int mLastCount;
	unsigned int RenderCount;
	unsigned int mMaxRows;
	int mStartY;
	int mSlideoutX, mSlideoutY, mSlideoutW, mSlideoutH;
	int mSlideinX, mSlideinY, mSlideinW, mSlideinH;
	int mConsoleX, mConsoleY, mConsoleW, mConsoleH;
	int mLastTouchX, mLastTouchY;
	int mSlideout;
	SlideoutState mSlideoutState;
	std::vector<std::string> rConsole;
	std::vector<std::string> rConsoleColor;
	bool mRender;

protected:
	virtual int RenderSlideout(void);
	virtual int RenderConsole(void);
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
	Resource* mButtonIcon;
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
	Resource* mChecked;
	Resource* mUnchecked;
	GUIText* mLabel;
	int mTextX, mTextY;
	int mCheckX, mCheckY, mCheckW, mCheckH;
	int mLastState;
	bool mRendered;
	std::string mVarName;
};

class GUIFileSelector : public GUIObject, public RenderObject, public ActionObject
{
public:
	GUIFileSelector(xml_node<>* node);
	virtual ~GUIFileSelector();

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
	virtual int GetSelection(int x, int y);

	virtual int GetFileList(const std::string folder);
	static bool fileSort(FileData d1, FileData d2);

protected:
	std::vector<FileData> mFolderList;
	std::vector<FileData> mFileList;
	std::string mPathVar;
	std::string mExtn;
	std::string mVariable;
	std::string mSortVariable;
	std::string mSelection;
	std::string mHeaderText;
	std::string mLastValue;
	int actualLineHeight;
	int mStart;
	int mLineSpacing;
	int mSeparatorH;
	int mHeaderSeparatorH;
	int mShowFolders, mShowFiles, mShowNavFolders;
	int mUpdate;
	int mBackgroundX, mBackgroundY, mBackgroundW, mBackgroundH;
	int mHeaderH;
	int mFastScrollW;
	int mFastScrollLineW;
	int mFastScrollRectW;
	int mFastScrollRectH;
	int mFastScrollRectX;
	int mFastScrollRectY;
	static int mSortOrder;
	int startY;
	int scrollingSpeed;
	int scrollingY;
	int mHeaderIsStatic;
	int touchDebounce;
	unsigned mFontHeight;
	unsigned mLineHeight;
	int mIconWidth, mIconHeight, mFolderIconHeight, mFileIconHeight, mFolderIconWidth, mFileIconWidth, mHeaderIconHeight, mHeaderIconWidth;
	Resource* mHeaderIcon;
	Resource* mFolderIcon;
	Resource* mFileIcon;
	Resource* mBackground;
	Resource* mFont;
	COLOR mBackgroundColor;
	COLOR mFontColor;
	COLOR mHeaderBackgroundColor;
	COLOR mHeaderFontColor;
	COLOR mSeparatorColor;
	COLOR mHeaderSeparatorColor;
	COLOR mFastScrollLineColor;
	COLOR mFastScrollRectColor;
	bool hasHighlightColor;
	bool hasFontHighlightColor;
	bool isHighlighted;
	COLOR mHighlightColor;
	COLOR mFontHighlightColor;
	int startSelection;
	bool updateFileList;
};

class GUIListBox : public GUIObject, public RenderObject, public ActionObject
{
public:
	GUIListBox(xml_node<>* node);
	virtual ~GUIListBox();

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
	struct ListData {
		std::string displayName;
		std::string variableValue;
		unsigned int selected;
	};

protected:
	virtual int GetSelection(int x, int y);

protected:
	std::vector<ListData> mList;
	std::string mVariable;
	std::string mSelection;
	std::string currentValue;
	std::string mHeaderText;
	std::string mLastValue;
	int actualLineHeight;
	int mStart;
	int startY;
	int mSeparatorH, mHeaderSeparatorH;
	int mLineSpacing;
	int mUpdate;
	int mBackgroundX, mBackgroundY, mBackgroundW, mBackgroundH, mHeaderH;
	int mFastScrollW;
	int mFastScrollLineW;
	int mFastScrollRectW;
	int mFastScrollRectH;
	int mFastScrollRectX;
	int mFastScrollRectY;
	int mIconWidth, mIconHeight, mSelectedIconWidth, mSelectedIconHeight, mUnselectedIconWidth, mUnselectedIconHeight, mHeaderIconHeight, mHeaderIconWidth;
	int scrollingSpeed;
	int scrollingY;
	static int mSortOrder;
	unsigned mFontHeight;
	unsigned mLineHeight;
	Resource* mHeaderIcon;
	Resource* mIconSelected;
	Resource* mIconUnselected;
	Resource* mBackground;
	Resource* mFont;
	COLOR mBackgroundColor;
	COLOR mFontColor;
	COLOR mHeaderBackgroundColor;
	COLOR mHeaderFontColor;
	COLOR mSeparatorColor;
	COLOR mHeaderSeparatorColor;
	COLOR mFastScrollLineColor;
	COLOR mFastScrollRectColor;
	bool hasHighlightColor;
	bool hasFontHighlightColor;
	bool isHighlighted;
	COLOR mHighlightColor;
	COLOR mFontHighlightColor;
	int mHeaderIsStatic;
	int startSelection;
	int touchDebounce;
};

class GUIPartitionList : public GUIObject, public RenderObject, public ActionObject
{
public:
	GUIPartitionList(xml_node<>* node);
	virtual ~GUIPartitionList();

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
	virtual int GetSelection(int x, int y);
	virtual void MatchList(void);

protected:
	std::vector<PartitionList> mList;
	std::string ListType;
	std::string mVariable;
	std::string selectedList;
	std::string currentValue;
	std::string mHeaderText;
	std::string mLastValue;
	int actualLineHeight;
	int mStart;
	int startY;
	int mSeparatorH, mHeaderSeparatorH;
	int mLineSpacing;
	int mUpdate;
	int mBackgroundX, mBackgroundY, mBackgroundW, mBackgroundH, mHeaderH;
	int mFastScrollW;
	int mFastScrollLineW;
	int mFastScrollRectW;
	int mFastScrollRectH;
	int mFastScrollRectX;
	int mFastScrollRectY;
	int mIconWidth, mIconHeight, mSelectedIconWidth, mSelectedIconHeight, mUnselectedIconWidth, mUnselectedIconHeight, mHeaderIconHeight, mHeaderIconWidth;
	int scrollingSpeed;
	int scrollingY;
	static int mSortOrder;
	unsigned mFontHeight;
	unsigned mLineHeight;
	Resource* mHeaderIcon;
	Resource* mIconSelected;
	Resource* mIconUnselected;
	Resource* mBackground;
	Resource* mFont;
	COLOR mBackgroundColor;
	COLOR mFontColor;
	COLOR mHeaderBackgroundColor;
	COLOR mHeaderFontColor;
	COLOR mSeparatorColor;
	COLOR mHeaderSeparatorColor;
	COLOR mFastScrollLineColor;
	COLOR mFastScrollRectColor;
	bool hasHighlightColor;
	bool hasFontHighlightColor;
	bool isHighlighted;
	COLOR mHighlightColor;
	COLOR mFontHighlightColor;
	int mHeaderIsStatic;
	int startSelection;
	int touchDebounce;
	bool updateList;
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
	Resource* mEmptyBar;
	Resource* mFullBar;
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
	Resource* sSlider;
	Resource* sSliderUsed;
	Resource* sTouch;
	int sTouchW, sTouchH;
	int sCurTouchX;
	int sUpdate;
};

#define MAX_KEYBOARD_LAYOUTS 5
#define MAX_KEYBOARD_ROWS 9
#define MAX_KEYBOARD_KEYS 20
#define KEYBOARD_ACTION 253
#define KEYBOARD_LAYOUT 254
#define KEYBOARD_SWIPE_LEFT 252
#define KEYBOARD_SWIPE_RIGHT 251
#define KEYBOARD_ARROW_LEFT 250
#define KEYBOARD_ARROW_RIGHT 249
#define KEYBOARD_HOME 248
#define KEYBOARD_END 247
#define KEYBOARD_ARROW_UP 246
#define KEYBOARD_ARROW_DOWN 245
#define KEYBOARD_SPECIAL_KEYS 245
#define KEYBOARD_BACKSPACE 8

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

protected:
	virtual int GetSelection(int x, int y);

protected:
	struct keyboard_key_class
	{
		unsigned char key;
		unsigned char longpresskey;
		unsigned int end_x;
		unsigned int layout;
	};
	struct capslock_tracking_struct
	{
		int capslock;
		int set_capslock;
		int revert_layout;
	};

	Resource* keyboardImg[MAX_KEYBOARD_LAYOUTS];
	struct keyboard_key_class keyboard_keys[MAX_KEYBOARD_LAYOUTS][MAX_KEYBOARD_ROWS][MAX_KEYBOARD_KEYS];
	struct capslock_tracking_struct caps_tracking[MAX_KEYBOARD_LAYOUTS];
	bool mRendered;
	std::string mVariable;
	unsigned int cursorLocation;
	unsigned int currentLayout;
	unsigned int row_heights[MAX_KEYBOARD_LAYOUTS][MAX_KEYBOARD_ROWS];
	unsigned int KeyboardWidth, KeyboardHeight;
	int rowY, colX, highlightRenderCount, hasHighlight, hasCapsHighlight;
	GUIAction* mAction;
	COLOR mHighlightColor;
	COLOR mCapsHighlightColor;
};

// GUIInput - Used for keyboard input
class GUIInput : public GUIObject, public RenderObject, public ActionObject, public InputObject
{
public:
	// w and h may be ignored, in which case, no bounding box is applied
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

	virtual int NotifyKeyboard(int key);

protected:
	virtual int GetSelection(int x, int y);

	// Handles displaying the text properly when chars are added, deleted, or for scrolling
	virtual int HandleTextLocation(int x);

protected:
	GUIText* mInputText;
	GUIAction* mAction;
	Resource* mBackground;
	Resource* mCursor;
	Resource* mFont;
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
	HardwareKeyboard(void);
	virtual ~HardwareKeyboard();

public:
	virtual int KeyDown(int key_code);
	virtual int KeyUp(int key_code);
	virtual int KeyRepeat(void);

	void ConsumeKeyRelease(int key);

private:
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
	Resource *mFont;
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
	Resource *mBackgroundImage;
	Resource *mHandleImage;
	Resource *mHandleHoverImage;
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
	Resource *m_image;
	bool m_present;
};

// Helper APIs
bool LoadPlacement(xml_node<>* node, int* x, int* y, int* w = NULL, int* h = NULL, RenderObject::Placement* placement = NULL);

#endif  // _OBJECTS_HEADER

