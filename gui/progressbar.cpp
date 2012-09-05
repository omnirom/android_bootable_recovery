// progressbar.cpp - GUIProgressBar object

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

extern "C" {
#include "../common.h"
#include "../minuitwrp/minui.h"
#include "../recovery_ui.h"
}

#include "rapidxml.hpp"
#include "objects.hpp"

GUIProgressBar::GUIProgressBar(xml_node<>* node)
{
    xml_attribute<>* attr;
    xml_node<>* child;

    mEmptyBar = NULL;
    mFullBar = NULL;
    mLastPos = 0;
    mSlide = 0.0;
    mSlideInc = 0.0;

    if (!node)
    {
        LOGE("GUIProgressBar created without XML node\n");
        return;
    }

    child = node->first_node("resource");
    if (child)
    {
        attr = child->first_attribute("empty");
        if (attr)
            mEmptyBar = PageManager::FindResource(attr->value());

        attr = child->first_attribute("full");
        if (attr)
            mFullBar = PageManager::FindResource(attr->value());
    }

    // Load the placement
    LoadPlacement(node->first_node("placement"), &mRenderX, &mRenderY);

    // Load the data
    child = node->first_node("data");
    if (child)
    {
        attr = child->first_attribute("min");
        if (attr)   mMinValVar = attr->value();

        attr = child->first_attribute("max");
        if (attr)   mMaxValVar = attr->value();

        attr = child->first_attribute("name");
        if (attr)   mCurValVar = attr->value();
    }

    if (mEmptyBar && mEmptyBar->GetResource())
    {
        mRenderW = gr_get_width(mEmptyBar->GetResource());
        mRenderH = gr_get_height(mEmptyBar->GetResource());
    }

    return;
}

int GUIProgressBar::Render(void)
{
    // This handles making sure timing updates occur
    Update();
    return RenderInternal();
}

int GUIProgressBar::RenderInternal(void)
{
    if (!mEmptyBar || !mEmptyBar->GetResource())    return -1;
    if (!mFullBar || !mFullBar->GetResource())      return -1;

    gr_blit(mEmptyBar->GetResource(), 0, 0, mRenderW, mRenderH, mRenderX, mRenderY);
    gr_blit(mFullBar->GetResource(), 0, 0, mLastPos, mRenderH, mRenderX, mRenderY);
    return 0;
}

int GUIProgressBar::Update(void)
{
    std::string str;
    int min, max, cur, pos;

    if (mMinValVar.empty())     min = 0;
    else
    {
        str.clear();
        if (atoi(mMinValVar.c_str()) != 0)      str = mMinValVar;
        else                                    DataManager::GetValue(mMinValVar, str);
        min = atoi(str.c_str());
    }

    if (mMaxValVar.empty())     max = 100;
    else
    {
        str.clear();
        if (atoi(mMaxValVar.c_str()) != 0)      str = mMaxValVar;
        else                                    DataManager::GetValue(mMaxValVar, str);
        max = atoi(str.c_str());
    }

    str.clear();
    DataManager::GetValue(mCurValVar, str);
    cur = atoi(str.c_str());

    // Do slide, if needed
    if (mSlideFrames)
    {
        mSlide += mSlideInc;
        mSlideFrames--;
        if (cur != (int) mSlide)
        {
            cur = (int) mSlide;
            DataManager::SetValue(mCurValVar, cur);
        }
    }

    // Normalize to 0
    max -= min;
    cur -= min;
    min = 0;

    if (cur < min)  cur = min;
    if (cur > max)  cur = max;

    if (max == 0)   pos = 0;
    else            pos = (cur * mRenderW) / max;

    if (pos == mLastPos)            return 0;
    mLastPos = pos;
    if (RenderInternal() != 0)      return -1;
    return 2;
}

int GUIProgressBar::NotifyVarChange(std::string varName, std::string value)
{
    static int nextPush = 0;

    if (varName.empty())
    {
        nextPush = 0;
        mLastPos = 0;
        mSlide = 0.0;
        mSlideInc = 0.0;
        return 0;
    }

    if (varName == "ui_progress_portion" || varName == "ui_progress_frames")
    {
        std::string str;
        int cur;

        if (mSlideFrames)
        {
            mSlide += (mSlideInc * mSlideFrames);
            cur = (int) mSlide;
            DataManager::SetValue(mCurValVar, cur);
            mSlideFrames = 0;
        }

        if (nextPush)
        {
            mSlide += nextPush;
            cur = (int) mSlide;
            DataManager::SetValue(mCurValVar, cur);
            nextPush = 0;
        }

        if (varName == "ui_progress_portion")   mSlide = atof(value.c_str());
        else
        {
            mSlideFrames = atol(value.c_str());
            if (mSlideFrames == 0)
            {
                // We're just holding this progress until the next push
                nextPush = mSlide;
            }
        }

        if (mSlide > 0 && mSlideFrames > 0)
        {
            // Get the current position
            str.clear();
            DataManager::GetValue(mCurValVar, str);
            cur = atoi(str.c_str());

            mSlideInc = (float) mSlide / (float) mSlideFrames;
            mSlide = cur;
        }
    }
    return 0;
}

