/*
        Copyright 2015 bigbiff/Dees_Troy TeamWin
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

#ifndef _GUI_HPP_HEADER
#define _GUI_HPP_HEADER

#include "twmsg.h"

void set_select_fd();

void gui_msg(const char* text);
void gui_warn(const char* text);
void gui_err(const char* text);
void gui_highlight(const char* text);
void gui_msg(Message msg);
void gui_err(Message msg);

std::string gui_parse_text(std::string inText);
std::string gui_lookup(const std::string& resource_name, const std::string& default_value);

#endif //_GUI_HPP_HEADER
