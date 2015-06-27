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

extern int gGuiRunning;

std::map<std::string, PageSet*> PageManager::mPageSets;
PageSet* PageManager::mCurrentSet;
PageSet* PageManager::mBaseSet = NULL;
MouseCursor *PageManager::mMouseCursor = NULL;
HardwareKeyboard *PageManager::mHardwareKeyboard = NULL;

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
		// Search for stylename in the parent node <object type="foo" stylename="foo2">
		xml_attribute<>* attr = parent->first_attribute("style");
		// If no style is found anywhere else and the node wasn't found in the object itself
		// as a special case we will search for a style that uses the same style name as the
		// object type, so <object type="button"> would search for a style named "button"
		if (!attr)
			attr = parent->first_attribute("type");
		if (attr) {
			xml_node<>* node = PageManager::FindStyle(attr->value());
			if (node) {
				xml_node<>* stylenode = FindNode(node, nodename, depth + 1);
				if (stylenode)
					return stylenode;
			}
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

bool LoadPlacement(xml_node<>* node, int* x, int* y, int* w /* = NULL */, int* h /* = NULL */, RenderObject::Placement* placement /* = NULL */)
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
		*placement = (RenderObject::Placement) LoadAttrInt(node, "placement");

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

	// Don't try to handle a lack of handlers
	if (mActions.size() == 0)
		return 1;

	int ret = 1;
	// We work backwards, from top-most element to bottom-most element
	for (iter = mActions.rbegin(); iter != mActions.rend(); iter++)
	{
		ret = (*iter)->NotifyKey(key, down);
		if (ret < 0) {
			LOGERR("An action handler has returned an error\n");
			ret = 1;
		}
	}
	return ret;
}

int Page::NotifyKeyboard(int key)
{
	std::vector<InputObject*>::reverse_iterator iter;

	// Don't try to handle a lack of handlers
	if (mInputs.size() == 0)
		return 1;

	// We work backwards, from top-most element to bottom-most element
	for (iter = mInputs.rbegin(); iter != mInputs.rend(); iter++)
	{
		int ret = (*iter)->NotifyKeyboard(key);
		if (ret == 0)
			return 0;
		else if (ret < 0)
			LOGERR("A keyboard handler has returned an error");
	}
	return 1;
}

int Page::SetKeyBoardFocus(int inFocus)
{
	std::vector<InputObject*>::reverse_iterator iter;

	// Don't try to handle a lack of handlers
	if (mInputs.size() == 0)
		return 1;

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

PageSet::PageSet(char* xmlFile)
{
	mResources = new ResourceManager;
	mCurrentPage = NULL;

	mXmlFile = xmlFile;
	if (xmlFile)
		mDoc.parse<0>(mXmlFile);
	else
		mCurrentPage = new Page(NULL, NULL);
}

PageSet::~PageSet()
{
	mOverlays.clear();
	for (std::vector<Page*>::iterator itr = mPages.begin(); itr != mPages.end(); ++itr)
		delete *itr;

	delete mResources;
	free(mXmlFile);

	mDoc.clear();

	for (std::vector<xml_document<>*>::iterator itr = mIncludedDocs.begin(); itr != mIncludedDocs.end(); ++itr) {
		(*itr)->clear();
		delete *itr;
	}
}

int PageSet::Load(ZipArchive* package)
{
	xml_node<>* parent;
	xml_node<>* child;
	xml_node<>* xmltemplate;
	xml_node<>* xmlstyle;

	parent = mDoc.first_node("recovery");
	if (!parent)
		parent = mDoc.first_node("install");

	set_scale_values(1, 1); // Reset any previous scaling values

	// Now, let's parse the XML
	LOGINFO("Checking resolution...\n");
	child = parent->first_node("details");
	if (child) {
		xml_node<>* resolution = child->first_node("resolution");
		if (resolution) {
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
	LOGINFO("Loading resources...\n");
	child = parent->first_node("resources");
	if (child)
		mResources->LoadResources(child, package);

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
			return -1;
		}
	}

	return CheckInclude(package, &mDoc);
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
			LOGINFO("PageSet::CheckInclude loading filename: '%s'\n", filename.c_str());
			struct stat st;
			if(stat(filename.c_str(),&st) != 0) {
				LOGERR("Unable to locate '%s'\n", filename.c_str());
				return -1;
			}

			len = st.st_size;
			xmlFile = (char*) malloc(len + 1);
			if (!xmlFile)
				return -1;

			int fd = open(filename.c_str(), O_RDONLY);
			if (fd == -1)
				return -1;

			read(fd, xmlFile, len);
			close(fd);
		} else {
			filename += attr->value();
			LOGINFO("PageSet::CheckInclude loading filename: '%s'\n", filename.c_str());
			const ZipEntry* ui_xml = mzFindZipEntry(package, filename.c_str());
			if (ui_xml == NULL)
			{
				LOGERR("Unable to locate '%s' in zip file\n", filename.c_str());
				return -1;
			}

			// Allocate the buffer for the file
			len = mzGetZipEntryUncompLen(ui_xml);
			xmlFile = (char*) malloc(len + 1);
			if (!xmlFile)
				return -1;

			if (!mzExtractZipEntryToBuffer(package, ui_xml, (unsigned char*) xmlFile))
			{
				LOGERR("Unable to extract '%s'\n", filename.c_str());
				return -1;
			}
		}

		xmlFile[len] = '\0';
		doc = new xml_document<>();
		doc->parse<0>(xmlFile);

		parent = doc->first_node("recovery");
		if (!parent)
			parent = doc->first_node("install");

		// Now, let's parse the XML
		LOGINFO("Loading included resources...\n");
		child = parent->first_node("resources");
		if (child)
			mResources->LoadResources(child, package);

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
			return -1;
		}

		mIncludedDocs.push_back(doc);

		if (CheckInclude(package, doc))
			return -1;

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

int PageSet::NotifyKeyboard(int key)
{
	if (!mOverlays.empty())
		return mOverlays.back()->NotifyKeyboard(key);

	return (mCurrentPage ? mCurrentPage->NotifyKeyboard(key) : -1);
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

int PageManager::LoadPackage(std::string name, std::string package, std::string startpage)
{
	int fd;
	ZipArchive zip, *pZip = NULL;
	long len;
	char* xmlFile = NULL;
	PageSet* pageSet = NULL;
	int ret;
	MemMapping map;

	// Open the XML file
	LOGINFO("Loading package: %s (%s)\n", name.c_str(), package.c_str());
	if (package.size() > 4 && package.substr(package.size() - 4) != ".zip")
	{
		LOGINFO("Load XML directly\n");
		tw_x_offset = TW_X_OFFSET;
		tw_y_offset = TW_Y_OFFSET;
		// We can try to load the XML directly...
		struct stat st;
		if(stat(package.c_str(),&st) != 0)
			return -1;

		len = st.st_size;
		xmlFile = (char*) malloc(len + 1);
		if (!xmlFile)
			return -1;

		fd = open(package.c_str(), O_RDONLY);
		if (fd == -1)
			goto error;

		read(fd, xmlFile, len);
		close(fd);
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
			return -1;
		}
		if (mzOpenZipArchive(map.addr, map.length, &zip)) {
			LOGERR("Unable to open zip archive '%s'\n", package.c_str());
			sysReleaseMap(&map);
			return -1;
		}
		pZip = &zip;
		const ZipEntry* ui_xml = mzFindZipEntry(&zip, "ui.xml");
		if (ui_xml == NULL)
		{
			LOGERR("Unable to locate ui.xml in zip file\n");
			goto error;
		}

		// Allocate the buffer for the file
		len = mzGetZipEntryUncompLen(ui_xml);
		xmlFile = (char*) malloc(len + 1);
		if (!xmlFile)
			goto error;

		if (!mzExtractZipEntryToBuffer(&zip, ui_xml, (unsigned char*) xmlFile))
		{
			LOGERR("Unable to extract ui.xml\n");
			goto error;
		}
	}

	// NULL-terminate the string
	xmlFile[len] = 0x00;

	// Before loading, mCurrentSet must be the loading package so we can find resources
	pageSet = mCurrentSet;
	mCurrentSet = new PageSet(xmlFile);

	ret = mCurrentSet->Load(pZip);
	if (ret == 0)
	{
		mCurrentSet->SetPage(startpage);
		mPageSets.insert(std::pair<std::string, PageSet*>(name, mCurrentSet));
	}
	else
	{
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
	return ret;

error:
	LOGERR("An internal error has occurred.\n");
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

	iter = mPageSets.find(name);
	if (iter == mPageSets.end())
		return -1;

	if(mMouseCursor)
		mMouseCursor->ResetData(gr_fb_width(), gr_fb_height());

	PageSet* set = (*iter).second;
	mPageSets.erase(iter);

	if (LoadPackage(name, package, "main") != 0)
	{
		LOGERR("Failed to load package '%s'.\n", package.c_str());
		mPageSets.insert(std::pair<std::string, PageSet*>(name, set));
		return -1;
	}
	if (mCurrentSet == set)
		SelectPackage(name);
	if (mBaseSet == set)
		mBaseSet = mCurrentSet;
	delete set;
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

int PageManager::ChangePage(std::string name)
{
	DataManager::SetValue("tw_operation_state", 0);
	int ret = (mCurrentSet ? mCurrentSet->SetPage(name) : -1);
	return ret;
}

int PageManager::ChangeOverlay(std::string name)
{
	if (name.empty())
		return mCurrentSet->SetOverlay(NULL);
	else
	{
		Page* page = mBaseSet ? mBaseSet->FindPage(name) : NULL;
		return mCurrentSet->SetOverlay(page);
	}
}

const ResourceManager* PageManager::GetResources()
{
	return (mCurrentSet ? mCurrentSet->GetResources() : NULL);
}

int PageManager::SwitchToConsole(void)
{
	PageSet* console = new PageSet(NULL);

	mCurrentSet = console;
	return 0;
}

int PageManager::EndConsole(void)
{
	if (mCurrentSet && mBaseSet) {
		delete mCurrentSet;
		mCurrentSet = mBaseSet;
		return 0;
	}
	return -1;
}

int PageManager::IsCurrentPage(Page* page)
{
	return (mCurrentSet ? mCurrentSet->IsCurrentPage(page) : 0);
}

int PageManager::Render(void)
{
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

int PageManager::NotifyKeyboard(int key)
{
	return (mCurrentSet ? mCurrentSet->NotifyKeyboard(key) : -1);
}

int PageManager::SetKeyBoardFocus(int inFocus)
{
	return (mCurrentSet ? mCurrentSet->SetKeyBoardFocus(inFocus) : -1);
}

int PageManager::NotifyVarChange(std::string varName, std::string value)
{
	return (mCurrentSet ? mCurrentSet->NotifyVarChange(varName, value) : -1);
}

extern "C" void gui_notifyVarChange(const char *name, const char* value)
{
	if (!gGuiRunning)
		return;

	PageManager::NotifyVarChange(name, value);
}
