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
#include "../twcommon.h"
}
#include "../minuitwrp/minui.h"

#include "rapidxml.hpp"
#include "objects.hpp"

MouseCursor::MouseCursor(int resX, int resY)
{
	ResetData(resX, resY);
}

MouseCursor::~MouseCursor()
{
}

void MouseCursor::ResetData(int resX, int resY)
{
	m_resX = resX;
	m_resY = resY;
	m_moved = false;
	m_speedMultiplier = 2.5f;
	m_image = NULL;
	m_present = false;

	ConvertStrToColor("red", &m_color);

	SetRenderPos(resX/2, resY/2, 10, 10);
}

void MouseCursor::LoadData(xml_node<>* node)
{
	xml_attribute<>* attr;
	xml_node<>* child;

	child = FindNode(node, "placement");
	if(child)
		LoadPlacement(child, &mRenderX, &mRenderY, &mRenderW, &mRenderH);

	child = FindNode(node, "background");
	if(child)
	{
		m_color = LoadAttrColor(child, "color", m_color);
		m_image = LoadAttrImage(child, "resource");
		if(m_image)
		{
			mRenderW = m_image->GetWidth();
			mRenderH = m_image->GetHeight();
		}
	}

	child = FindNode(node, "speed");
	if(child)
	{
		attr = child->first_attribute("multiplier");
		if(attr)
			m_speedMultiplier = atof(attr->value());
	}
}

int MouseCursor::Render(void)
{
	if(!m_present)
		return 0;

	if(m_image)
	{
		gr_blit(m_image->GetResource(), 0, 0, mRenderW, mRenderH, mRenderX, mRenderY);
	}
	else
	{
		gr_color(m_color.red, m_color.green, m_color.blue, m_color.alpha);
		gr_fill(mRenderX, mRenderY, mRenderW, mRenderH);
	}
	return 0;
}

int MouseCursor::Update(void)
{
	if(m_present != ev_has_mouse())
	{
		m_present = ev_has_mouse();
		if(m_present)
			SetRenderPos(m_resX/2, m_resY/2);
		return 2;
	}

	if(m_present && m_moved)
	{
		m_moved = false;
		return 2;
	}
	return 0;
}

int MouseCursor::SetRenderPos(int x, int y, int w, int h)
{
	if(x == mRenderX && y == mRenderY)
		m_moved = true;

	return RenderObject::SetRenderPos(x, y, w, h);
}

void MouseCursor::Move(int deltaX, int deltaY)
{
	if(deltaX != 0)
	{
		mRenderX += deltaX*m_speedMultiplier;
		mRenderX = (std::min)(mRenderX, m_resX);
		mRenderX = (std::max)(mRenderX, 0);

		m_moved = true;
	}

	if(deltaY != 0)
	{
		mRenderY += deltaY*m_speedMultiplier;
		mRenderY = (std::min)(mRenderY, m_resY);
		mRenderY = (std::max)(mRenderY, 0);

		m_moved = true;
	}
}

void MouseCursor::GetPos(int& x, int& y)
{
	x = mRenderX;
	y = mRenderY;
}
