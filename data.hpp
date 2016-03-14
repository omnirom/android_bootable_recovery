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

#ifndef _DATAMANAGER_HPP_HEADER
#define _DATAMANAGER_HPP_HEADER

#include <string>
#include <pthread.h>
#include "infomanager.hpp"

using namespace std;

class DataManager
{
public:
	static int ResetDefaults();
	static int LoadValues(const string& filename);
	static int Flush();

	// Core get routines
	static int GetValue(const string& varName, string& value);
	static int GetValue(const string& varName, int& value);
	static int GetValue(const string& varName, float& value);
	static unsigned long long GetValue(const string& varName, unsigned long long& value);

	// Helper functions
	static string GetStrValue(const string& varName);
	static int GetIntValue(const string& varName);

	// Core set routines
	static int SetValue(const string& varName, const string& value, const int persist = 0);
	static int SetValue(const string& varName, const int value, const int persist = 0);
	static int SetValue(const string& varName, const float value, const int persist = 0);
	static int SetValue(const string& varName, const unsigned long long& value, const int persist = 0);
	static int SetProgress(const float Fraction);
	static int ShowProgress(const float Portion, const float Seconds);

	static void DumpValues();
	static void update_tz_environment_variables();
	static void Vibrate(const string& varName);
	static void SetBackupFolder();
	static void SetDefaultValues();
	static void Output_Version(void); // Outputs the version to a file in the TWRP folder
	static void ReadSettingsFile(void);

	static string GetCurrentStoragePath(void);
	static string GetSettingsStoragePath(void);

protected:
	static string mBackingFile;
	static int mInitialized;
	static InfoManager mPersist;
	static InfoManager mData;
	static InfoManager mConst;

	static map<string, string> mConstValues;

protected:
	static int SaveValues();

	static int GetMagicValue(const string& varName, string& value);

private:
	static void sanitize_device_id(char* device_id);
	static void get_device_id(void);

	static pthread_mutex_t m_valuesLock;
};

#endif // _DATAMANAGER_HPP_HEADER

