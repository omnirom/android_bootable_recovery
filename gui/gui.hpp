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

void gui_translate_va(const char *lookup, const char* default_value, const char *fmt, ...);
void gui_translate_color(const char *color, const char *lookup, const char* default_value, const char *fmt, ...);
void gui_translate(const char *lookup, const char* default_value, const int val);
void gui_translate(const char *lookup, const char* default_value, const char* val);
void gui_translate(const char *color, const char *lookup, const char* default_value, const int val);
void gui_translate(const char *color, const char *lookup, const char* default_value, const char* val);

#endif //_GUI_HPP_HEADER
