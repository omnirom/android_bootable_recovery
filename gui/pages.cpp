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

// pages.cpp - Source to manage GUI base objects

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
#include "../twrp-functions.hpp"
#include "../partitions.hpp"

#include <string>

extern "C" {
#include "../twcommon.h"
#include "../minuitwrp/minui.h"
#include "../minzip/SysUtil.h"
#include "../minzip/Zip.h"
#include "gui.h"
}

#include "rapidxml.hpp"
#include "objects.hpp"
#include "blanktimer.hpp"

#define TW_THEME_VERSION 1
#define TW_THEME_VER_ERR -2

extern int gGuiRunning;

// From ../twrp.cpp
extern bool datamedia;

// From console.cpp
extern size_t last_message_count;
extern std::vector<std::string> gConsole;
extern std::vector<std::string> gConsoleColor;

std::map<std::string, PageSet*> PageManager::mPageSets;
PageSet* PageManager::mCurrentSet;
PageSet* PageManager::mBaseSet = NULL;
MouseCursor *PageManager::mMouseCursor = NULL;
HardwareKeyboard *PageManager::mHardwareKeyboard = NULL;
bool PageManager::mReloadTheme = false;
std::string PageManager::mStartPage = "main";
std::vector<language_struct> Language_List;

int tw_x_offset = 0;
int tw_y_offset = 0;

// Helper routine to convert a string to a color declaration
int ConvertStrToColor(std::string str, COLOR* color)
{
	// Set the default, solid black
	memset(color, 0, sizeof(COLOR));
	color->alpha = 255;

	// Translate variables
	DataManager::GetValue(str, str);

	// Look for some defaults
	if (str == "black")		return 0;
	else if (str == "white")	{ color->red = color->green = color->blue = 255; return 0; }
	else if (str == "red")		{ color->red = 255; return 0; }
	else if (str == "green")	{ color->green = 255; return 0; }
	else if (str == "blue")		{ color->blue = 255; return 0; }

	// At this point, we require an RGB(A) color
	if (str[0] != '#')
		return -1;

	str.erase(0, 1);

	int result;
	if (str.size() >= 8) {
		// We have alpha channel
		string alpha = str.substr(6, 2);
		result = strtol(alpha.c_str(), NULL, 16);
		color->alpha = result & 0x000000FF;
		str.resize(6);
		result = strtol(str.c_str(), NULL, 16);
		color->red = (result >> 16) & 0x000000FF;
		color->green = (result >> 8) & 0x000000FF;
		color->blue = result & 0x000000FF;
	} else {
		result = strtol(str.c_str(), NULL, 16);
		color->red = (result >> 16) & 0x000000FF;
		color->green = (result >> 8) & 0x000000FF;
		color->blue = result & 0x000000FF;
	}
	return 0;
}

// Helper APIs
xml_node<>* FindNode(xml_node<>* parent, const char* nodename, int depth /* = 0 */)
{
	if (!parent)
		return NULL;

	xml_node<>* child = parent->first_node(nodename);
	if (child)
		return child;

	if (depth == 10) {
		LOGERR("Too many style loops detected.\n");
		return NULL;
	}

	xml_node<>* style = parent->first_node("style");
	if (style) {
		while (style) {
			if (!style->first_attribute("name")) {
				LOGERR("No name given for style.\n");
				continue;
			} else {
				std::string name = style->first_attribute("name")->value();
				xml_node<>* node = PageManager::FindStyle(name);

				if (node) {
					// We found the style that was named
					xml_node<>* stylenode = FindNode(node, nodename, depth + 1);
					if (stylenode)
						return stylenode;
				}
			}
			style = style->next_sibling("style");
		}
	} else {
		// Search for stylename in the parent node <object type="foo" style="foo2">
		xml_attribute<>* attr = parent->first_attribute("style");
		// If no style is found anywhere else and the node wasn't found in the object itself
		// as a special case we will search for a style that uses the same style name as the
		// object type, so <object type="button"> would search for a style named "button"
		if (!attr)
			attr = parent->first_attribute("type");
		// if there's no attribute type, the object type must be the element name
		std::string stylename = attr ? attr->value() : parent->name();
		xml_node<>* node = PageManager::FindStyle(stylename);
		if (node) {
			xml_node<>* stylenode = FindNode(node, nodename, depth + 1);
			if (stylenode)
				return stylenode;
		}
	}
	return NULL;
}

std::string LoadAttrString(xml_node<>* element, const char* attrname, const char* defaultvalue)
{
	if (!element)
		return defaultvalue;

	xml_attribute<>* attr = element->first_attribute(attrname);
	return attr ? attr->value() : defaultvalue;
}

int LoadAttrInt(xml_node<>* element, const char* attrname, int defaultvalue)
{
	string value = LoadAttrString(element, attrname);
	// resolve variables
	DataManager::GetValue(value, value);
	return value.empty() ? defaultvalue : atoi(value.c_str());
}

int LoadAttrIntScaleX(xml_node<>* element, const char* attrname, int defaultvalue)
{
	return scale_theme_x(LoadAttrInt(element, attrname, defaultvalue));
}

int LoadAttrIntScaleY(xml_node<>* element, const char* attrname, int defaultvalue)
{
	return scale_theme_y(LoadAttrInt(element, attrname, defaultvalue));
}

COLOR LoadAttrColor(xml_node<>* element, const char* attrname, bool* found_color, COLOR defaultvalue)
{
	string value = LoadAttrString(element, attrname);
	*found_color = !value.empty();
	// resolve variables
	DataManager::GetValue(value, value);
	COLOR ret = defaultvalue;
	if (ConvertStrToColor(value, &ret) == 0)
		return ret;
	else
		return defaultvalue;
}

COLOR LoadAttrColor(xml_node<>* element, const char* attrname, COLOR defaultvalue)
{
	bool found_color = false;
	return LoadAttrColor(element, attrname, &found_color, defaultvalue);
}

FontResource* LoadAttrFont(xml_node<>* element, const char* attrname)
{
	std::string name = LoadAttrString(element, attrname, "");
	if (name.empty())
		return NULL;
	else
		return PageManager::GetResources()->FindFont(name);
}

ImageResource* LoadAttrImage(xml_node<>* element, const char* attrname)
{
	std::string name = LoadAttrString(element, attrname, "");
	if (name.empty())
		return NULL;
	else
		return PageManager::GetResources()->FindImage(name);
}

AnimationResource* LoadAttrAnimation(xml_node<>* element, const char* attrname)
{
	std::string name = LoadAttrString(element, attrname, "");
	if (name.empty())
		return NULL;
	else
		return PageManager::GetResources()->FindAnimation(name);
}

bool LoadPlacement(xml_node<>* node, int* x, int* y, int* w /* = NULL */, int* h /* = NULL */, Placement* placement /* = NULL */)
{
	if (!node)
		return false;

	if (node->first_attribute("x"))
		*x = LoadAttrIntScaleX(node, "x") + tw_x_offset;

	if (node->first_attribute("y"))
		*y = LoadAttrIntScaleY(node, "y") + tw_y_offset;

	if (w && node->first_attribute("w"))
		*w = LoadAttrIntScaleX(node, "w");

	if (h && node->first_attribute("h"))
		*h = LoadAttrIntScaleY(node, "h");

	if (placement && node->first_attribute("placement"))
		*placement = (Placement) LoadAttrInt(node, "placement");

	return true;
}

int ActionObject::SetActionPos(int x, int y, int w, int h)
{
	if (x < 0 || y < 0)
		return -1;

	mActionX = x;
	mActionY = y;
	if (w || h)
	{
		mActionW = w;
		mActionH = h;
	}
	return 0;
}

Page::Page(xml_node<>* page, std::vector<xml_node<>*> *templates)
{
	mTouchStart = NULL;

	// We can memset the whole structure, because the alpha channel is ignored
	memset(&mBackground, 0, sizeof(COLOR));

	// With NULL, we make a console-only display
	if (!page)
	{
		mName = "console";

		GUIConsole* element = new GUIConsole(NULL);
		mRenders.push_back(element);
		mActions.push_back(element);
		return;
	}

	if (page->first_attribute("name"))
		mName = page->first_attribute("name")->value();
	else
	{
		LOGERR("No page name attribute found!\n");
		return;
	}

	LOGINFO("Loading page %s\n", mName.c_str());

	// This is a recursive routine for template handling
	ProcessNode(page, templates, 0);
}

Page::~Page()
{
	for (std::vector<GUIObject*>::iterator itr = mObjects.begin(); itr != mObjects.end(); ++itr)
		delete *itr;
}

bool Page::ProcessNode(xml_node<>* page, std::vector<xml_node<>*> *templates, int depth)
{
	if (depth == 10)
	{
		LOGERR("Page processing depth has exceeded 10. Failing out. This is likely a recursive template.\n");
		return false;
	}

	for (xml_node<>* child = page->first_node(); child; child = child->next_sibling())
	{
		std::string type = child->name();

		if (type == "background") {
			mBackground = LoadAttrColor(child, "color", COLOR(0,0,0,0));
			continue;
		}

		if (type == "object") {
			// legacy format : <object type="...">
			xml_attribute<>* attr = child->first_attribute("type");
			type = attr ? attr->value() : "*unspecified*";
		}

		if (type == "text")
		{
			GUIText* element = new GUIText(child);
			mObjects.push_back(element);
			mRenders.push_back(element);
			mActions.push_back(element);
		}
		else if (type == "image")
		{
			GUIImage* element = new GUIImage(child);
			mObjects.push_back(element);
			mRenders.push_back(element);
		}
		else if (type == "fill")
		{
			GUIFill* element = new GUIFill(child);
			mObjects.push_back(element);
			mRenders.push_back(element);
		}
		else if (type == "action")
		{
			GUIAction* element = new GUIAction(child);
			mObjects.push_back(element);
			mActions.push_back(element);
		}
		else if (type == "console")
		{
			GUIConsole* element = new GUIConsole(child);
			mObjects.push_back(element);
			mRenders.push_back(element);
			mActions.push_back(element);
		}
		else if (type == "terminal")
		{
			GUITerminal* element = new GUITerminal(child);
			mObjects.push_back(element);
			mRenders.push_back(element);
			mActions.push_back(element);
			mInputs.push_back(element);
		}
		else if (type == "button")
		{
			GUIButton* element = new GUIButton(child);
			mObjects.push_back(element);
			mRenders.push_back(element);
			mActions.push_back(element);
		}
		else if (type == "checkbox")
		{
			GUICheckbox* element = new GUICheckbox(child);
			mObjects.push_back(element);
			mRenders.push_back(element);
			mActions.push_back(element);
		}
		else if (type == "fileselector")
		{
			GUIFileSelector* element = new GUIFileSelector(child);
			mObjects.push_back(element);
			mRenders.push_back(element);
			mActions.push_back(element);
		}
		else if (type == "animation")
		{
			GUIAnimation* element = new GUIAnimation(child);
			mObjects.push_back(element);
			mRenders.push_back(element);
		}
		else if (type == "progressbar")
		{
			GUIProgressBar* element = new GUIProgressBar(child);
			mObjects.push_back(element);
			mRenders.push_back(element);
			mActions.push_back(element);
		}
		else if (type == "slider")
		{
			GUISlider* element = new GUISlider(child);
			mObjects.push_back(element);
			mRenders.push_back(element);
			mActions.push_back(element);
		}
		else if (type == "slidervalue")
		{
			GUISliderValue *element = new GUISliderValue(child);
			mObjects.push_back(element);
			mRenders.push_back(element);
			mActions.push_back(element);
		}
		else if (type == "listbox")
		{
			GUIListBox* element = new GUIListBox(child);
			mObjects.push_back(element);
			mRenders.push_back(element);
			mActions.push_back(element);
		}
		else if (type == "keyboard")
		{
			GUIKeyboard* element = new GUIKeyboard(child);
			mObjects.push_back(element);
			mRenders.push_back(element);
			mActions.push_back(element);
		}
		else if (type == "input")
		{
			GUIInput* element = new GUIInput(child);
			mObjects.push_back(element);
			mRenders.push_back(element);
			mActions.push_back(element);
			mInputs.push_back(element);
		}
		else if (type == "partitionlist")
		{
			GUIPartitionList* element = new GUIPartitionList(child);
			mObjects.push_back(element);
			mRenders.push_back(element);
			mActions.push_back(element);
		}
		else if (type == "patternpassword")
		{
			GUIPatternPassword* element = new GUIPatternPassword(child);
			mObjects.push_back(element);
			mRenders.push_back(element);
			mActions.push_back(element);
		}
		else if (type == "textbox")
		{
			GUITextBox* element = new GUITextBox(child);
			mObjects.push_back(element);
			mRenders.push_back(element);
			mActions.push_back(element);
		}
		else if (type == "template")
		{
			if (!templates || !child->first_attribute("name"))
			{
				LOGERR("Invalid template request.\n");
			}
			else
			{
				std::string name = child->first_attribute("name")->value();
				xml_node<>* node;
				bool node_found = false;

				// We need to find the correct template
				for (std::vector<xml_node<>*>::iterator itr = templates->begin(); itr != templates->end(); itr++) {
					node = (*itr)->first_node("template");

					while (node)
					{
						if (!node->first_attribute("name"))
							continue;

						if (name == node->first_attribute("name")->value())
						{
							if (!ProcessNode(node, templates, depth + 1))
								return false;
							else {
								node_found = true;
								break;
							}
						}
						if (node_found)
							break;
						node = node->next_sibling("template");
					}
					// [check] why is there no if (node_found) here too?
				}
			}
		}
		else
		{
			LOGERR("Unknown object type: %s.\n", type.c_str());
		}
	}
	return true;
}

int Page::Render(void)
{
	// Render background
	gr_color(mBackground.red, mBackground.green, mBackground.blue, mBackground.alpha);
	gr_fill(0, 0, gr_fb_width(), gr_fb_height());

	// Render remaining objects
	std::vector<RenderObject*>::iterator iter;
	for (iter = mRenders.begin(); iter != mRenders.end(); iter++)
	{
		if ((*iter)->Render())
			LOGERR("A render request has failed.\n");
	}
	return 0;
}

int Page::Update(void)
{
	int retCode = 0;

	std::vector<RenderObject*>::iterator iter;
	for (iter = mRenders.begin(); iter != mRenders.end(); iter++)
	{
		int ret = (*iter)->Update();
		if (ret < 0)
			LOGERR("An update request has failed.\n");
		else if (ret > retCode)
			retCode = ret;
	}

	return retCode;
}

int Page::NotifyTouch(TOUCH_STATE state, int x, int y)
{
	// By default, return 1 to ignore further touches if nobody is listening
	int ret = 1;

	// Don't try to handle a lack of handlers
	if (mActions.size() == 0)
		return ret;

	// We record mTouchStart so we can pass all the touch stream to the same handler
	if (state == TOUCH_START)
	{
		std::vector<ActionObject*>::reverse_iterator iter;
		// We work backwards, from top-most element to bottom-most element
		for (iter = mActions.rbegin(); iter != mActions.rend(); iter++)
		{
			if ((*iter)->IsInRegion(x, y))
			{
				mTouchStart = (*iter);
				ret = mTouchStart->NotifyTouch(state, x, y);
				if (ret >= 0)
					break;
				mTouchStart = NULL;
			}
		}
	}
	else if (state == TOUCH_RELEASE && mTouchStart != NULL)
	{
		ret = mTouchStart->NotifyTouch(state, x, y);
		mTouchStart = NULL;
	}
	else if ((state == TOUCH_DRAG || state == TOUCH_HOLD || state == TOUCH_REPEAT) && mTouchStart != NULL)
	{
		ret = mTouchStart->NotifyTouch(state, x, y);
	}
	return ret;
}

int Page::NotifyKey(int key, bool down)
{
	std::vector<ActionObject*>::reverse_iterator iter;

	int ret = 1;
	// We work backwards, from top-most element to bottom-most element
	for (iter = mActions.rbegin(); iter != mActions.rend(); iter++)
	{
		ret = (*iter)->NotifyKey(key, down);
		if (ret == 0)
			return 0;
		if (ret < 0) {
			LOGERR("An action handler has returned an error\n");
			ret = 1;
		}
	}
	return ret;
}

int Page::NotifyCharInput(int ch)
{
	std::vector<InputObject*>::reverse_iterator iter;

	// We work backwards, from top-most element to bottom-most element
	for (iter = mInputs.rbegin(); iter != mInputs.rend(); iter++)
	{
		int ret = (*iter)->NotifyCharInput(ch);
		if (ret == 0)
			return 0;
		else if (ret < 0)
			LOGERR("A char input handler has returned an error");
	}
	return 1;
}

int Page::SetKeyBoardFocus(int inFocus)
{
	std::vector<InputObject*>::reverse_iterator iter;

	// We work backwards, from top-most element to bottom-most element
	for (iter = mInputs.rbegin(); iter != mInputs.rend(); iter++)
	{
		int ret = (*iter)->SetInputFocus(inFocus);
		if (ret == 0)
			return 0;
		else if (ret < 0)
			LOGERR("An input focus handler has returned an error");
	}
	return 1;
}

void Page::SetPageFocus(int inFocus)
{
	// Render remaining objects
	std::vector<RenderObject*>::iterator iter;
	for (iter = mRenders.begin(); iter != mRenders.end(); iter++)
		(*iter)->SetPageFocus(inFocus);

	return;
}

int Page::NotifyVarChange(std::string varName, std::string value)
{
	std::vector<GUIObject*>::iterator iter;
	for (iter = mObjects.begin(); iter != mObjects.end(); ++iter)
	{
		if ((*iter)->NotifyVarChange(varName, value))
			LOGERR("An action handler errored on NotifyVarChange.\n");
	}
	return 0;
}

PageSet::PageSet(const char* xmlFile)
{
	mResources = new ResourceManager;
	mCurrentPage = NULL;

	if (!xmlFile)
		mCurrentPage = new Page(NULL, NULL);
}

PageSet::~PageSet()
{
	mOverlays.clear();
	for (std::vector<Page*>::iterator itr = mPages.begin(); itr != mPages.end(); ++itr)
		delete *itr;

	delete mResources;
}

int PageSet::LoadLanguage(char* languageFile, ZipArchive* package)
{
	xml_document<> lang;
	xml_node<>* parent;
	xml_node<>* child;
	std::string resource_source;
	int ret = 0;

	if (languageFile) {
		printf("parsing languageFile\n");
		lang.parse<0>(languageFile);
		printf("parsing languageFile done\n");
	} else {
		return -1;
	}

	parent = lang.first_node("language");
	if (!parent) {
		LOGERR("Unable to locate language node in language file.\n");
		lang.clear();
		return -1;
	}

	child = parent->first_node("display");
	if (child) {
		DataManager::SetValue("tw_language_display", child->value());
		resource_source = child->value();
	} else {
		LOGERR("language file does not have a display value set\n");
		DataManager::SetValue("tw_language_display", "Not Set");
		resource_source = languageFile;
	}

	child = parent->first_node("resources");
	if (child)
		mResources->LoadResources(child, package, resource_source);
	else
		ret = -1;
	DataManager::SetValue("tw_backup_name", gui_lookup("auto_generate", "(Auto Generate)"));
	lang.clear();
	return ret;
}

int PageSet::Load(ZipArchive* package, char* xmlFile, char* languageFile, char* baseLanguageFile)
{
	xml_document<> mDoc;
	xml_node<>* parent;
	xml_node<>* child;
	xml_node<>* xmltemplate;
	xml_node<>* xmlstyle;

	mDoc.parse<0>(xmlFile);
	parent = mDoc.first_node("recovery");
	if (!parent)
		parent = mDoc.first_node("install");

	set_scale_values(1, 1); // Reset any previous scaling values

	if (baseLanguageFile)
		LoadLanguage(baseLanguageFile, NULL);

	// Now, let's parse the XML
	child = parent->first_node("details");
	if (child) {
		int theme_ver = 0;
		xml_node<>* themeversion = child->first_node("themeversion");
		if (themeversion && themeversion->value()) {
			theme_ver = atoi(themeversion->value());
		} else {
			LOGINFO("No themeversion in theme.\n");
		}
		if (theme_ver != TW_THEME_VERSION) {
			LOGINFO("theme version from xml: %i, expected %i\n", theme_ver, TW_THEME_VERSION);
			if (package) {
				gui_err("theme_ver_err=Custom theme version does not match TWRP version. Using stock theme.");
				mDoc.clear();
				return TW_THEME_VER_ERR;
			} else {
				gui_print_color("warning", "Stock theme version does not match TWRP version.\n");
			}
		}
		xml_node<>* resolution = child->first_node("resolution");
		if (resolution) {
			LOGINFO("Checking resolution...\n");
			xml_attribute<>* width_attr = resolution->first_attribute("width");
			xml_attribute<>* height_attr = resolution->first_attribute("height");
			xml_attribute<>* noscale_attr = resolution->first_attribute("noscaling");
			if (width_attr && height_attr && !noscale_attr) {
				int width = atoi(width_attr->value());
				int height = atoi(height_attr->value());
				int offx = 0, offy = 0;
#ifdef TW_ROUND_SCREEN
				xml_node<>* roundscreen = child->first_node("roundscreen");
				if (roundscreen) {
					LOGINFO("TW_ROUND_SCREEN := true, using round screen XML settings.\n");
					xml_attribute<>* offx_attr = roundscreen->first_attribute("offset_x");
					xml_attribute<>* offy_attr = roundscreen->first_attribute("offset_y");
					if (offx_attr) {
						offx = atoi(offx_attr->value());
					}
					if (offy_attr) {
						offy = atoi(offy_attr->value());
					}
				}
#endif
				if (width != 0 && height != 0) {
					float scale_w = ((float)gr_fb_width() - ((float)offx * 2.0)) / (float)width;
					float scale_h = ((float)gr_fb_height() - ((float)offy * 2.0)) / (float)height;
#ifdef TW_ROUND_SCREEN
					float scale_off_w = (float)gr_fb_width() / (float)width;
					float scale_off_h = (float)gr_fb_height() / (float)height;
					tw_x_offset = offx * scale_off_w;
					tw_y_offset = offy * scale_off_h;
#endif
					if (scale_w != 1 || scale_h != 1) {
						LOGINFO("Scaling theme width %fx and height %fx, offsets x: %i y: %i\n", scale_w, scale_h, tw_x_offset, tw_y_offset);
						set_scale_values(scale_w, scale_h);
					}
				}
			} else {
				LOGINFO("XML does not contain width and height, no scaling will be applied\n");
			}
		} else {
			LOGINFO("XML contains no resolution tag, no scaling will be applied.\n");
		}
	} else {
		LOGINFO("XML contains no details tag, no scaling will be applied.\n");
	}

	if (languageFile)
		LoadLanguage(languageFile, package);

	LOGINFO("Loading resources...\n");
	child = parent->first_node("resources");
	if (child)
		mResources->LoadResources(child, package, "theme");

	LOGINFO("Loading variables...\n");
	child = parent->first_node("variables");
	if (child)
		LoadVariables(child);

	LOGINFO("Loading mouse cursor...\n");
	child = parent->first_node("mousecursor");
	if(child)
		PageManager::LoadCursorData(child);

	LOGINFO("Loading pages...\n");
	// This may be NULL if no templates are present
	xmltemplate = parent->first_node("templates");
	if (xmltemplate)
		templates.push_back(xmltemplate);

	// Load styles if present
	xmlstyle = parent->first_node("styles");
	if (xmlstyle)
		styles.push_back(xmlstyle);

	child = parent->first_node("pages");
	if (child) {
		if (LoadPages(child)) {
			LOGERR("PageSet::Load returning -1\n");
			mDoc.clear();
			return -1;
		}
	}

	int ret = CheckInclude(package, &mDoc);
	mDoc.clear();
	templates.clear();
	return ret;
}

int PageSet::CheckInclude(ZipArchive* package, xml_document<> *parentDoc)
{
	xml_node<>* par;
	xml_node<>* par2;
	xml_node<>* chld;
	xml_node<>* parent;
	xml_node<>* child;
	xml_node<>* xmltemplate;
	xml_node<>* xmlstyle;
	long len;
	char* xmlFile = NULL;
	string filename;
	xml_document<> *doc = NULL;

	par = parentDoc->first_node("recovery");
	if (!par) {
		par = parentDoc->first_node("install");
	}
	if (!par) {
		return 0;
	}

	par2 = par->first_node("include");
	if (!par2)
		return 0;
	chld = par2->first_node("xmlfile");
	while (chld != NULL) {
		xml_attribute<>* attr = chld->first_attribute("name");
		if (!attr)
			break;

		if (!package) {
			// We can try to load the XML directly...
			filename = TWRES;
			filename += attr->value();
		} else {
			filename += attr->value();
		}
		xmlFile = PageManager::LoadFileToBuffer(filename, package);
		if (xmlFile == NULL) {
			LOGERR("PageSet::CheckInclude unable to load '%s'\n", filename.c_str());
			return -1;
		}

		doc = new xml_document<>();
		doc->parse<0>(xmlFile);

		parent = doc->first_node("recovery");
		if (!parent)
			parent = doc->first_node("install");

		// Now, let's parse the XML
		LOGINFO("Loading included resources...\n");
		child = parent->first_node("resources");
		if (child)
			mResources->LoadResources(child, package, "theme");

		LOGINFO("Loading included variables...\n");
		child = parent->first_node("variables");
		if (child)
			LoadVariables(child);

		LOGINFO("Loading mouse cursor...\n");
		child = parent->first_node("mousecursor");
		if(child)
			PageManager::LoadCursorData(child);

		LOGINFO("Loading included pages...\n");
		// This may be NULL if no templates are present
		xmltemplate = parent->first_node("templates");
		if (xmltemplate)
			templates.push_back(xmltemplate);

		// Load styles if present
		xmlstyle = parent->first_node("styles");
		if (xmlstyle)
			styles.push_back(xmlstyle);

		child = parent->first_node("pages");
		if (child && LoadPages(child))
		{
			templates.pop_back();
			doc->clear();
			delete doc;
			free(xmlFile);
			return -1;
		}

		if (CheckInclude(package, doc)) {
			doc->clear();
			delete doc;
			free(xmlFile);
			return -1;
		}
		doc->clear();
		delete doc;
		free(xmlFile);

		chld = chld->next_sibling("xmlfile");
	}

	return 0;
}

int PageSet::SetPage(std::string page)
{
	Page* tmp = FindPage(page);
	if (tmp)
	{
		if (mCurrentPage)   mCurrentPage->SetPageFocus(0);
		mCurrentPage = tmp;
		mCurrentPage->SetPageFocus(1);
		mCurrentPage->NotifyVarChange("", "");
		return 0;
	}
	else
	{
		LOGERR("Unable to locate page (%s)\n", page.c_str());
	}
	return -1;
}

int PageSet::SetOverlay(Page* page)
{
	if (page) {
		if (mOverlays.size() >= 10) {
			LOGERR("Too many overlays requested, max is 10.\n");
			return -1;
		}

		std::vector<Page*>::iterator iter;
		for (iter = mOverlays.begin(); iter != mOverlays.end(); iter++) {
			if ((*iter)->GetName() == page->GetName()) {
				mOverlays.erase(iter);
				// SetOverlay() is (and should stay) the only function which
				// adds to mOverlays. Then, each page can appear at most once.
				break;
			}
		}

		page->SetPageFocus(1);
		page->NotifyVarChange("", "");

		if (!mOverlays.empty())
			mOverlays.back()->SetPageFocus(0);

		mOverlays.push_back(page);
	} else {
		if (!mOverlays.empty()) {
			mOverlays.back()->SetPageFocus(0);
			mOverlays.pop_back();
			if (!mOverlays.empty())
				mOverlays.back()->SetPageFocus(1);
			else if (mCurrentPage)
				mCurrentPage->SetPageFocus(1); // Just in case somehow the regular page lost focus, we'll set it again
		}
	}
	return 0;
}

const ResourceManager* PageSet::GetResources()
{
	return mResources;
}

Page* PageSet::FindPage(std::string name)
{
	std::vector<Page*>::iterator iter;

	for (iter = mPages.begin(); iter != mPages.end(); iter++)
	{
		if (name == (*iter)->GetName())
			return (*iter);
	}
	return NULL;
}

int PageSet::LoadVariables(xml_node<>* vars)
{
	xml_node<>* child;
	xml_attribute<> *name, *value, *persist;
	int p;

	child = vars->first_node("variable");
	while (child)
	{
		name = child->first_attribute("name");
		value = child->first_attribute("value");
		persist = child->first_attribute("persist");
		if(name && value)
		{
			if (strcmp(name->value(), "tw_x_offset") == 0) {
				tw_x_offset = atoi(value->value());
				child = child->next_sibling("variable");
				continue;
			}
			if (strcmp(name->value(), "tw_y_offset") == 0) {
				tw_y_offset = atoi(value->value());
				child = child->next_sibling("variable");
				continue;
			}
			p = persist ? atoi(persist->value()) : 0;
			string temp = value->value();
			string valstr = gui_parse_text(temp);

			if (valstr.find("+") != string::npos) {
				string val1str = valstr;
				val1str = val1str.substr(0, val1str.find('+'));
				string val2str = valstr;
				val2str = val2str.substr(val2str.find('+') + 1, string::npos);
				int val1 = atoi(val1str.c_str());
				int val2 = atoi(val2str.c_str());
				int val = val1 + val2;

				DataManager::SetValue(name->value(), val, p);
			} else if (valstr.find("-") != string::npos) {
				string val1str = valstr;
				val1str = val1str.substr(0, val1str.find('-'));
				string val2str = valstr;
				val2str = val2str.substr(val2str.find('-') + 1, string::npos);
				int val1 = atoi(val1str.c_str());
				int val2 = atoi(val2str.c_str());
				int val = val1 - val2;

				DataManager::SetValue(name->value(), val, p);
			} else {
				DataManager::SetValue(name->value(), valstr, p);
			}
		}

		child = child->next_sibling("variable");
	}
	return 0;
}

int PageSet::LoadPages(xml_node<>* pages)
{
	xml_node<>* child;

	if (!pages)
		return -1;

	child = pages->first_node("page");
	while (child != NULL)
	{
		Page* page = new Page(child, &templates);
		if (page->GetName().empty())
		{
			LOGERR("Unable to process load page\n");
			delete page;
		}
		else
		{
			mPages.push_back(page);
		}
		child = child->next_sibling("page");
	}
	if (mPages.size() > 0)
		return 0;
	return -1;
}

int PageSet::IsCurrentPage(Page* page)
{
	return ((mCurrentPage && mCurrentPage == page) ? 1 : 0);
}

std::string PageSet::GetCurrentPage() const
{
	return mCurrentPage ? mCurrentPage->GetName() : "";
}

int PageSet::Render(void)
{
	int ret;

	ret = (mCurrentPage ? mCurrentPage->Render() : -1);
	if (ret < 0)
		return ret;

	std::vector<Page*>::iterator iter;

	for (iter = mOverlays.begin(); iter != mOverlays.end(); iter++) {
		ret = ((*iter) ? (*iter)->Render() : -1);
		if (ret < 0)
			return ret;
	}
	return ret;
}

int PageSet::Update(void)
{
	int ret;

	ret = (mCurrentPage ? mCurrentPage->Update() : -1);
	if (ret < 0 || ret > 1)
		return ret;

	std::vector<Page*>::iterator iter;

	for (iter = mOverlays.begin(); iter != mOverlays.end(); iter++) {
		ret = ((*iter) ? (*iter)->Update() : -1);
		if (ret < 0)
			return ret;
	}
	return ret;
}

int PageSet::NotifyTouch(TOUCH_STATE state, int x, int y)
{
	if (!mOverlays.empty())
		return mOverlays.back()->NotifyTouch(state, x, y);

	return (mCurrentPage ? mCurrentPage->NotifyTouch(state, x, y) : -1);
}

int PageSet::NotifyKey(int key, bool down)
{
	if (!mOverlays.empty())
		return mOverlays.back()->NotifyKey(key, down);

	return (mCurrentPage ? mCurrentPage->NotifyKey(key, down) : -1);
}

int PageSet::NotifyCharInput(int ch)
{
	if (!mOverlays.empty())
		return mOverlays.back()->NotifyCharInput(ch);

	return (mCurrentPage ? mCurrentPage->NotifyCharInput(ch) : -1);
}

int PageSet::SetKeyBoardFocus(int inFocus)
{
	if (!mOverlays.empty())
		return mOverlays.back()->SetKeyBoardFocus(inFocus);

	return (mCurrentPage ? mCurrentPage->SetKeyBoardFocus(inFocus) : -1);
}

int PageSet::NotifyVarChange(std::string varName, std::string value)
{
	std::vector<Page*>::iterator iter;

	for (iter = mOverlays.begin(); iter != mOverlays.end(); iter++)
		(*iter)->NotifyVarChange(varName, value);

	return (mCurrentPage ? mCurrentPage->NotifyVarChange(varName, value) : -1);
}

void PageSet::AddStringResource(std::string resource_source, std::string resource_name, std::string value)
{
	mResources->AddStringResource(resource_source, resource_name, value);
}

char* PageManager::LoadFileToBuffer(std::string filename, ZipArchive* package) {
	size_t len;
	char* buffer = NULL;

	if (!package) {
		// We can try to load the XML directly...
		LOGINFO("PageManager::LoadFileToBuffer loading filename: '%s' directly\n", filename.c_str());
		struct stat st;
		if(stat(filename.c_str(),&st) != 0) {
			// This isn't always an error, sometimes we request files that don't exist.
			return NULL;
		}

		len = (size_t)st.st_size;

		buffer = (char*) malloc(len + 1);
		if (!buffer) {
			LOGERR("PageManager::LoadFileToBuffer failed to malloc\n");
			return NULL;
		}

		int fd = open(filename.c_str(), O_RDONLY);
		if (fd == -1) {
			LOGERR("PageManager::LoadFileToBuffer failed to open '%s' - (%s)\n", filename.c_str(), strerror(errno));
			free(buffer);
			return NULL;
		}

		if (read(fd, buffer, len) < 0) {
			LOGERR("PageManager::LoadFileToBuffer failed to read '%s' - (%s)\n", filename.c_str(), strerror(errno));
			free(buffer);
			close(fd);
			return NULL;
		}
		close(fd);
	} else {
		LOGINFO("PageManager::LoadFileToBuffer loading filename: '%s' from zip\n", filename.c_str());
		const ZipEntry* zipentry = mzFindZipEntry(package, filename.c_str());
		if (zipentry == NULL) {
			LOGERR("Unable to locate '%s' in zip file\n", filename.c_str());
			return NULL;
		}

		// Allocate the buffer for the file
		len = mzGetZipEntryUncompLen(zipentry);
		buffer = (char*) malloc(len + 1);
		if (!buffer)
			return NULL;

		if (!mzExtractZipEntryToBuffer(package, zipentry, (unsigned char*) buffer)) {
			LOGERR("Unable to extract '%s'\n", filename.c_str());
			free(buffer);
			return NULL;
		}
	}
	// NULL-terminate the string
	buffer[len] = 0x00;
	return buffer;
}

void PageManager::LoadLanguageListDir(string dir) {
	if (!TWFunc::Path_Exists(dir)) {
		LOGERR("LoadLanguageListDir '%s' path not found\n", dir.c_str());
		return;
	}

	DIR *d = opendir(dir.c_str());
	struct dirent *p;

	if (d == NULL) {
		LOGERR("LoadLanguageListDir error opening dir: '%s', %s\n", dir.c_str(), strerror(errno));
		return;
	}

	while ((p = readdir(d))) {
		if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, "..") || strlen(p->d_name) < 5)
			continue;

		string file = p->d_name;
		if (file.substr(strlen(p->d_name) - 4) != ".xml")
			continue;
		string path = dir + p->d_name;
		string file_no_extn = file.substr(0, strlen(p->d_name) - 4);
		struct language_struct language_entry;
		language_entry.filename = file_no_extn;
		char* xmlFile = PageManager::LoadFileToBuffer(dir + p->d_name, NULL);
		if (xmlFile == NULL) {
			LOGERR("LoadLanguageListDir unable to load '%s'\n", language_entry.filename.c_str());
			continue;
		}
		xml_document<> *doc = new xml_document<>();
		doc->parse<0>(xmlFile);

		xml_node<>* parent = doc->first_node("language");
		if (!parent) {
			LOGERR("Invalid language XML file '%s'\n", language_entry.filename.c_str());
		} else {
			xml_node<>* child = parent->first_node("display");
			if (child) {
				language_entry.displayvalue = child->value();
			} else {
				LOGERR("No display value for '%s'\n", language_entry.filename.c_str());
				language_entry.displayvalue = language_entry.filename;
			}
			Language_List.push_back(language_entry);
		}
		doc->clear();
		delete doc;
		free(xmlFile);
	}
	closedir(d);
}

void PageManager::LoadLanguageList(ZipArchive* package) {
	Language_List.clear();
	if (TWFunc::Path_Exists(TWRES "customlanguages"))
		TWFunc::removeDir(TWRES "customlanguages", true);
	if (package) {
		TWFunc::Recursive_Mkdir(TWRES "customlanguages");
		struct utimbuf timestamp = { 1217592000, 1217592000 };  // 8/1/2008 default
		mzExtractRecursive(package, "languages", TWRES "customlanguages/", &timestamp, NULL, NULL, NULL);
		LoadLanguageListDir(TWRES "customlanguages/");
	} else {
		LoadLanguageListDir(TWRES "languages/");
	}
}

void PageManager::LoadLanguage(string filename) {
	string actual_filename;
	if (TWFunc::Path_Exists(TWRES "customlanguages/" + filename + ".xml"))
		actual_filename = TWRES "customlanguages/" + filename + ".xml";
	else
		actual_filename = TWRES "languages/" + filename + ".xml";
	char* xmlFile = PageManager::LoadFileToBuffer(actual_filename, NULL);
	if (xmlFile == NULL)
		LOGERR("Unable to load '%s'\n", actual_filename.c_str());
	else {
		mCurrentSet->LoadLanguage(xmlFile, NULL);
		free(xmlFile);
	}
	PartitionManager.Translate_Partition_Display_Names();
}

int PageManager::LoadPackage(std::string name, std::string package, std::string startpage)
{
	int fd;
	ZipArchive zip, *pZip = NULL;
	long len;
	char* xmlFile = NULL;
	char* languageFile = NULL;
	char* baseLanguageFile = NULL;
	PageSet* pageSet = NULL;
	int ret;
	MemMapping map;

	mReloadTheme = false;
	mStartPage = startpage;

	// Open the XML file
	LOGINFO("Loading package: %s (%s)\n", name.c_str(), package.c_str());
	if (package.size() > 4 && package.substr(package.size() - 4) != ".zip")
	{
		LOGINFO("Load XML directly\n");
		tw_x_offset = TW_X_OFFSET;
		tw_y_offset = TW_Y_OFFSET;
		LoadLanguageList(NULL);
		languageFile = LoadFileToBuffer(TWRES "languages/en.xml", NULL);
	}
	else
	{
		LOGINFO("Loading zip theme\n");
		tw_x_offset = 0;
		tw_y_offset = 0;
		if (!TWFunc::Path_Exists(package))
			return -1;
		if (sysMapFile(package.c_str(), &map) != 0) {
			LOGERR("Failed to map '%s'\n", package.c_str());
			goto error;
		}
		if (mzOpenZipArchive(map.addr, map.length, &zip)) {
			LOGERR("Unable to open zip archive '%s'\n", package.c_str());
			sysReleaseMap(&map);
			goto error;
		}
		pZip = &zip;
		package = "ui.xml";
		LoadLanguageList(pZip);
		languageFile = LoadFileToBuffer("languages/en.xml", pZip);
		baseLanguageFile = LoadFileToBuffer(TWRES "languages/en.xml", NULL);
	}

	xmlFile = LoadFileToBuffer(package, pZip);
	if (xmlFile == NULL) {
		goto error;
	}

	// Before loading, mCurrentSet must be the loading package so we can find resources
	pageSet = mCurrentSet;
	mCurrentSet = new PageSet(xmlFile);
	ret = mCurrentSet->Load(pZip, xmlFile, languageFile, baseLanguageFile);
	if (languageFile) {
		free(languageFile);
		languageFile = NULL;
	}
	if (ret == 0) {
		mCurrentSet->SetPage(startpage);
		mPageSets.insert(std::pair<std::string, PageSet*>(name, mCurrentSet));
	} else {
		if (ret != TW_THEME_VER_ERR)
			LOGERR("Package %s failed to load.\n", name.c_str());
	}

	// The first successful package we loaded is the base
	if (mBaseSet == NULL)
		mBaseSet = mCurrentSet;

	mCurrentSet = pageSet;

	if (pZip) {
		mzCloseZipArchive(pZip);
		sysReleaseMap(&map);
	}
	free(xmlFile);
	if (languageFile)
		free(languageFile);
	return ret;

error:
	// Sometimes we get here without a real error
	if (pZip) {
		mzCloseZipArchive(pZip);
		sysReleaseMap(&map);
	}
	if (xmlFile)
		free(xmlFile);
	return -1;
}

PageSet* PageManager::FindPackage(std::string name)
{
	std::map<std::string, PageSet*>::iterator iter;

	iter = mPageSets.find(name);
	if (iter != mPageSets.end())
		return (*iter).second;

	LOGERR("Unable to locate package %s\n", name.c_str());
	return NULL;
}

PageSet* PageManager::SelectPackage(std::string name)
{
	LOGINFO("Switching packages (%s)\n", name.c_str());
	PageSet* tmp;

	tmp = FindPackage(name);
	if (tmp)
	{
		mCurrentSet = tmp;
		mCurrentSet->NotifyVarChange("", "");
	}
	else
		LOGERR("Unable to find package.\n");

	return mCurrentSet;
}

int PageManager::ReloadPackage(std::string name, std::string package)
{
	std::map<std::string, PageSet*>::iterator iter;

	mReloadTheme = false;

	iter = mPageSets.find(name);
	if (iter == mPageSets.end())
		return -1;

	if(mMouseCursor)
		mMouseCursor->ResetData(gr_fb_width(), gr_fb_height());

	PageSet* set = (*iter).second;
	mPageSets.erase(iter);

	if (LoadPackage(name, package, mStartPage) != 0)
	{
		LOGINFO("Failed to load package '%s'.\n", package.c_str());
		mPageSets.insert(std::pair<std::string, PageSet*>(name, set));
		return -1;
	}
	if (mCurrentSet == set)
		SelectPackage(name);
	if (mBaseSet == set)
		mBaseSet = mCurrentSet;
	delete set;
	GUIConsole::Translate_Now();
	return 0;
}

void PageManager::ReleasePackage(std::string name)
{
	std::map<std::string, PageSet*>::iterator iter;

	iter = mPageSets.find(name);
	if (iter == mPageSets.end())
		return;

	PageSet* set = (*iter).second;
	mPageSets.erase(iter);
	delete set;
	return;
}

int PageManager::RunReload() {
	int ret_val = 0;
	std::string theme_path;

	if (!mReloadTheme)
		return 0;

	mReloadTheme = false;
	theme_path = DataManager::GetSettingsStoragePath();
	if (PartitionManager.Mount_By_Path(theme_path.c_str(), 1) < 0) {
		LOGERR("Unable to mount %s during gui_reload_theme function.\n", theme_path.c_str());
		ret_val = 1;
	}

	theme_path += "/TWRP/theme/ui.zip";
	if (ret_val != 0 || ReloadPackage("TWRP", theme_path) != 0)
	{
		// Loading the custom theme failed - try loading the stock theme
		LOGINFO("Attempting to reload stock theme...\n");
		if (ReloadPackage("TWRP", TWRES "ui.xml"))
		{
			LOGERR("Failed to load base packages.\n");
			ret_val = 1;
		}
	}
	if (ret_val == 0) {
		if (DataManager::GetStrValue("tw_language") != "en.xml") {
			LOGINFO("Loading language '%s'\n", DataManager::GetStrValue("tw_language").c_str());
			LoadLanguage(DataManager::GetStrValue("tw_language"));
		}
	}

	// This makes the console re-translate
	last_message_count = 0;
	gConsole.clear();
	gConsoleColor.clear();

	return ret_val;
}

void PageManager::RequestReload() {
	mReloadTheme = true;
}

int PageManager::ChangePage(std::string name)
{
	DataManager::SetValue("tw_operation_state", 0);
	int ret = (mCurrentSet ? mCurrentSet->SetPage(name) : -1);
	return ret;
}

std::string PageManager::GetCurrentPage()
{
	return mCurrentSet ? mCurrentSet->GetCurrentPage() : "";
}

int PageManager::ChangeOverlay(std::string name)
{
	if (name.empty())
		return mCurrentSet->SetOverlay(NULL);
	else
	{
		Page* page = mCurrentSet ? mCurrentSet->FindPage(name) : NULL;
		return mCurrentSet->SetOverlay(page);
	}
}

const ResourceManager* PageManager::GetResources()
{
	return (mCurrentSet ? mCurrentSet->GetResources() : NULL);
}

int PageManager::IsCurrentPage(Page* page)
{
	return (mCurrentSet ? mCurrentSet->IsCurrentPage(page) : 0);
}

int PageManager::Render(void)
{
	if(blankTimer.isScreenOff())
		return 0;

	int res = (mCurrentSet ? mCurrentSet->Render() : -1);
	if(mMouseCursor)
		mMouseCursor->Render();
	return res;
}

HardwareKeyboard *PageManager::GetHardwareKeyboard()
{
	if(!mHardwareKeyboard)
		mHardwareKeyboard = new HardwareKeyboard();
	return mHardwareKeyboard;
}

xml_node<>* PageManager::FindStyle(std::string name)
{
	for (std::vector<xml_node<>*>::iterator itr = mCurrentSet->styles.begin(); itr != mCurrentSet->styles.end(); itr++) {
		xml_node<>* node = (*itr)->first_node("style");

		while (node) {
			if (!node->first_attribute("name"))
				continue;

			if (name == node->first_attribute("name")->value())
				return node;
			node = node->next_sibling("style");
		}
	}
	return NULL;
}

MouseCursor *PageManager::GetMouseCursor()
{
	if(!mMouseCursor)
		mMouseCursor = new MouseCursor(gr_fb_width(), gr_fb_height());
	return mMouseCursor;
}

void PageManager::LoadCursorData(xml_node<>* node)
{
	if(!mMouseCursor)
		mMouseCursor = new MouseCursor(gr_fb_width(), gr_fb_height());

	mMouseCursor->LoadData(node);
}

int PageManager::Update(void)
{
	if(blankTimer.isScreenOff())
		return 0;

	if (RunReload())
		return -2;

	int res = (mCurrentSet ? mCurrentSet->Update() : -1);

	if(mMouseCursor)
	{
		int c_res = mMouseCursor->Update();
		if(c_res > res)
			res = c_res;
	}
	return res;
}

int PageManager::NotifyTouch(TOUCH_STATE state, int x, int y)
{
	return (mCurrentSet ? mCurrentSet->NotifyTouch(state, x, y) : -1);
}

int PageManager::NotifyKey(int key, bool down)
{
	return (mCurrentSet ? mCurrentSet->NotifyKey(key, down) : -1);
}

int PageManager::NotifyCharInput(int ch)
{
	return (mCurrentSet ? mCurrentSet->NotifyCharInput(ch) : -1);
}

int PageManager::SetKeyBoardFocus(int inFocus)
{
	return (mCurrentSet ? mCurrentSet->SetKeyBoardFocus(inFocus) : -1);
}

int PageManager::NotifyVarChange(std::string varName, std::string value)
{
	return (mCurrentSet ? mCurrentSet->NotifyVarChange(varName, value) : -1);
}

void PageManager::AddStringResource(std::string resource_source, std::string resource_name, std::string value)
{
	if (mCurrentSet)
		mCurrentSet->AddStringResource(resource_source, resource_name, value);
}

extern "C" void gui_notifyVarChange(const char *name, const char* value)
{
	if (!gGuiRunning)
		return;

	PageManager::NotifyVarChange(name, value);
}
