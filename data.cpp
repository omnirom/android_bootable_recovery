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
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>

#include <string>
#include <utility>
#include <map>
#include <fstream>
#include <sstream>
#include <pthread.h>

#include "variables.h"
#include "data.hpp"
#include "partitions.hpp"
#include "twrp-functions.hpp"
#ifndef TW_NO_SCREEN_TIMEOUT
#include "gui/blanktimer.hpp"
#endif
#include "find_file.hpp"
#include "set_metadata.h"
#include <cutils/properties.h>
#include "gui/gui.hpp"

#define DEVID_MAX 64
#define HWID_MAX 32

extern "C"
{
	#include "twcommon.h"
	#include "gui/pages.h"
	#include "minuitwrp/minui.h"
	void gui_notifyVarChange(const char *name, const char* value);
}

#define FILE_VERSION 0x00010010

using namespace std;

map<string, DataManager::TStrIntPair>   DataManager::mValues;
map<string, string>                     DataManager::mConstValues;
string                                  DataManager::mBackingFile;
int                                     DataManager::mInitialized = 0;

extern bool datamedia;

#ifndef PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP
pthread_mutex_t DataManager::m_valuesLock = PTHREAD_RECURSIVE_MUTEX_INITIALIZER;
#else
pthread_mutex_t DataManager::m_valuesLock = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
#endif

// Device ID functions
void DataManager::sanitize_device_id(char* device_id) {
	const char* whitelist ="-._";
	char str[DEVID_MAX];
	char* c = str;

	snprintf(str, DEVID_MAX, "%s", device_id);
	memset(device_id, 0, strlen(device_id));
	while (*c) {
		if (isalnum(*c) || strchr(whitelist, *c))
			strncat(device_id, c, 1);
		c++;
	}
	return;
}

#define CMDLINE_SERIALNO		"androidboot.serialno="
#define CMDLINE_SERIALNO_LEN	(strlen(CMDLINE_SERIALNO))
#define CPUINFO_SERIALNO		"Serial"
#define CPUINFO_SERIALNO_LEN	(strlen(CPUINFO_SERIALNO))
#define CPUINFO_HARDWARE		"Hardware"
#define CPUINFO_HARDWARE_LEN	(strlen(CPUINFO_HARDWARE))

void DataManager::get_device_id(void) {
	FILE *fp;
	size_t i;
	char line[2048];
	char hardware_id[HWID_MAX] = { 0 };
	char device_id[DEVID_MAX] = { 0 };
	char* token;

#ifdef TW_USE_MODEL_HARDWARE_ID_FOR_DEVICE_ID
	// Use (product_model)_(hardware_id) as device id
	char model_id[PROPERTY_VALUE_MAX];
	property_get("ro.product.model", model_id, "error");
	if (strcmp(model_id, "error") != 0) {
		LOGINFO("=> product model: '%s'\n", model_id);
		// Replace spaces with underscores
		for (i = 0; i < strlen(model_id); i++) {
			if (model_id[i] == ' ')
				model_id[i] = '_';
		}
		snprintf(device_id, DEVID_MAX, "%s", model_id);

		if (strlen(device_id) < DEVID_MAX) {
			fp = fopen("proc_cpuinfo.txt", "rt");
			if (fp != NULL) {
				while (fgets(line, sizeof(line), fp) != NULL) {
					if (memcmp(line, CPUINFO_HARDWARE,
							CPUINFO_HARDWARE_LEN) == 0) {
						// skip past "Hardware", spaces, and colon
						token = line + CPUINFO_HARDWARE_LEN;
						while (*token && (!isgraph(*token) || *token == ':'))
							token++;

						if (*token && *token != '\n'
								&& strcmp("UNKNOWN\n", token)) {
							snprintf(hardware_id, HWID_MAX, "%s", token);
							if (hardware_id[strlen(hardware_id)-1] == '\n')
								hardware_id[strlen(hardware_id)-1] = 0;
							LOGINFO("=> hardware id from cpuinfo: '%s'\n",
									hardware_id);
						}
						break;
					}
				}
				fclose(fp);
			}
		}

		if (hardware_id[0] != 0)
			snprintf(device_id, DEVID_MAX, "%s_%s", model_id, hardware_id);

		sanitize_device_id(device_id);
		mConstValues.insert(make_pair("device_id", device_id));
		LOGINFO("=> using device id: '%s'\n", device_id);
		return;
	}
#endif

#ifndef TW_FORCE_CPUINFO_FOR_DEVICE_ID
	// Check the cmdline to see if the serial number was supplied
	fp = fopen("/proc/cmdline", "rt");
	if (fp != NULL) {
		fgets(line, sizeof(line), fp);
		fclose(fp); // cmdline is only one line long

		token = strtok(line, " ");
		while (token) {
			if (memcmp(token, CMDLINE_SERIALNO, CMDLINE_SERIALNO_LEN) == 0) {
				token += CMDLINE_SERIALNO_LEN;
				snprintf(device_id, DEVID_MAX, "%s", token);
				sanitize_device_id(device_id); // also removes newlines
				mConstValues.insert(make_pair("device_id", device_id));
				return;
			}
			token = strtok(NULL, " ");
		}
	}
#endif
	// Check cpuinfo for serial number; if found, use as device_id
	// If serial number is not found, fallback to hardware_id for the device_id
	fp = fopen("/proc/cpuinfo", "rt");
	if (fp != NULL) {
		while (fgets(line, sizeof(line), fp) != NULL) {
			if (memcmp(line, CPUINFO_SERIALNO, CPUINFO_SERIALNO_LEN) == 0) {
				// skip past "Serial", spaces, and colon
				token = line + CPUINFO_SERIALNO_LEN;
				while (*token && (!isgraph(*token) || *token == ':'))
					token++;

				if (*token && *token != '\n') {
					snprintf(device_id, DEVID_MAX, "%s", token);
					sanitize_device_id(device_id); // also removes newlines
					LOGINFO("=> serial from cpuinfo: '%s'\n", device_id);
					mConstValues.insert(make_pair("device_id", device_id));
					fclose(fp);
					return;
				}
			} else if (memcmp(line, CPUINFO_HARDWARE,
					CPUINFO_HARDWARE_LEN) == 0) {
				// skip past "Hardware", spaces, and colon
				token = line + CPUINFO_HARDWARE_LEN;
				while (*token && (!isgraph(*token) || *token == ':'))
					token++;

				if (*token && *token != '\n') {
					snprintf(hardware_id, HWID_MAX, "%s", token);
					if (hardware_id[strlen(hardware_id)-1] == '\n')
						hardware_id[strlen(hardware_id)-1] = 0;
					LOGINFO("=> hardware id from cpuinfo: '%s'\n", hardware_id);
				}
			}
		}
		fclose(fp);
	}

	if (hardware_id[0] != 0) {
		LOGINFO("\nusing hardware id for device id: '%s'\n", hardware_id);
		snprintf(device_id, DEVID_MAX, "%s", hardware_id);
		sanitize_device_id(device_id);
		mConstValues.insert(make_pair("device_id", device_id));
		return;
	}

	strcpy(device_id, "serialno");
	LOGINFO("=> device id not found, using '%s'\n", device_id);
	mConstValues.insert(make_pair("device_id", device_id));
	return;
}

int DataManager::ResetDefaults()
{
	pthread_mutex_lock(&m_valuesLock);
	mValues.clear();
	pthread_mutex_unlock(&m_valuesLock);

	mConstValues.clear();
	SetDefaultValues();
	return 0;
}

int DataManager::LoadValues(const string filename)
{
	string str, dev_id;

	if (!mInitialized)
		SetDefaultValues();

	GetValue("device_id", dev_id);
	// Save off the backing file for set operations
	mBackingFile = filename;

	// Read in the file, if possible
	FILE* in = fopen(filename.c_str(), "rb");
	if (!in) {
		LOGINFO("Settings file '%s' not found.\n", filename.c_str());
		return 0;
	} else {
		LOGINFO("Loading settings from '%s'.\n", filename.c_str());
	}

	int file_version;
	if (fread(&file_version, 1, sizeof(int), in) != sizeof(int))	goto error;
	if (file_version != FILE_VERSION)								goto error;

	while (!feof(in))
	{
		string Name;
		string Value;
		unsigned short length;
		char array[513];

		if (fread(&length, 1, sizeof(unsigned short), in) != sizeof(unsigned short))	goto error;
		if (length >= 512)																goto error;
		if (fread(array, 1, length, in) != length)										goto error;
		array[length+1] = '\0';
		Name = array;

		if (fread(&length, 1, sizeof(unsigned short), in) != sizeof(unsigned short))	goto error;
		if (length >= 512)																goto error;
		if (fread(array, 1, length, in) != length)										goto error;
		array[length+1] = '\0';
		Value = array;

		map<string, TStrIntPair>::iterator pos;

		pthread_mutex_lock(&m_valuesLock);

		pos = mValues.find(Name);
		if (pos != mValues.end())
		{
			pos->second.first = Value;
			pos->second.second = 1;
		}
		else
			mValues.insert(TNameValuePair(Name, TStrIntPair(Value, 1)));

		pthread_mutex_unlock(&m_valuesLock);

#ifndef TW_NO_SCREEN_TIMEOUT
		if (Name == "tw_screen_timeout_secs")
			blankTimer.setTime(atoi(Value.c_str()));
#endif
	}
error:
	fclose(in);
	string current = GetCurrentStoragePath();
	TWPartition* Part = PartitionManager.Find_Partition_By_Path(current);
	if(!Part)
		Part = PartitionManager.Get_Default_Storage_Partition();
	if (Part && current != Part->Storage_Path && Part->Mount(false)) {
		LOGINFO("LoadValues setting storage path to '%s'\n", Part->Storage_Path.c_str());
		SetValue("tw_storage_path", Part->Storage_Path);
	} else {
		SetBackupFolder();
	}
	return 0;
}

int DataManager::Flush()
{
	return SaveValues();
}

int DataManager::SaveValues()
{
#ifndef TW_OEM_BUILD
	if (mBackingFile.empty())
		return -1;

	string mount_path = GetSettingsStoragePath();
	PartitionManager.Mount_By_Path(mount_path.c_str(), 1);

	FILE* out = fopen(mBackingFile.c_str(), "wb");
	if (!out)
		return -1;

	int file_version = FILE_VERSION;
	fwrite(&file_version, 1, sizeof(int), out);

	pthread_mutex_lock(&m_valuesLock);

	map<string, TStrIntPair>::iterator iter;
	for (iter = mValues.begin(); iter != mValues.end(); ++iter)
	{
		// Save only the persisted data
		if (iter->second.second != 0)
		{
			unsigned short length = (unsigned short) iter->first.length() + 1;
			fwrite(&length, 1, sizeof(unsigned short), out);
			fwrite(iter->first.c_str(), 1, length, out);
			length = (unsigned short) iter->second.first.length() + 1;
			fwrite(&length, 1, sizeof(unsigned short), out);
			fwrite(iter->second.first.c_str(), 1, length, out);
		}
	}

	pthread_mutex_unlock(&m_valuesLock);

	fclose(out);
	tw_set_default_metadata(mBackingFile.c_str());
#endif // ifdef TW_OEM_BUILD
	return 0;
}

int DataManager::GetValue(const string varName, string& value)
{
	string localStr = varName;

	if (!mInitialized)
		SetDefaultValues();

	// Strip off leading and trailing '%' if provided
	if (localStr.length() > 2 && localStr[0] == '%' && localStr[localStr.length()-1] == '%')
	{
		localStr.erase(0, 1);
		localStr.erase(localStr.length() - 1, 1);
	}

	// Handle magic values
	if (GetMagicValue(localStr, value) == 0)
		return 0;

	// Handle property
	if (localStr.length() > 9 && localStr.substr(0, 9) == "property.") {
		char property_value[PROPERTY_VALUE_MAX];
		property_get(localStr.substr(9).c_str(), property_value, "");
		value = property_value;
		return 0;
	}

	map<string, string>::iterator constPos;
	constPos = mConstValues.find(localStr);
	if (constPos != mConstValues.end())
	{
		value = constPos->second;
		return 0;
	}

	pthread_mutex_lock(&m_valuesLock);
	map<string, TStrIntPair>::iterator pos;
	pos = mValues.find(localStr);
	if (pos == mValues.end()){
		pthread_mutex_unlock(&m_valuesLock);
		return -1;
	}

	value = pos->second.first;
	pthread_mutex_unlock(&m_valuesLock);
	return 0;
}

int DataManager::GetValue(const string varName, int& value)
{
	string data;

	if (GetValue(varName,data) != 0)
		return -1;

	value = atoi(data.c_str());
	return 0;
}

int DataManager::GetValue(const string varName, float& value)
{
	string data;

	if (GetValue(varName,data) != 0)
		return -1;

	value = atof(data.c_str());
	return 0;
}

unsigned long long DataManager::GetValue(const string varName, unsigned long long& value)
{
	string data;

	if (GetValue(varName,data) != 0)
		return -1;

	value = strtoull(data.c_str(), NULL, 10);
	return 0;
}

// This function will return an empty string if the value doesn't exist
string DataManager::GetStrValue(const string varName)
{
	string retVal;

	GetValue(varName, retVal);
	return retVal;
}

// This function will return 0 if the value doesn't exist
int DataManager::GetIntValue(const string varName)
{
	string retVal;

	GetValue(varName, retVal);
	return atoi(retVal.c_str());
}

int DataManager::SetValue(const string varName, string value, int persist /* = 0 */)
{
	if (!mInitialized)
		SetDefaultValues();

	// Handle property
	if (varName.length() > 9 && varName.substr(0, 9) == "property.") {
		int ret = property_set(varName.substr(9).c_str(), value.c_str());
		if (ret)
			LOGERR("Error setting property '%s' to '%s'\n", varName.substr(9).c_str(), value.c_str());
		return ret;
	}

	// Don't allow empty values or numerical starting values
	if (varName.empty() || (varName[0] >= '0' && varName[0] <= '9'))
		return -1;

	map<string, string>::iterator constChk;
	constChk = mConstValues.find(varName);
	if (constChk != mConstValues.end())
		return -1;

	pthread_mutex_lock(&m_valuesLock);

	map<string, TStrIntPair>::iterator pos;
	pos = mValues.find(varName);
	if (pos == mValues.end())
		pos = (mValues.insert(TNameValuePair(varName, TStrIntPair(value, persist)))).first;
	else
		pos->second.first = value;

	if (pos->second.second != 0)
		SaveValues();

	pthread_mutex_unlock(&m_valuesLock);

#ifndef TW_NO_SCREEN_TIMEOUT
	if (varName == "tw_screen_timeout_secs") {
		blankTimer.setTime(atoi(value.c_str()));
	} else
#endif
	if (varName == "tw_storage_path") {
		SetBackupFolder();
	}
	gui_notifyVarChange(varName.c_str(), value.c_str());
	return 0;
}

int DataManager::SetValue(const string varName, int value, int persist /* = 0 */)
{
	ostringstream valStr;
	valStr << value;
	if (varName == "tw_use_external_storage") {
		string str;

		if (GetIntValue(TW_HAS_INTERNAL) == 1)
			str = GetStrValue(TW_INTERNAL_PATH);
		else
			str = GetStrValue(TW_EXTERNAL_PATH);

		SetValue("tw_storage_path", str);
	}
	return SetValue(varName, valStr.str(), persist);
}

int DataManager::SetValue(const string varName, float value, int persist /* = 0 */)
{
	ostringstream valStr;
	valStr << value;
	return SetValue(varName, valStr.str(), persist);;
}

int DataManager::SetValue(const string varName, unsigned long long value, int persist /* = 0 */)
{
	ostringstream valStr;
	valStr << value;
	return SetValue(varName, valStr.str(), persist);
}

int DataManager::SetProgress(float Fraction) {
	return SetValue("ui_progress", (float) (Fraction * 100.0));
}

int DataManager::ShowProgress(float Portion, float Seconds)
{
	float Starting_Portion;
	GetValue("ui_progress_portion", Starting_Portion);
	if (SetValue("ui_progress_portion", (float)(Portion * 100.0) + Starting_Portion) != 0)
		return -1;
	if (SetValue("ui_progress_frames", Seconds * 30) != 0)
		return -1;
	return 0;
}

void DataManager::DumpValues()
{
	map<string, TStrIntPair>::iterator iter;
	gui_print("Data Manager dump - Values with leading X are persisted.\n");
	pthread_mutex_lock(&m_valuesLock);
	for (iter = mValues.begin(); iter != mValues.end(); ++iter)
		gui_print("%c %s=%s\n", iter->second.second ? 'X' : ' ', iter->first.c_str(), iter->second.first.c_str());
	pthread_mutex_unlock(&m_valuesLock);
}

void DataManager::update_tz_environment_variables(void)
{
	setenv("TZ", GetStrValue(TW_TIME_ZONE_VAR).c_str(), 1);
	tzset();
}

void DataManager::SetBackupFolder()
{
	string str = GetCurrentStoragePath();
	TWPartition* partition = PartitionManager.Find_Partition_By_Path(str);
	str += "/TWRP/BACKUPS/";

	string dev_id;
	GetValue("device_id", dev_id);

	str += dev_id;
	LOGINFO("Backup folder set to '%s'\n", str.c_str());
	SetValue(TW_BACKUPS_FOLDER_VAR, str, 0);
	if (partition != NULL) {
		SetValue("tw_storage_display_name", partition->Storage_Name);
		char free_space[255];
		sprintf(free_space, "%llu", partition->Free / 1024 / 1024);
		SetValue("tw_storage_free_size", free_space);
		string zip_path, zip_root, storage_path;
		GetValue(TW_ZIP_LOCATION_VAR, zip_path);
		if (partition->Has_Data_Media)
			storage_path = partition->Symlink_Mount_Point;
		else
			storage_path = partition->Storage_Path;
		if (zip_path.size() < storage_path.size()) {
			SetValue(TW_ZIP_LOCATION_VAR, storage_path);
		} else {
			zip_root = TWFunc::Get_Root_Path(zip_path);
			if (zip_root != storage_path) {
				LOGINFO("DataManager::SetBackupFolder zip path was %s changing to %s, %s\n", zip_path.c_str(), storage_path.c_str(), zip_root.c_str());
				SetValue(TW_ZIP_LOCATION_VAR, storage_path);
			}
		}
	} else {
		if (PartitionManager.Fstab_Processed() != 0) {
			LOGINFO("Storage partition '%s' not found\n", str.c_str());
			gui_err("unable_locate_storage=Unable to locate storage device.");
		}
	}
}

void DataManager::SetDefaultValues()
{
	string str, path;

	get_device_id();

	pthread_mutex_lock(&m_valuesLock);

	mInitialized = 1;

	mConstValues.insert(make_pair("true", "1"));
	mConstValues.insert(make_pair("false", "0"));

	mConstValues.insert(make_pair(TW_VERSION_VAR, TW_VERSION_STR));
	mValues.insert(make_pair("tw_button_vibrate", make_pair("80", 1)));
	mValues.insert(make_pair("tw_keyboard_vibrate", make_pair("40", 1)));
	mValues.insert(make_pair("tw_action_vibrate", make_pair("160", 1)));

	TWPartition *store = PartitionManager.Get_Default_Storage_Partition();
	if(store)
		mValues.insert(make_pair("tw_storage_path", make_pair(store->Storage_Path.c_str(), 1)));
	else
		mValues.insert(make_pair("tw_storage_path", make_pair("/", 1)));

#ifdef TW_FORCE_CPUINFO_FOR_DEVICE_ID
	printf("TW_FORCE_CPUINFO_FOR_DEVICE_ID := true\n");
#endif

#ifdef BOARD_HAS_NO_REAL_SDCARD
	printf("BOARD_HAS_NO_REAL_SDCARD := true\n");
	mConstValues.insert(make_pair(TW_ALLOW_PARTITION_SDCARD, "0"));
#else
	mConstValues.insert(make_pair(TW_ALLOW_PARTITION_SDCARD, "1"));
#endif

#ifdef TW_INCLUDE_DUMLOCK
	printf("TW_INCLUDE_DUMLOCK := true\n");
	mConstValues.insert(make_pair(TW_SHOW_DUMLOCK, "1"));
#else
	mConstValues.insert(make_pair(TW_SHOW_DUMLOCK, "0"));
#endif

	str = GetCurrentStoragePath();
	SetValue(TW_ZIP_LOCATION_VAR, str.c_str(), 1);
	str += "/TWRP/BACKUPS/";

	string dev_id;
	GetValue("device_id", dev_id);

	str += dev_id;
	SetValue(TW_BACKUPS_FOLDER_VAR, str, 0);

	mConstValues.insert(make_pair(TW_REBOOT_SYSTEM, "1"));
#ifdef TW_NO_REBOOT_RECOVERY
	printf("TW_NO_REBOOT_RECOVERY := true\n");
	mConstValues.insert(make_pair(TW_REBOOT_RECOVERY, "0"));
#else
	mConstValues.insert(make_pair(TW_REBOOT_RECOVERY, "1"));
#endif
	mConstValues.insert(make_pair(TW_REBOOT_POWEROFF, "1"));
#ifdef TW_NO_REBOOT_BOOTLOADER
	printf("TW_NO_REBOOT_BOOTLOADER := true\n");
	mConstValues.insert(make_pair(TW_REBOOT_BOOTLOADER, "0"));
#else
	mConstValues.insert(make_pair(TW_REBOOT_BOOTLOADER, "1"));
#endif
#ifdef RECOVERY_SDCARD_ON_DATA
	printf("RECOVERY_SDCARD_ON_DATA := true\n");
	mConstValues.insert(make_pair(TW_HAS_DATA_MEDIA, "1"));
	mConstValues.insert(make_pair("tw_has_internal", "1"));
	datamedia = true;
#else
	mValues.insert(make_pair(TW_HAS_DATA_MEDIA, make_pair("0", 0)));
	mValues.insert(make_pair("tw_has_internal", make_pair("0", 0)));
#endif
#ifdef TW_NO_BATT_PERCENT
	printf("TW_NO_BATT_PERCENT := true\n");
	mConstValues.insert(make_pair(TW_NO_BATTERY_PERCENT, "1"));
#else
	mConstValues.insert(make_pair(TW_NO_BATTERY_PERCENT, "0"));
#endif
#ifdef TW_NO_CPU_TEMP
	printf("TW_NO_CPU_TEMP := true\n");
	mConstValues.insert(make_pair("tw_no_cpu_temp", "1"));
#else
	string cpu_temp_file;
#ifdef TW_CUSTOM_CPU_TEMP_PATH
	cpu_temp_file = EXPAND(TW_CUSTOM_CPU_TEMP_PATH);
#else
	cpu_temp_file = "/sys/class/thermal/thermal_zone0/temp";
#endif
	if (TWFunc::Path_Exists(cpu_temp_file)) {
		mConstValues.insert(make_pair("tw_no_cpu_temp", "0"));
	} else {
		LOGINFO("CPU temperature file '%s' not found, disabling CPU temp.\n", cpu_temp_file.c_str());
		mConstValues.insert(make_pair("tw_no_cpu_temp", "1"));
	}
#endif
#ifdef TW_CUSTOM_POWER_BUTTON
	printf("TW_POWER_BUTTON := %s\n", EXPAND(TW_CUSTOM_POWER_BUTTON));
	mConstValues.insert(make_pair(TW_POWER_BUTTON, EXPAND(TW_CUSTOM_POWER_BUTTON)));
#else
	mConstValues.insert(make_pair(TW_POWER_BUTTON, "0"));
#endif
#ifdef TW_ALWAYS_RMRF
	printf("TW_ALWAYS_RMRF := true\n");
	mConstValues.insert(make_pair(TW_RM_RF_VAR, "1"));
#endif
#ifdef TW_NEVER_UNMOUNT_SYSTEM
	printf("TW_NEVER_UNMOUNT_SYSTEM := true\n");
	mConstValues.insert(make_pair(TW_DONT_UNMOUNT_SYSTEM, "1"));
#else
	mConstValues.insert(make_pair(TW_DONT_UNMOUNT_SYSTEM, "0"));
#endif
#ifdef TW_NO_USB_STORAGE
	printf("TW_NO_USB_STORAGE := true\n");
	mConstValues.insert(make_pair(TW_HAS_USB_STORAGE, "0"));
#else
	char lun_file[255];
	string Lun_File_str = CUSTOM_LUN_FILE;
	size_t found = Lun_File_str.find("%");
	if (found != string::npos) {
		sprintf(lun_file, CUSTOM_LUN_FILE, 0);
		Lun_File_str = lun_file;
	}
	if (!TWFunc::Path_Exists(Lun_File_str)) {
		LOGINFO("Lun file '%s' does not exist, USB storage mode disabled\n", Lun_File_str.c_str());
		mConstValues.insert(make_pair(TW_HAS_USB_STORAGE, "0"));
	} else {
		LOGINFO("Lun file '%s'\n", Lun_File_str.c_str());
		mConstValues.insert(make_pair(TW_HAS_USB_STORAGE, "1"));
	}
#endif
#ifdef TW_INCLUDE_INJECTTWRP
	printf("TW_INCLUDE_INJECTTWRP := true\n");
	mConstValues.insert(make_pair(TW_HAS_INJECTTWRP, "1"));
	mValues.insert(make_pair(TW_INJECT_AFTER_ZIP, make_pair("1", 1)));
#else
	mConstValues.insert(make_pair(TW_HAS_INJECTTWRP, "0"));
	mValues.insert(make_pair(TW_INJECT_AFTER_ZIP, make_pair("0", 1)));
#endif
#ifdef TW_HAS_DOWNLOAD_MODE
	printf("TW_HAS_DOWNLOAD_MODE := true\n");
	mConstValues.insert(make_pair(TW_DOWNLOAD_MODE, "1"));
#endif
#ifdef TW_INCLUDE_CRYPTO
	mConstValues.insert(make_pair(TW_HAS_CRYPTO, "1"));
	printf("TW_INCLUDE_CRYPTO := true\n");
#endif
#ifdef TW_SDEXT_NO_EXT4
	printf("TW_SDEXT_NO_EXT4 := true\n");
	mConstValues.insert(make_pair(TW_SDEXT_DISABLE_EXT4, "1"));
#else
	mConstValues.insert(make_pair(TW_SDEXT_DISABLE_EXT4, "0"));
#endif

#ifdef TW_HAS_NO_BOOT_PARTITION
	mValues.insert(make_pair("tw_backup_list", make_pair("/system;/data;", 1)));
#else
	mValues.insert(make_pair("tw_backup_list", make_pair("/system;/data;/boot;", 1)));
#endif
	mConstValues.insert(make_pair(TW_MIN_SYSTEM_VAR, TW_MIN_SYSTEM_SIZE));
	mValues.insert(make_pair(TW_BACKUP_NAME, make_pair("(Auto Generate)", 0)));

	mValues.insert(make_pair(TW_REBOOT_AFTER_FLASH_VAR, make_pair("0", 1)));
	mValues.insert(make_pair(TW_SIGNED_ZIP_VERIFY_VAR, make_pair("0", 1)));
	mValues.insert(make_pair(TW_FORCE_MD5_CHECK_VAR, make_pair("0", 1)));
	mValues.insert(make_pair(TW_COLOR_THEME_VAR, make_pair("0", 1)));
	mValues.insert(make_pair(TW_USE_COMPRESSION_VAR, make_pair("0", 1)));
	mValues.insert(make_pair(TW_SHOW_SPAM_VAR, make_pair("0", 1)));
	mValues.insert(make_pair(TW_TIME_ZONE_VAR, make_pair("CST6CDT,M3.2.0,M11.1.0", 1)));
	mValues.insert(make_pair(TW_SORT_FILES_BY_DATE_VAR, make_pair("0", 1)));
	mValues.insert(make_pair(TW_GUI_SORT_ORDER, make_pair("1", 1)));
	mValues.insert(make_pair(TW_RM_RF_VAR, make_pair("0", 1)));
	mValues.insert(make_pair(TW_SKIP_MD5_CHECK_VAR, make_pair("0", 1)));
	mValues.insert(make_pair(TW_SKIP_MD5_GENERATE_VAR, make_pair("0", 1)));
	mValues.insert(make_pair(TW_SDEXT_SIZE, make_pair("0", 1)));
	mValues.insert(make_pair(TW_SWAP_SIZE, make_pair("0", 1)));
	mValues.insert(make_pair(TW_SDPART_FILE_SYSTEM, make_pair("ext3", 1)));
	mValues.insert(make_pair(TW_TIME_ZONE_GUISEL, make_pair("CST6;CDT,M3.2.0,M11.1.0", 1)));
	mValues.insert(make_pair(TW_TIME_ZONE_GUIOFFSET, make_pair("0", 1)));
	mValues.insert(make_pair(TW_TIME_ZONE_GUIDST, make_pair("1", 1)));
	mValues.insert(make_pair(TW_ACTION_BUSY, make_pair("0", 0)));
	mValues.insert(make_pair("tw_wipe_cache", make_pair("0", 0)));
	mValues.insert(make_pair("tw_wipe_dalvik", make_pair("0", 0)));
	if (GetIntValue(TW_HAS_INTERNAL) == 1 && GetIntValue(TW_HAS_DATA_MEDIA) == 1 && GetIntValue(TW_HAS_EXTERNAL) == 0)
		SetValue(TW_HAS_USB_STORAGE, 0, 0);
	else
		SetValue(TW_HAS_USB_STORAGE, 1, 0);
	mValues.insert(make_pair(TW_ZIP_INDEX, make_pair("0", 0)));
	mValues.insert(make_pair(TW_ZIP_QUEUE_COUNT, make_pair("0", 0)));
	mValues.insert(make_pair(TW_FILENAME, make_pair("/sdcard", 0)));
	mValues.insert(make_pair(TW_SIMULATE_ACTIONS, make_pair("0", 1)));
	mValues.insert(make_pair(TW_SIMULATE_FAIL, make_pair("0", 1)));
	mValues.insert(make_pair(TW_IS_ENCRYPTED, make_pair("0", 0)));
	mValues.insert(make_pair(TW_IS_DECRYPTED, make_pair("0", 0)));
	mValues.insert(make_pair(TW_CRYPTO_PASSWORD, make_pair("0", 0)));
	mValues.insert(make_pair(TW_DATA_BLK_DEVICE, make_pair("0", 0)));
	mValues.insert(make_pair("tw_terminal_state", make_pair("0", 0)));
	mValues.insert(make_pair("tw_background_thread_running", make_pair("0", 0)));
	mValues.insert(make_pair(TW_RESTORE_FILE_DATE, make_pair("0", 0)));
	mValues.insert(make_pair("tw_military_time", make_pair("0", 1)));
#ifdef TW_NO_SCREEN_TIMEOUT
	mValues.insert(make_pair("tw_screen_timeout_secs", make_pair("0", 1)));
	mValues.insert(make_pair("tw_no_screen_timeout", make_pair("1", 1)));
#else
	mValues.insert(make_pair("tw_screen_timeout_secs", make_pair("60", 1)));
	mValues.insert(make_pair("tw_no_screen_timeout", make_pair("0", 1)));
#endif
	mValues.insert(make_pair("tw_gui_done", make_pair("0", 0)));
	mValues.insert(make_pair("tw_encrypt_backup", make_pair("0", 0)));
	string findbright;
#ifdef TW_BRIGHTNESS_PATH
	findbright = EXPAND(TW_BRIGHTNESS_PATH);
	LOGINFO("TW_BRIGHTNESS_PATH := %s\n", findbright.c_str());
	if (!TWFunc::Path_Exists(findbright)) {
		LOGINFO("Specified brightness file '%s' not found.\n", findbright.c_str());
		findbright = "";
	}
#endif
	if (findbright.empty()) {
		// Attempt to locate the brightness file
		findbright = Find_File::Find("brightness", "/sys/class/backlight");
		if (findbright.empty()) findbright = Find_File::Find("brightness", "/sys/class/leds/lcd-backlight");
	}
	if (findbright.empty()) {
		LOGINFO("Unable to locate brightness file\n");
		mConstValues.insert(make_pair("tw_has_brightnesss_file", "0"));
	} else {
		LOGINFO("Found brightness file at '%s'\n", findbright.c_str());
		mConstValues.insert(make_pair("tw_has_brightnesss_file", "1"));
		mConstValues.insert(make_pair("tw_brightness_file", findbright));
		string maxBrightness;
#ifdef TW_MAX_BRIGHTNESS
		ostringstream maxVal;
		maxVal << TW_MAX_BRIGHTNESS;
		maxBrightness = maxVal.str();
#else
		// Attempt to locate the max_brightness file
		string maxbrightpath = findbright.insert(findbright.rfind('/') + 1, "max_");
		if (TWFunc::Path_Exists(maxbrightpath)) {
			ifstream maxVal(maxbrightpath.c_str());
			if(maxVal >> maxBrightness) {
				LOGINFO("Got max brightness %s from '%s'\n", maxBrightness.c_str(), maxbrightpath.c_str());
			} else {
				// Something went wrong, set that to indicate error
				maxBrightness = "-1";
			}
		}
		if (atoi(maxBrightness.c_str()) <= 0)
		{
			// Fallback into default
			ostringstream maxVal;
			maxVal << 255;
			maxBrightness = maxVal.str();
		}
#endif
		mConstValues.insert(make_pair("tw_brightness_max", maxBrightness));
		mValues.insert(make_pair("tw_brightness", make_pair(maxBrightness, 1)));
		mValues.insert(make_pair("tw_brightness_pct", make_pair("100", 1)));
#ifdef TW_SECONDARY_BRIGHTNESS_PATH
		string secondfindbright = EXPAND(TW_SECONDARY_BRIGHTNESS_PATH);
		if (secondfindbright != "" && TWFunc::Path_Exists(secondfindbright)) {
			LOGINFO("Will use a second brightness file at '%s'\n", secondfindbright.c_str());
			mConstValues.insert(make_pair("tw_secondary_brightness_file", secondfindbright));
		} else {
			LOGINFO("Specified secondary brightness file '%s' not found.\n", secondfindbright.c_str());
		}
#endif
#ifdef TW_DEFAULT_BRIGHTNESS
		int defValInt = TW_DEFAULT_BRIGHTNESS;
		int maxValInt = atoi(maxBrightness.c_str());
		// Deliberately int so the % is always a whole number
		int defPctInt = ( ( (double)defValInt / maxValInt ) * 100 );
		ostringstream defPct;
		defPct << defPctInt;
		mValues.erase("tw_brightness_pct");
		mValues.insert(make_pair("tw_brightness_pct", make_pair(defPct.str(), 1)));

		ostringstream defVal;
		defVal << TW_DEFAULT_BRIGHTNESS;
		mValues.erase("tw_brightness");
		mValues.insert(make_pair("tw_brightness", make_pair(defVal.str(), 1)));
		TWFunc::Set_Brightness(defVal.str());
#else
		TWFunc::Set_Brightness(maxBrightness);
#endif
	}
	mValues.insert(make_pair(TW_MILITARY_TIME, make_pair("0", 1)));
#ifndef TW_EXCLUDE_ENCRYPTED_BACKUPS
	mValues.insert(make_pair("tw_include_encrypted_backup", make_pair("1", 0)));
#else
	LOGINFO("TW_EXCLUDE_ENCRYPTED_BACKUPS := true\n");
	mValues.insert(make_pair("tw_include_encrypted_backup", make_pair("0", 0)));
#endif
#ifdef TW_HAS_MTP
	mConstValues.insert(make_pair("tw_has_mtp", "1"));
	mValues.insert(make_pair("tw_mtp_enabled", make_pair("1", 1)));
	mValues.insert(make_pair("tw_mtp_debug", make_pair("0", 1)));
#else
	LOGINFO("TW_EXCLUDE_MTP := true\n");
	mConstValues.insert(make_pair("tw_has_mtp", "0"));
	mConstValues.insert(make_pair("tw_mtp_enabled", "0"));
#endif
	mValues.insert(make_pair("tw_mount_system_ro", make_pair("2", 1)));
	mValues.insert(make_pair("tw_never_show_system_ro_page", make_pair("0", 1)));
	mValues.insert(make_pair("tw_language", make_pair(EXPAND(TW_DEFAULT_LANGUAGE), 1)));
	LOGINFO("LANG: %s\n", EXPAND(TW_DEFAULT_LANGUAGE));

	pthread_mutex_unlock(&m_valuesLock);
}

// Magic Values
int DataManager::GetMagicValue(const string varName, string& value)
{
	// Handle special dynamic cases
	if (varName == "tw_time")
	{
		char tmp[32];

		struct tm *current;
		time_t now;
		int tw_military_time;
		now = time(0);
		current = localtime(&now);
		GetValue(TW_MILITARY_TIME, tw_military_time);
		if (current->tm_hour >= 12)
		{
			if (tw_military_time == 1)
				sprintf(tmp, "%d:%02d", current->tm_hour, current->tm_min);
			else
				sprintf(tmp, "%d:%02d PM", current->tm_hour == 12 ? 12 : current->tm_hour - 12, current->tm_min);
		}
		else
		{
			if (tw_military_time == 1)
				sprintf(tmp, "%d:%02d", current->tm_hour, current->tm_min);
			else
				sprintf(tmp, "%d:%02d AM", current->tm_hour == 0 ? 12 : current->tm_hour, current->tm_min);
		}
		value = tmp;
		return 0;
	}
	else if (varName == "tw_cpu_temp")
	{
	   int tw_no_cpu_temp;
	   GetValue("tw_no_cpu_temp", tw_no_cpu_temp);
	   if (tw_no_cpu_temp == 1) return -1;

	   string cpu_temp_file;
	   static unsigned long convert_temp = 0;
	   static time_t cpuSecCheck = 0;
	   int divisor = 0;
	   struct timeval curTime;
	   string results;

	   gettimeofday(&curTime, NULL);
	   if (curTime.tv_sec > cpuSecCheck)
	   {
#ifdef TW_CUSTOM_CPU_TEMP_PATH
		   cpu_temp_file = EXPAND(TW_CUSTOM_CPU_TEMP_PATH);
		   if (TWFunc::read_file(cpu_temp_file, results) != 0)
			return -1;
#else
		   cpu_temp_file = "/sys/class/thermal/thermal_zone0/temp";
		   if (TWFunc::read_file(cpu_temp_file, results) != 0)
			return -1;
#endif
		   convert_temp = strtoul(results.c_str(), NULL, 0) / 1000;
		   if (convert_temp <= 0)
			convert_temp = strtoul(results.c_str(), NULL, 0);
		   if (convert_temp >= 150)
			convert_temp = strtoul(results.c_str(), NULL, 0) / 10;
		   cpuSecCheck = curTime.tv_sec + 5;
	   }
	   value = TWFunc::to_string(convert_temp);
	   return 0;
	}
	else if (varName == "tw_battery")
	{
		char tmp[16];
		static char charging = ' ';
		static int lastVal = -1;
		static time_t nextSecCheck = 0;
		struct timeval curTime;
		gettimeofday(&curTime, NULL);
		if (curTime.tv_sec > nextSecCheck)
		{
			char cap_s[4];
#ifdef TW_CUSTOM_BATTERY_PATH
			string capacity_file = EXPAND(TW_CUSTOM_BATTERY_PATH);
			capacity_file += "/capacity";
			FILE * cap = fopen(capacity_file.c_str(),"rt");
#else
			FILE * cap = fopen("/sys/class/power_supply/battery/capacity","rt");
#endif
			if (cap){
				fgets(cap_s, 4, cap);
				fclose(cap);
				lastVal = atoi(cap_s);
				if (lastVal > 100)	lastVal = 101;
				if (lastVal < 0)	lastVal = 0;
			}
#ifdef TW_CUSTOM_BATTERY_PATH
			string status_file = EXPAND(TW_CUSTOM_BATTERY_PATH);
			status_file += "/status";
			cap = fopen(status_file.c_str(),"rt");
#else
			cap = fopen("/sys/class/power_supply/battery/status","rt");
#endif
			if (cap) {
				fgets(cap_s, 2, cap);
				fclose(cap);
				if (cap_s[0] == 'C')
					charging = '+';
				else
					charging = ' ';
			}
			nextSecCheck = curTime.tv_sec + 60;
		}

		sprintf(tmp, "%i%%%c", lastVal, charging);
		value = tmp;
		return 0;
	}
	return -1;
}

void DataManager::Output_Version(void)
{
#ifndef TW_OEM_BUILD
	string Path;
	char version[255];

	if (!PartitionManager.Mount_By_Path("/cache", false)) {
		LOGINFO("Unable to mount '%s' to write version number.\n", Path.c_str());
		return;
	}
	if (!TWFunc::Path_Exists("/cache/recovery/.")) {
		LOGINFO("Recreating /cache/recovery folder.\n");
		if (mkdir("/cache/recovery", S_IRWXU | S_IRWXG | S_IWGRP | S_IXGRP) != 0) {
			LOGERR("DataManager::Output_Version -- Unable to make /cache/recovery\n");
			return;
		}
	}
	Path = "/cache/recovery/.version";
	if (TWFunc::Path_Exists(Path)) {
		unlink(Path.c_str());
	}
	FILE *fp = fopen(Path.c_str(), "w");
	if (fp == NULL) {
		gui_msg(Msg(msg::kError, "error_opening_strerr=Error opening: '{1}' ({2})")(Path)(strerror(errno)));
		return;
	}
	strcpy(version, TW_VERSION_STR);
	fwrite(version, sizeof(version[0]), strlen(version) / sizeof(version[0]), fp);
	fclose(fp);
	TWFunc::copy_file("/etc/recovery.fstab", "/cache/recovery/recovery.fstab", 0644);
	PartitionManager.Output_Storage_Fstab();
	sync();
	LOGINFO("Version number saved to '%s'\n", Path.c_str());
#endif
}

void DataManager::ReadSettingsFile(void)
{
#ifndef TW_OEM_BUILD
	// Load up the values for TWRP - Sleep to let the card be ready
	char mkdir_path[255], settings_file[255];
	int is_enc, has_dual, use_ext, has_data_media, has_ext;

	GetValue(TW_IS_ENCRYPTED, is_enc);
	GetValue(TW_HAS_DATA_MEDIA, has_data_media);
	if (is_enc == 1 && has_data_media == 1) {
		LOGINFO("Cannot load settings -- encrypted.\n");
		return;
	}

	memset(mkdir_path, 0, sizeof(mkdir_path));
	memset(settings_file, 0, sizeof(settings_file));
	sprintf(mkdir_path, "%s/TWRP", GetSettingsStoragePath().c_str());
	sprintf(settings_file, "%s/.twrps", mkdir_path);

	if (!PartitionManager.Mount_Settings_Storage(false))
	{
		usleep(500000);
		if (!PartitionManager.Mount_Settings_Storage(false))
			gui_msg(Msg(msg::kError, "unable_to_mount=Unable to mount {1}")(settings_file));
	}

	mkdir(mkdir_path, 0777);

	LOGINFO("Attempt to load settings from settings file...\n");
	LoadValues(settings_file);
	Output_Version();
#endif // ifdef TW_OEM_BUILD
	PartitionManager.Mount_All_Storage();
	update_tz_environment_variables();
#ifdef TW_MAX_BRIGHTNESS
	TWFunc::Set_Brightness(GetStrValue("tw_brightness"));
#endif
}

string DataManager::GetCurrentStoragePath(void)
{
	return GetStrValue("tw_storage_path");
}

string DataManager::GetSettingsStoragePath(void)
{
	return GetStrValue("tw_settings_path");
}

void DataManager::Vibrate(const string varName)
{
	int vib_value = 0;
	GetValue(varName, vib_value);
	if (vib_value) {
		vibrate(vib_value);
	}
}
