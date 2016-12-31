/*
    Copyright 2017 TeamWin
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

#ifndef TWCOMMON_HPP
#define TWCOMMON_HPP

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BUILD_TWRPTAR_MAIN
#include "gui/gui.h"
#define LOGERR(...) gui_print_color("error", "E:" __VA_ARGS__)
#define LOGINFO(...) fprintf(stdout, "I:" __VA_ARGS__)
#else
#include <stdio.h>
#define LOGERR(...) printf("E:" __VA_ARGS__)
#define LOGINFO(...) printf("I:" __VA_ARGS__)
#define gui_print(...) printf( __VA_ARGS__ )
#endif

#define STRINGIFY(x) #x
#define EXPAND(x) STRINGIFY(x)

#ifdef __cplusplus
}
#endif

#endif  // TWCOMMON_HPP
