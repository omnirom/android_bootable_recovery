// fill.cpp - GUIFill object

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

GUIFill::GUIFill(xml_node<>* node) : GUIObject(node)
{
	bool has_color = false;
	mColor = LoadAttrColor(node, "color", &has_color);
	if (!has_color) {
		LOGERR("No color specified for fill\n");
		return;
	}

	// Load the placement
	LoadPlacement(FindNode(node, "placement"), &mRenderX, &mRenderY, &mRenderW, &mRenderH);

	return;
}

int GUIFill::Render(void)
{
	if(!isConditionTrue())
		return 0;

	gr_color(mColor.red, mColor.green, mColor.blue, mColor.alpha);
	gr_fill(mRenderX, mRenderY, mRenderW, mRenderH);
	return 0;
}

