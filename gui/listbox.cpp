// ListBox.cpp - GUIListBox object

#include <linux/input.h>
#include <pthread.h>
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
#include <dirent.h>
#include <ctype.h>

#include <algorithm>

extern "C" {
#include "../common.h"
#include "../roots.h"
#include "../minuitwrp/minui.h"
#include "../recovery_ui.h"
}

#include "rapidxml.hpp"
#include "objects.hpp"
#include "../data.hpp"

GUIListBox::GUIListBox(xml_node<>* node)
{
    xml_attribute<>* attr;
    xml_node<>* child;

    mStart = mLineSpacing = mIconWidth = mIconHeight = 0;
    mIconSelected = mIconUnselected = mBackground = mFont = NULL;
    mBackgroundX = mBackgroundY = mBackgroundW = mBackgroundH = 0;
    mUpdate = 0;
    ConvertStrToColor("black", &mBackgroundColor);
    ConvertStrToColor("white", &mFontColor);
    
    child = node->first_node("icon");
    if (child)
    {
        attr = child->first_attribute("selected");
        if (attr)
            mIconSelected = PageManager::FindResource(attr->value());
        attr = child->first_attribute("unselected");
        if (attr)
            mIconUnselected = PageManager::FindResource(attr->value());
    }
    child = node->first_node("background");
    if (child)
    {
        attr = child->first_attribute("resource");
        if (attr)
            mBackground = PageManager::FindResource(attr->value());
        attr = child->first_attribute("color");
        if (attr)
        {
            std::string color = attr->value();
            ConvertStrToColor(color, &mBackgroundColor);
        }
    }

    // Load the placement
    LoadPlacement(node->first_node("placement"), &mRenderX, &mRenderY, &mRenderW, &mRenderH);
    SetActionPos(mRenderX, mRenderY, mRenderW, mRenderH);

    // Load the font, and possibly override the color
    child = node->first_node("font");
    if (child)
    {
        attr = child->first_attribute("resource");
        if (attr)
            mFont = PageManager::FindResource(attr->value());

        attr = child->first_attribute("color");
        if (attr)
        {
            std::string color = attr->value();
            ConvertStrToColor(color, &mFontColor);
        }

        attr = child->first_attribute("spacing");
        if (attr) {
			string parsevalue = gui_parse_text(attr->value());
            mLineSpacing = atoi(parsevalue.c_str());
		}
    }

    // Handle the result variable
    child = node->first_node("data");
    if (child)
    {
        attr = child->first_attribute("name");
        if (attr)
            mVariable = attr->value();
        attr = child->first_attribute("default");
        if (attr)
            DataManager::SetValue(mVariable, attr->value());
    }

    // Retrieve the line height
    gr_getFontDetails(mFont ? mFont->GetResource() : NULL, &mFontHeight, NULL);
    mLineHeight = mFontHeight;
    if (mIconSelected && mIconSelected->GetResource())
    {
        if (gr_get_height(mIconSelected->GetResource()) > mLineHeight)
            mLineHeight = gr_get_height(mIconSelected->GetResource());
        mIconWidth = gr_get_width(mIconSelected->GetResource());
        mIconHeight = gr_get_height(mIconSelected->GetResource());
    }
    if (mIconUnselected && mIconUnselected->GetResource())
    {
        if (gr_get_height(mIconUnselected->GetResource()) > mLineHeight)
            mLineHeight = gr_get_height(mIconUnselected->GetResource());
        mIconWidth = gr_get_width(mIconUnselected->GetResource());
        mIconHeight = gr_get_height(mIconUnselected->GetResource());
    }
        
    if (mBackground && mBackground->GetResource())
    {
        mBackgroundW = gr_get_width(mBackground->GetResource());
        mBackgroundH = gr_get_height(mBackground->GetResource());
    }
	
	// Get the currently selected value for the list
	DataManager::GetValue(mVariable, currentValue);

	// Get the data for the list
	child = node->first_node("listitem");
    if (!child) return;
	
	while (child)
    {
        ListData data;

		attr = child->first_attribute("name");
        if (!attr) return;
        data.displayName = attr->value();

		data.variableValue = child->value();
		if (child->value() == currentValue)
			data.selected = 1;
		else
			data.selected = 0;

        mList.push_back(data);

        child = child->next_sibling("listitem");
    }

	// Call this to get the selected item to be shown in the list on first render
	NotifyVarChange(mVariable, currentValue);
}

GUIListBox::~GUIListBox()
{
}

int GUIListBox::Render(void)
{	
	// First step, fill background
    gr_color(mBackgroundColor.red, mBackgroundColor.green, mBackgroundColor.blue, 255);
    gr_fill(mRenderX, mRenderY, mRenderW, mRenderH);

    // Next, render the background resource (if it exists)
    if (mBackground && mBackground->GetResource())
    {
        mBackgroundX = mRenderX + ((mRenderW - mBackgroundW) / 2);
        mBackgroundY = mRenderY + ((mRenderH - mBackgroundH) / 2);
        gr_blit(mBackground->GetResource(), 0, 0, mBackgroundW, mBackgroundH, mBackgroundX, mBackgroundY);
    }

    // Now, we need the lines (icon + text)
    gr_color(mFontColor.red, mFontColor.green, mFontColor.blue, mFontColor.alpha);

    // This tells us how many lines we can actually render
    int lines = mRenderH / (mLineHeight + mLineSpacing);
    int line;

    int listSize = mList.size();

    if (listSize < lines)  lines = listSize;

    void* fontResource = NULL;
    if (mFont)  fontResource = mFont->GetResource();

    int yPos = mRenderY + (mLineSpacing / 2);
    for (line = 0; line < lines; line++)
    {
        Resource* icon;
        std::string label;

        label = mList.at(line + mStart).displayName;
		if (mList.at(line + mStart).selected != 0)
        {
            icon = mIconSelected;
        }
        else
        {
            icon = mIconUnselected;
        }

        if (icon && icon->GetResource())
        {
            gr_blit(icon->GetResource(), 0, 0, mIconWidth, mIconHeight, mRenderX, (yPos + (int)((mLineHeight - mIconHeight) / 2)));
        }
        gr_textExW(mRenderX + mIconWidth + 5, yPos, label.c_str(), fontResource, mRenderX + mRenderW - mIconWidth - 5);

        // Move the yPos
        yPos += mLineHeight + mLineSpacing;
    }

    mUpdate = 0;
    return 0;
}

int GUIListBox::Update(void)
{
    if (mUpdate)
    {
        mUpdate = 0;
        if (Render() == 0)
			return 2;
    }
    return 0;
}

int GUIListBox::GetSelection(int x, int y)
{
    // We only care about y position
    return (y - mRenderY) / (mLineHeight + mLineSpacing);
}

int GUIListBox::NotifyTouch(TOUCH_STATE state, int x, int y)
{
    static int startSelection = -1;
    static int startY = 0;
    int selection = 0;

    switch (state)
    {
    case TOUCH_START:
        startSelection = GetSelection(x,y);
        startY = y;
        break;

    case TOUCH_DRAG:
        // Check if we dragged out of the selection window
        selection = GetSelection(x,y);
        if (startSelection != selection)
        {
            startSelection = -1;

            // Handle scrolling
            if (y > (int) (startY + (mLineHeight + mLineSpacing)))
            {
                if (mStart)     mStart--;
                mUpdate = 1;
                startY = y;
            }
            else if (y < (int) (startY - (mLineHeight + mLineSpacing)))
            {
                int listSize = mList.size();
                int lines = mRenderH / (mLineHeight + mLineSpacing);

                if (mStart + lines < listSize)     mStart++;
                mUpdate = 1;
                startY = y;
            }
        }
        break;

    case TOUCH_RELEASE:
        if (startSelection >= 0)
        {
            // We've selected an item!
            std::string str;

            int listSize = mList.size();

            // Move the selection to the proper place in the array
            startSelection += mStart;

            if (startSelection < listSize)
            {
                if (!mVariable.empty())
                {
                    int i;
					for (i=0; i<listSize; i++)
						mList.at(i).selected = 0;

					str = mList.at(startSelection).variableValue;
					mList.at(startSelection).selected = 1;
                    DataManager::SetValue(mVariable, str);
					mUpdate = 1;
                }
            }
        }
	case TOUCH_HOLD:
	case TOUCH_REPEAT:
        break;
    }
    return 0;
}

int GUIListBox::NotifyVarChange(std::string varName, std::string value)
{
    string checkValue;
	int var_changed = 0;

	if (varName.empty())
    {
		DataManager::GetValue(mVariable, checkValue);
		if (checkValue != currentValue) {
			varName = mVariable;
			value = checkValue;
			currentValue = checkValue;
			var_changed = 1;
		}
    }
    if (varName == mVariable || var_changed != 0)
    {
        int i, listSize = mList.size(), selected_index = 0;

		currentValue = value;

		for (i=0; i<listSize; i++) {
			if (mList.at(i).variableValue == currentValue) {
				mList.at(i).selected = 1;
				selected_index = i;
			} else
				mList.at(i).selected = 0;
		}

		int lines = mRenderH / (mLineHeight + mLineSpacing);
		int line;

		if (selected_index > mStart + lines - 1)
			mStart = selected_index;
		if (mStart > listSize - lines) {
			mStart = listSize - lines;
		} else if (selected_index < mStart) {
			mStart = selected_index;
		}

		mUpdate = 1;
        return 0;
    }
    return 0;
}

int GUIListBox::SetRenderPos(int x, int y, int w /* = 0 */, int h /* = 0 */)
{
    mRenderX = x;
    mRenderY = y;
    if (w || h)
    {
        mRenderW = w;
        mRenderH = h;
    }
    SetActionPos(mRenderX, mRenderY, mRenderW, mRenderH);
    mUpdate = 1;
    return 0;
}

void GUIListBox::SetPageFocus(int inFocus)
{
    if (inFocus)
    {
        mUpdate = 1;
    }
}

