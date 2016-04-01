/*
	Copyright 2012 bigbiff/Dees_Troy TeamWin
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

#ifndef _INFOMANAGER_HPP_HEADER
#define _INFOMANAGER_HPP_HEADER

#include <string>
#include <utility>
#include <map>

using namespace std;

class InfoManager
{
public:
	InfoManager();
	explicit InfoManager(const string& filename);
	virtual ~InfoManager();
	void SetFile(const string& filename);
	void SetFileVersion(int version);
	void SetConst();
	void Clear();
	int LoadValues();
	int SaveValues();

	// Core get routines
	int GetValue(const string& varName, string& value);
	int GetValue(const string& varName, int& value);
	int GetValue(const string& varName, float& value);
	unsigned long long GetValue(const string& varName, unsigned long long& value);

	string GetStrValue(const string& varName);
	int GetIntValue(const string& varName);

	// Core set routines
	int SetValue(const string& varName, const string& value);
	int SetValue(const string& varName, const int value);
	int SetValue(const string& varName, const float value);
	int SetValue(const string& varName, const unsigned long long& value);

private:
	string File;
	map<string, string> mValues;
	int file_version;
	bool is_const;

};

#endif // _DATAMANAGER_HPP_HEADER

