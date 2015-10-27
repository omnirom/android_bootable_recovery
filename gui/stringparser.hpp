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

#ifndef __STRINGPARSER_HPP
#define __STRINGPARSER_HPP

#include <string>

class StringParser {
public:
	static std::string ParseAll(std::string str);
	static std::string ParseData(std::string str);
	static std::string ParseResource(std::string str);

private:
	enum ParseType { PARSE_ALL, PARSE_DATA, PARSE_RESOURCE };

	static std::string Parse(std::string str, ParseType ptype);

};

#endif //__STRINGPARSER_HPP
