/*
        Copyright 2014 Tom Hite (for TeamWin)
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

#ifndef __LED_HEADER_HPP
#define __LED_HEADER_HPP

using namespace std;

class batteryled
{
public:
	batteryled(void);
	bool setCharging(bool charging, char status);
	bool getCharging(void);
};

#endif // __LED_HEADER_HPP
