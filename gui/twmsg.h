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

#ifndef twmsg_h
#define twmsg_h

#include <cstdlib>
#include <cstring>
#include <string>
#include <sstream>
#include <vector>
#include <errno.h>

/*
Abstract interface for something that can look up strings by name.
*/
class StringLookup
{
public:
	virtual std::string operator()(const std::string& name) const = 0;
	virtual ~StringLookup() {};
};


namespace msg
{
	// These get translated to colors in the GUI console
	enum Kind
	{
		kNormal,
		kHighlight,
		kWarning,
		kError
	};


	template<typename T> std::string to_string(const T& v)
	{
		std::ostringstream ss;
		ss << v;
		return ss.str();
	}
}


/*
Generic message formatting class.
Designed to decouple message generation from actual resource string lookup and variable insertion,
so that messages can be re-translated at any later time.
*/
class Message
{
	msg::Kind kind; // severity or similar message kind
	std::string name; // the resource string name. may be of format "name=default value".
	std::vector<std::string> variables; // collected insertion variables to replace {1}, {2}, ...
	const StringLookup& resourceLookup; // object to resolve resource string name into a final format string
	const StringLookup& varLookup; // object to resolve any non-numeric insertion strings

	std::string GetFormatString(const std::string& name) const;

public:
	Message(msg::Kind kind, const char* name, const StringLookup& resourceLookup, const StringLookup& varLookup)
		: kind(kind), name(name), resourceLookup(resourceLookup), varLookup(varLookup) {}

	// Variable insertion.
	template<typename T>
	Message& operator()(const T& v) { variables.push_back(msg::to_string(v)); return *this; }

	// conversion to final string
	operator std::string() const;

	// Get Message Kind
	msg::Kind GetKind() {return kind;};
};


// Utility functions to create messages with standard resource and data manager lookups.
// Short names to make usage convenient.
Message Msg(const char* name);
Message Msg(msg::Kind kind, const char* name);

#endif
