/*
	Copyright 2015 _that/TeamWin
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

#include "../data.hpp"
#include "pages.hpp"
#include "resources.hpp"

#include "twmsg.h"

std::string Message::GetFormatString(const std::string& name) const
{
	std::string resname;
	size_t pos = name.find('=');
	if (pos == std::string::npos)
		resname = name;
	else
		resname = name.substr(0, pos);

	std::string formatstr = resourceLookup(resname);
	bool notfound = formatstr.empty() || formatstr[0] == '[';  // HACK: TODO: integrate this with resource-not-found logic
	if (notfound && pos != std::string::npos)
		// resource not found - use the default format string specified after "="
		formatstr = name.substr(pos + 1);
	return formatstr;
}

// Look up in local replacement vars first, if not found then use outer lookup object
class LocalLookup : public StringLookup
{
	const std::vector<std::string>& vars;
	const StringLookup& next;
	public:
	LocalLookup(const std::vector<std::string>& vars, const StringLookup& next) : vars(vars), next(next) {}
	virtual std::string operator()(const std::string& name) const
	{
		if (!name.empty() && isdigit(name[0])) { // {1}..{9}
			int i = atoi(name.c_str());
			if (i > 0 && i <= (int)vars.size())
				return vars[i - 1];
		}
		return next(name);
	}
};

// conversion to final string
Message::operator std::string() const
{
	// do resource lookup
	std::string str = GetFormatString(name);

	LocalLookup lookup(variables, varLookup);

	// now insert stuff into curly braces

	size_t pos = 0;
	while ((pos = str.find('{', pos)) < std::string::npos) {
		size_t end = str.find('}', pos);
		if (end == std::string::npos)
			break;

		std::string varname = str.substr(pos + 1, end - pos - 1);
		std::string vartext = lookup(varname);

		str.replace(pos, end - pos + 1, vartext);
	}
	// TODO: complain about too many or too few numbered replacement variables
	return str;
}

/*
Resource manager lookup
*/
class ResourceLookup : public StringLookup
{
public:
	virtual std::string operator()(const std::string& name) const
	{
		const ResourceManager* res = PageManager::GetResources();
		if (res)
			return res->FindString(name);
		return name;
	}
};
ResourceLookup resourceLookup;


/*
DataManager lookup
*/
class DataLookup : public StringLookup
{
public:
	virtual std::string operator()(const std::string& name) const
	{
		std::string value;
		if (DataManager::GetValue(name, value) == 0)
			return value;
		else
			return "";
	}
};
DataLookup dataLookup;


// Utility functions to create messages. Short names to make usage convenient.
Message Msg(const char* name)
{
	return Message(msg::kNormal, name, resourceLookup, dataLookup);
}

Message Msg(msg::Kind kind, const char* name)
{
	return Message(kind, name, resourceLookup, dataLookup);
}
