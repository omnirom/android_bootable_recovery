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

#include <linux/input.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdlib.h>

#include <string>
#include <utility>
#include <map>
#include <fstream>
#include <sstream>

#include "infomanager.hpp"
#include "twcommon.h"
#include "partitions.hpp"

using namespace std;

InfoManager::InfoManager(const string filename) {
	File = filename;
}

InfoManager::~InfoManager(void) {
	mValues.clear();
}

int InfoManager::LoadValues(void) {
	string str;

	// Read in the file, if possible
	FILE* in = fopen(File.c_str(), "rb");
	if (!in) {
		LOGINFO("InfoManager file '%s' not found.\n", File.c_str());
		return -1;
	} else {
		LOGINFO("InfoManager loading from '%s'.\n", File.c_str());
	}

	while (!feof(in)) {
		string Name;
		string Value;
		unsigned short length;
		char array[512];

		if (fread(&length, 1, sizeof(unsigned short), in) != sizeof(unsigned short))	goto error;
		if (length >= 512)																goto error;
		if (fread(array, 1, length, in) != length)										goto error;
		Name = array;

		if (fread(&length, 1, sizeof(unsigned short), in) != sizeof(unsigned short))	goto error;
		if (length >= 512)																goto error;
		if (fread(array, 1, length, in) != length)										goto error;
		Value = array;

		map<string, string>::iterator pos;

		pos = mValues.find(Name);
		if (pos != mValues.end()) {
			pos->second = Value;
		} else {
			mValues.insert(make_pair(Name, Value));
		}
	}
error:
	fclose(in);
	return 0;
}

int InfoManager::SaveValues(void) {
	if (File.empty())
		return -1;

	PartitionManager.Mount_By_Path(File, true);
LOGINFO("InfoManager saving '%s'\n", File.c_str());
	FILE* out = fopen(File.c_str(), "wb");
	if (!out)
		return -1;

	map<string, string>::iterator iter;
	for (iter = mValues.begin(); iter != mValues.end(); ++iter) {
		unsigned short length = (unsigned short) iter->first.length() + 1;
		fwrite(&length, 1, sizeof(unsigned short), out);
		fwrite(iter->first.c_str(), 1, length, out);
		length = (unsigned short) iter->second.length() + 1;
		fwrite(&length, 1, sizeof(unsigned short), out);
		fwrite(iter->second.c_str(), 1, length, out);
	}
	fclose(out);
	return 0;
}

int InfoManager::GetValue(const string varName, string& value) {
	string localStr = varName;

	map<string, string>::iterator pos;
	pos = mValues.find(localStr);
	if (pos == mValues.end())
		return -1;

	value = pos->second;
	return 0;
}

int InfoManager::GetValue(const string varName, int& value) {
	string data;

	if (GetValue(varName,data) != 0)
		return -1;

	value = atoi(data.c_str());
	return 0;
}

int InfoManager::GetValue(const string varName, float& value) {
	string data;

	if (GetValue(varName,data) != 0)
		return -1;

	value = atof(data.c_str());
	return 0;
}

unsigned long long InfoManager::GetValue(const string varName, unsigned long long& value) {
	string data;

	if (GetValue(varName,data) != 0)
		return -1;

	value = strtoull(data.c_str(), NULL, 10);
	return 0;
}

// This function will return an empty string if the value doesn't exist
string InfoManager::GetStrValue(const string varName) {
	string retVal;

	GetValue(varName, retVal);
	return retVal;
}

// This function will return 0 if the value doesn't exist
int InfoManager::GetIntValue(const string varName) {
	string retVal;
	GetValue(varName, retVal);
	return atoi(retVal.c_str());
}

int InfoManager::SetValue(const string varName, string value) {
	// Don't allow empty values or numerical starting values
	if (varName.empty() || (varName[0] >= '0' && varName[0] <= '9'))
		return -1;

	map<string, string>::iterator pos;
	pos = mValues.find(varName);
	if (pos == mValues.end())
		mValues.insert(make_pair(varName, value));
	else
		pos->second = value;

	return 0;
}

int InfoManager::SetValue(const string varName, int value) {
	ostringstream valStr;
	valStr << value;
	return SetValue(varName, valStr.str());
}

int InfoManager::SetValue(const string varName, float value) {
	ostringstream valStr;
	valStr << value;
	return SetValue(varName, valStr.str());
}

int InfoManager::SetValue(const string varName, unsigned long long value) {
	ostringstream valStr;
	valStr << value;
	return SetValue(varName, valStr.str());
}
