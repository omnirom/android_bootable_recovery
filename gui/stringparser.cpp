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

#include <string>
#include "stringparser.hpp"
#include "objects.hpp"
#include "resources.hpp"
#include "pages.hpp"
#include "../data.hpp"

std::string StringParser::ParseAll(std::string str) {
	return Parse(str, PARSE_ALL);
}

std::string StringParser::ParseData(std::string str) {
	return Parse(str, PARSE_DATA);
}

std::string StringParser::ParseResource(std::string str) {
	return Parse(str, PARSE_RESOURCE);
}

std::string StringParser::Parse(std::string str, ParseType ptype) {
	// This function parses text for DataManager values encompassed by %value% in the XML
	// and ResourceManager string resources (%@resource_name%)
	size_t pos = 0;

	for (;;) {
		size_t next = str.find('%', pos);
		if (next == std::string::npos)
			return str;

		size_t end = str.find('%', next + 1);
		if (end == std::string::npos)
			return str;

		// We have a block of data
		std::string var = str.substr(next + 1, (end - next) - 1);
		str.erase(next, (end - next) + 1);

		if (next + 1 == end)
			str.insert(next, 1, '%');
		else
		{
			std::string value;
			if ((ptype == PARSE_ALL || ptype == PARSE_RESOURCE) && var.size() > 0 && var[0] == '@') {
				// this is a string resource ("%@string_name%")
				value = PageManager::GetResources()->FindString(var.substr(1));
				str.insert(next, value);
			}
			else if ((ptype == PARSE_ALL || ptype == PARSE_DATA) && DataManager::GetValue(var, value) == 0)
				str.insert(next, value);
		}

		pos = next + 1;
	}
}
