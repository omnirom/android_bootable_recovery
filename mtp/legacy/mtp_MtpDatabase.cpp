/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *	  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Copyright (C) 2014 TeamWin - bigbiff and Dees_Troy mtp database conversion to C++
 */

#include <utils/Log.h>

#include <stdio.h>
#include <assert.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string>
#include <map>
#include <libgen.h>
#include <cutils/properties.h>

#include "MtpDatabase.h"
#include "MtpStorage.h"
#include "MtpDataPacket.h"
#include "MtpObjectInfo.h"
#include "MtpProperty.h"
#include "MtpDebug.h"
#include "MtpStringBuffer.h"
#include "MtpUtils.h"
#include "mtp.h"
#include "mtp_MtpDatabase.hpp"
//#include "btree.hpp"

MyMtpDatabase::MyMtpDatabase()
{
	storagenum = 0;
	count = -1;
}

MyMtpDatabase::~MyMtpDatabase() {
	std::map<int, MtpStorage*>::iterator i;
	for (i = storagemap.begin(); i != storagemap.end(); i++) {
		delete i->second;
	}
}

int MyMtpDatabase::DEVICE_PROPERTIES[3] = {
	MTP_DEVICE_PROPERTY_SYNCHRONIZATION_PARTNER,
	MTP_DEVICE_PROPERTY_DEVICE_FRIENDLY_NAME,
	MTP_DEVICE_PROPERTY_IMAGE_SIZE
};

int MyMtpDatabase::FILE_PROPERTIES[10] = {
	// NOTE must match beginning of AUDIO_PROPERTIES, VIDEO_PROPERTIES
	// and IMAGE_PROPERTIES below
	MTP_PROPERTY_STORAGE_ID,
	MTP_PROPERTY_OBJECT_FORMAT,
	MTP_PROPERTY_PROTECTION_STATUS,
	MTP_PROPERTY_OBJECT_SIZE,
	MTP_PROPERTY_OBJECT_FILE_NAME,
	MTP_PROPERTY_DATE_MODIFIED,
	MTP_PROPERTY_PARENT_OBJECT,
	MTP_PROPERTY_PERSISTENT_UID,
	MTP_PROPERTY_NAME,
	// TODO: why is DISPLAY_NAME not here?
	MTP_PROPERTY_DATE_ADDED
};

int MyMtpDatabase::AUDIO_PROPERTIES[19] = {
	// NOTE must match FILE_PROPERTIES above
	MTP_PROPERTY_STORAGE_ID,
	MTP_PROPERTY_OBJECT_FORMAT,
	MTP_PROPERTY_PROTECTION_STATUS,
	MTP_PROPERTY_OBJECT_SIZE,
	MTP_PROPERTY_OBJECT_FILE_NAME,
	MTP_PROPERTY_DATE_MODIFIED,
	MTP_PROPERTY_PARENT_OBJECT,
	MTP_PROPERTY_PERSISTENT_UID,
	MTP_PROPERTY_NAME,
	MTP_PROPERTY_DISPLAY_NAME,
	MTP_PROPERTY_DATE_ADDED,

	// audio specific properties
	MTP_PROPERTY_ARTIST,
	MTP_PROPERTY_ALBUM_NAME,
	MTP_PROPERTY_ALBUM_ARTIST,
	MTP_PROPERTY_TRACK,
	MTP_PROPERTY_ORIGINAL_RELEASE_DATE,
	MTP_PROPERTY_DURATION,
	MTP_PROPERTY_GENRE,
	MTP_PROPERTY_COMPOSER
};

int MyMtpDatabase::VIDEO_PROPERTIES[15] = {
	// NOTE must match FILE_PROPERTIES above
	MTP_PROPERTY_STORAGE_ID,
	MTP_PROPERTY_OBJECT_FORMAT,
	MTP_PROPERTY_PROTECTION_STATUS,
	MTP_PROPERTY_OBJECT_SIZE,
	MTP_PROPERTY_OBJECT_FILE_NAME,
	MTP_PROPERTY_DATE_MODIFIED,
	MTP_PROPERTY_PARENT_OBJECT,
	MTP_PROPERTY_PERSISTENT_UID,
	MTP_PROPERTY_NAME,
	MTP_PROPERTY_DISPLAY_NAME,
	MTP_PROPERTY_DATE_ADDED,

	// video specific properties
	MTP_PROPERTY_ARTIST,
	MTP_PROPERTY_ALBUM_NAME,
	MTP_PROPERTY_DURATION,
	MTP_PROPERTY_DESCRIPTION
};

int MyMtpDatabase::IMAGE_PROPERTIES[12] = {
	// NOTE must match FILE_PROPERTIES above
	MTP_PROPERTY_STORAGE_ID,
	MTP_PROPERTY_OBJECT_FORMAT,
	MTP_PROPERTY_PROTECTION_STATUS,
	MTP_PROPERTY_OBJECT_SIZE,
	MTP_PROPERTY_OBJECT_FILE_NAME,
	MTP_PROPERTY_DATE_MODIFIED,
	MTP_PROPERTY_PARENT_OBJECT,
	MTP_PROPERTY_PERSISTENT_UID,
	MTP_PROPERTY_NAME,
	MTP_PROPERTY_DISPLAY_NAME,
	MTP_PROPERTY_DATE_ADDED,

	// image specific properties
	MTP_PROPERTY_DESCRIPTION
};

int MyMtpDatabase::ALL_PROPERTIES[25] = {
	// NOTE must match FILE_PROPERTIES above
	MTP_PROPERTY_STORAGE_ID,
	MTP_PROPERTY_OBJECT_FORMAT,
	MTP_PROPERTY_PROTECTION_STATUS,
	MTP_PROPERTY_OBJECT_SIZE,
	MTP_PROPERTY_OBJECT_FILE_NAME,
	MTP_PROPERTY_DATE_MODIFIED,
	MTP_PROPERTY_PARENT_OBJECT,
	MTP_PROPERTY_PERSISTENT_UID,
	MTP_PROPERTY_NAME,
	MTP_PROPERTY_DISPLAY_NAME,
	MTP_PROPERTY_DATE_ADDED,

	// image specific properties
	MTP_PROPERTY_DESCRIPTION,

	// audio specific properties
	MTP_PROPERTY_ARTIST,
	MTP_PROPERTY_ALBUM_NAME,
	MTP_PROPERTY_ALBUM_ARTIST,
	MTP_PROPERTY_TRACK,
	MTP_PROPERTY_ORIGINAL_RELEASE_DATE,
	MTP_PROPERTY_DURATION,
	MTP_PROPERTY_GENRE,
	MTP_PROPERTY_COMPOSER,

	// video specific properties
	MTP_PROPERTY_ARTIST,
	MTP_PROPERTY_ALBUM_NAME,
	MTP_PROPERTY_DURATION,
	MTP_PROPERTY_DESCRIPTION,

	// image specific properties
	MTP_PROPERTY_DESCRIPTION
};

int MyMtpDatabase::SUPPORTED_PLAYBACK_FORMATS[26] = {
	SUPPORTED_PLAYBACK_FORMAT_UNDEFINED,
	SUPPORTED_PLAYBACK_FORMAT_ASSOCIATION,
	SUPPORTED_PLAYBACK_FORMAT_TEXT,
	SUPPORTED_PLAYBACK_FORMAT_HTML,
	SUPPORTED_PLAYBACK_FORMAT_WAV,
	SUPPORTED_PLAYBACK_FORMAT_MP3,
	SUPPORTED_PLAYBACK_FORMAT_MPEG,
	SUPPORTED_PLAYBACK_FORMAT_EXIF_JPEG,
	SUPPORTED_PLAYBACK_FORMAT_TIFF_EP,
	SUPPORTED_PLAYBACK_FORMAT_BMP,
	SUPPORTED_PLAYBACK_FORMAT_GIF,
	SUPPORTED_PLAYBACK_FORMAT_JFIF,
	SUPPORTED_PLAYBACK_FORMAT_PNG,
	SUPPORTED_PLAYBACK_FORMAT_TIFF,
	SUPPORTED_PLAYBACK_FORMAT_WMA,
	SUPPORTED_PLAYBACK_FORMAT_OGG,
	SUPPORTED_PLAYBACK_FORMAT_AAC,
	SUPPORTED_PLAYBACK_FORMAT_MP4_CONTAINER,
	SUPPORTED_PLAYBACK_FORMAT_MP2,
	SUPPORTED_PLAYBACK_FORMAT_3GP_CONTAINER,
	SUPPORTED_PLAYBACK_FORMAT_ABSTRACT_AV_PLAYLIST,
	SUPPORTED_PLAYBACK_FORMAT_WPL_PLAYLIST,
	SUPPORTED_PLAYBACK_FORMAT_M3U_PLAYLIST,
	SUPPORTED_PLAYBACK_FORMAT_PLS_PLAYLIST,
	SUPPORTED_PLAYBACK_FORMAT_XML_DOCUMENT,
	SUPPORTED_PLAYBACK_FORMAT_FLAC
};

MtpObjectHandle MyMtpDatabase::beginSendObject(const char* path,
											MtpObjectFormat format,
											MtpObjectHandle parent,
											MtpStorageID storage,
											uint64_t size,
											time_t modified) {
	if (storagemap.find(storage) == storagemap.end())
		return kInvalidObjectHandle;
	return storagemap[storage]->beginSendObject(path, format, parent, size, modified);
}

void MyMtpDatabase::endSendObject(const char* path, MtpObjectHandle handle,
								MtpObjectFormat format, bool succeeded) {
	MTPD("endSendObject() %s\n", path);
	if (!succeeded) {
		MTPE("endSendObject() failed, unlinking %s\n", path);
		unlink(path);
	}
	std::map<int, MtpStorage*>::iterator storit;
	for (storit = storagemap.begin(); storit != storagemap.end(); storit++)
		storit->second->endSendObject(path, handle, format, succeeded);
}

void MyMtpDatabase::createDB(MtpStorage* storage, MtpStorageID storageID) {
	MTPD("MyMtpDatabase::createDB called\n");
	storagemap[storageID] = storage;
	storage->createDB();
}

void MyMtpDatabase::destroyDB(MtpStorageID storageID) {
	MtpStorage* storage = storagemap[storageID];
	storagemap.erase(storageID);
	delete storage;
}

MtpObjectHandleList* MyMtpDatabase::getObjectList(MtpStorageID storageID,
									MtpObjectFormat format,
									MtpObjectHandle parent) {
	MTPD("storageID: %d\n", storageID);
	MtpObjectHandleList* list = storagemap[storageID]->getObjectList(storageID, parent);
	MTPD("list: %d\n", list->size());
	return list;
}

int MyMtpDatabase::getNumObjects(MtpStorageID storageID,
									MtpObjectFormat format,
									MtpObjectHandle parent) {
	MtpObjectHandleList* list = storagemap[storageID]->getObjectList(storageID, parent);
	int size = list->size();
	delete list;
	return size;
}

MtpObjectFormatList* MyMtpDatabase::getSupportedPlaybackFormats() {
	// This function tells the host PC which file formats the device supports
	MtpObjectFormatList* list = new MtpObjectFormatList();
	int length = sizeof(SUPPORTED_PLAYBACK_FORMATS) / sizeof(SUPPORTED_PLAYBACK_FORMATS[0]);
	MTPD("MyMtpDatabase::getSupportedPlaybackFormats length: %i\n", length);
	for (int i = 0; i < length; i++) {
		MTPD("supported playback format: %x\n", SUPPORTED_PLAYBACK_FORMATS[i]);
		list->push(SUPPORTED_PLAYBACK_FORMATS[i]);
	}
	return list;
}

MtpObjectFormatList* MyMtpDatabase::getSupportedCaptureFormats() {
	// Android OS implementation of this function returns NULL
	// so we are not implementing this function either.
	MTPD("MyMtpDatabase::getSupportedCaptureFormats returning NULL (This is what Android does as well).\n");
	return NULL;
}

MtpObjectPropertyList* MyMtpDatabase::getSupportedObjectProperties(MtpObjectFormat format) {
	int* properties;
	MtpObjectPropertyList* list = new MtpObjectPropertyList();
	int length = 0;
	switch (format) {
		case MTP_FORMAT_MP3:
		case MTP_FORMAT_WAV:
		case MTP_FORMAT_WMA:
		case MTP_FORMAT_OGG:
		case MTP_FORMAT_AAC:
			properties = AUDIO_PROPERTIES;
			length = sizeof(AUDIO_PROPERTIES) / sizeof(AUDIO_PROPERTIES[0]);
			break;
		case MTP_FORMAT_MPEG:
		case MTP_FORMAT_3GP_CONTAINER:
		case MTP_FORMAT_WMV:
			properties = VIDEO_PROPERTIES;
			length = sizeof(VIDEO_PROPERTIES) / sizeof(VIDEO_PROPERTIES[0]);
			break;
		case MTP_FORMAT_EXIF_JPEG:
		case MTP_FORMAT_GIF:
		case MTP_FORMAT_PNG:
		case MTP_FORMAT_BMP:
			properties = IMAGE_PROPERTIES;
			length = sizeof(IMAGE_PROPERTIES) / sizeof(IMAGE_PROPERTIES[0]);
			break;
		case 0:
			properties = ALL_PROPERTIES;
			length = sizeof(ALL_PROPERTIES) / sizeof(ALL_PROPERTIES[0]);
			break;
		default:
			properties = FILE_PROPERTIES;
			length = sizeof(FILE_PROPERTIES) / sizeof(FILE_PROPERTIES[0]);
	}
	MTPD("MyMtpDatabase::getSupportedObjectProperties length is: %i, format: %x", length, format);
	for (int i = 0; i < length; i++) {
		MTPD("supported object property: %x\n", properties[i]);
		list->push(properties[i]);
	}
	return list;
}

MtpDevicePropertyList* MyMtpDatabase::getSupportedDeviceProperties() {
	MtpDevicePropertyList* list = new MtpDevicePropertyList();
	int length = sizeof(DEVICE_PROPERTIES) / sizeof(DEVICE_PROPERTIES[0]);
	MTPD("MyMtpDatabase::getSupportedDeviceProperties length was: %i\n", length);
	for (int i = 0; i < length; i++)
		list->push(DEVICE_PROPERTIES[i]);
	return list;
}

MtpResponseCode MyMtpDatabase::getObjectPropertyValue(MtpObjectHandle handle,
											MtpObjectProperty property,
											MtpDataPacket& packet) {
	MTPD("MyMtpDatabase::getObjectPropertyValue mtpid: %u, property: %x\n", handle, property);
	int type;
	MtpResponseCode result = MTP_RESPONSE_INVALID_OBJECT_HANDLE;
	MtpStorage::PropEntry prop;
	if (!getObjectPropertyInfo(property, type)) {
		MTPE("MyMtpDatabase::getObjectPropertyValue returning MTP_RESPONSE_OBJECT_PROP_NOT_SUPPORTED\n");
		return MTP_RESPONSE_OBJECT_PROP_NOT_SUPPORTED;
	}
	std::map<int, MtpStorage*>::iterator storit;
	for (storit = storagemap.begin(); storit != storagemap.end(); storit++) {
		if (storit->second->getObjectPropertyValue(handle, property, prop) == 0) {
			result = MTP_RESPONSE_OK;
			break;
		}
	}

	if (result != MTP_RESPONSE_OK) {
		MTPE("MyMtpDatabase::getObjectPropertyValue unable to locate handle: %u\n", handle);
		return MTP_RESPONSE_INVALID_OBJECT_HANDLE;
	}

	uint64_t longValue = prop.intvalue;
	// special case date properties, which are strings to MTP
	// but stored internally as a uint64
	if (property == MTP_PROPERTY_DATE_MODIFIED || property == MTP_PROPERTY_DATE_ADDED) {
		char date[20];
		formatDateTime(longValue, date, sizeof(date));
		packet.putString(date);
		goto out;
	}
	// release date is stored internally as just the year
	if (property == MTP_PROPERTY_ORIGINAL_RELEASE_DATE) {
		char date[20];
		snprintf(date, sizeof(date), "%04lld0101T000000", longValue);
		packet.putString(date);
		goto out;
	}

	switch (type) {
		case MTP_TYPE_INT8:
			packet.putInt8(longValue);
			break;
		case MTP_TYPE_UINT8:
			packet.putUInt8(longValue);
			break;
		case MTP_TYPE_INT16:
			packet.putInt16(longValue);
			break;
		case MTP_TYPE_UINT16:
			packet.putUInt16(longValue);
			break;
		case MTP_TYPE_INT32:
			packet.putInt32(longValue);
			break;
		case MTP_TYPE_UINT32:
			packet.putUInt32(longValue);
			break;
		case MTP_TYPE_INT64:
			packet.putInt64(longValue);
			break;
		case MTP_TYPE_UINT64:
			packet.putUInt64(longValue);
			break;
		case MTP_TYPE_INT128:
			packet.putInt128(longValue);
			break;
		case MTP_TYPE_UINT128:
			packet.putUInt128(longValue);
			break;
		case MTP_TYPE_STR:
			{
				/*std::string stringValue = (string)stringValuesArray[0];
				if (stringValue) {
					const char* str = stringValue.c_str();
					if (str == NULL) {
						return MTP_RESPONSE_GENERAL_ERROR;
					}
					packet.putString(str);
				} else {
					packet.putEmptyString();
				}*/
				packet.putString(prop.strvalue.c_str());
				MTPD("MTP_TYPE_STR: %x = %s\n", prop.property, prop.strvalue.c_str());
				//MTPE("STRING unsupported type in getObjectPropertyValue\n");
				//result = MTP_RESPONSE_INVALID_OBJECT_PROP_FORMAT;
				break;
			}
		default:
			MTPE("unsupported type in getObjectPropertyValue\n");
			result = MTP_RESPONSE_INVALID_OBJECT_PROP_FORMAT;
	}
out:
	return result;
}

MtpResponseCode MyMtpDatabase::setObjectPropertyValue(MtpObjectHandle handle,
											MtpObjectProperty property,
											MtpDataPacket& packet) {
	int type;
	MTPD("MyMtpDatabase::setObjectPropertyValue start\n");
	if (!getObjectPropertyInfo(property, type)) {
		MTPE("MyMtpDatabase::setObjectPropertyValue returning MTP_RESPONSE_OBJECT_PROP_NOT_SUPPORTED\n");
		return MTP_RESPONSE_OBJECT_PROP_NOT_SUPPORTED;
	}
	MTPD("MyMtpDatabase::setObjectPropertyValue continuing\n");
	long longValue = 0;
	std::string stringValue;

	switch (type) {
		case MTP_TYPE_INT8:
			MTPD("int8\n");
			longValue = packet.getInt8();
			break;
		case MTP_TYPE_UINT8:
			MTPD("uint8\n");
			longValue = packet.getUInt8();
			break;
		case MTP_TYPE_INT16:
			MTPD("int16\n");
			longValue = packet.getInt16();
			break;
		case MTP_TYPE_UINT16:
			MTPD("uint16\n");
			longValue = packet.getUInt16();
			break;
		case MTP_TYPE_INT32:
			MTPD("int32\n");
			longValue = packet.getInt32();
			break;
		case MTP_TYPE_UINT32:
			MTPD("uint32\n");
			longValue = packet.getUInt32();
			break;
		case MTP_TYPE_INT64:
			MTPD("int64\n");
			longValue = packet.getInt64();
			break;
		case MTP_TYPE_UINT64:
			MTPD("uint64\n");
			longValue = packet.getUInt64();
			break;
		case MTP_TYPE_STR:
			{
				MTPD("string\n");
				MtpStringBuffer buffer;
				packet.getString(buffer);
				stringValue = (const char *)buffer;
				break;
			 }
		default:
			MTPE("MyMtpDatabase::setObjectPropertyValue unsupported type %i in getObjectPropertyValue\n", type);
			return MTP_RESPONSE_INVALID_OBJECT_PROP_FORMAT;
	}

	int result = MTP_RESPONSE_OBJECT_PROP_NOT_SUPPORTED;

	switch (property) {
		case MTP_PROPERTY_OBJECT_FILE_NAME:
			{
				MTPD("MyMtpDatabase::setObjectPropertyValue renaming file, handle: %d, new name: '%s'\n", handle, stringValue.c_str());
				std::map<int, MtpStorage*>::iterator storit;
				for (storit = storagemap.begin(); storit != storagemap.end(); storit++) {
					if (storit->second->renameObject(handle, stringValue) == 0) {
						MTPD("MTP_RESPONSE_OK\n");
						result = MTP_RESPONSE_OK;
						break;
					}
				}
			}
			break;

		default:
			MTPE("MyMtpDatabase::setObjectPropertyValue property %x not supported.\n", property);
			result = MTP_RESPONSE_OBJECT_PROP_NOT_SUPPORTED;
	}
	MTPD("MyMtpDatabase::setObjectPropertyValue returning %d\n", result);
	return result;
}

MtpResponseCode MyMtpDatabase::getDevicePropertyValue(MtpDeviceProperty property,
											MtpDataPacket& packet) {
	int type, result = 0;
	char prop_value[PROPERTY_VALUE_MAX];
	MTPD("property %s\n",
			MtpDebug::getDevicePropCodeName(property));
	if (!getDevicePropertyInfo(property, type)) {
		MTPE("MyMtpDatabase::getDevicePropertyValue MTP_RESPONSE_DEVICE_PROP_NOT_SUPPORTED\n");
		return MTP_RESPONSE_DEVICE_PROP_NOT_SUPPORTED;
	}
	MTPD("property %s\n",
			MtpDebug::getDevicePropCodeName(property));
	MTPD("property %x\n", property);
	MTPD("MTP_DEVICE_PROPERTY_DEVICE_FRIENDLY_NAME %x\n", MTP_DEVICE_PROPERTY_DEVICE_FRIENDLY_NAME); 
	switch (property) {
		case MTP_DEVICE_PROPERTY_SYNCHRONIZATION_PARTNER:
		case MTP_DEVICE_PROPERTY_DEVICE_FRIENDLY_NAME:
			result =  MTP_RESPONSE_OK;
			break;
		default:
		{
			MTPE("MyMtpDatabase::getDevicePropertyValue property %x not supported\n", property);
			result = MTP_RESPONSE_DEVICE_PROP_NOT_SUPPORTED;
			break;
		}
	}

	if (result != MTP_RESPONSE_OK) {
		MTPD("MTP_REPONSE_OK NOT OK\n");
		return result;
	}

	long longValue = 0;
	property_get("ro.build.product", prop_value, "unknown manufacturer");
	switch (type) {
		case MTP_TYPE_INT8: {
			MTPD("MTP_TYPE_INT8\n");
			packet.putInt8(longValue);
			break;
		}
		case MTP_TYPE_UINT8:
		{
			MTPD("MTP_TYPE_UINT8\n");
			packet.putUInt8(longValue);
			break;
		}
		case MTP_TYPE_INT16:
		{
			MTPD("MTP_TYPE_INT16\n");
			packet.putInt16(longValue);
			break;
		}
		case MTP_TYPE_UINT16:
		{
			MTPD("MTP_TYPE_UINT16\n");
			packet.putUInt16(longValue);
			break;
		}
		case MTP_TYPE_INT32:
		{
			MTPD("MTP_TYPE_INT32\n");
			packet.putInt32(longValue);
			break;
		}
		case MTP_TYPE_UINT32:
		{
			MTPD("MTP_TYPE_UINT32\n");
			packet.putUInt32(longValue);
			break;
		}
		case MTP_TYPE_INT64:
		{
			MTPD("MTP_TYPE_INT64\n");
			packet.putInt64(longValue);
			break;
		}
		case MTP_TYPE_UINT64:
		{
			MTPD("MTP_TYPE_UINT64\n");
			packet.putUInt64(longValue);
			break;
		}
		case MTP_TYPE_INT128:
		{
			MTPD("MTP_TYPE_INT128\n");
			packet.putInt128(longValue);
			break;
		}
		case MTP_TYPE_UINT128:
		{
			MTPD("MTP_TYPE_UINT128\n");
			packet.putInt128(longValue);
			break;
		}
		case MTP_TYPE_STR:
		{
			MTPD("MTP_TYPE_STR\n");
			char* str = prop_value;
			packet.putString(str);
			break;
		 }
		default:
			MTPE("MyMtpDatabase::getDevicePropertyValue unsupported type %i in getDevicePropertyValue\n", type);
			return MTP_RESPONSE_INVALID_DEVICE_PROP_FORMAT;
	}

	return MTP_RESPONSE_OK;
}

MtpResponseCode MyMtpDatabase::setDevicePropertyValue(MtpDeviceProperty property, MtpDataPacket& packet) {
   	int type;
	MTPE("MyMtpDatabase::setDevicePropertyValue not implemented, returning 0\n");
	return 0;
}

MtpResponseCode MyMtpDatabase::resetDeviceProperty(MtpDeviceProperty property) {
	MTPE("MyMtpDatabase::resetDeviceProperty not implemented, returning -1\n");
   	return -1;
}

MtpResponseCode MyMtpDatabase::getObjectPropertyList(MtpObjectHandle handle, uint32_t format, uint32_t property, int groupCode, int depth, MtpDataPacket& packet) {
	MTPD("getObjectPropertyList()\n");
	MTPD("property: %x\n", property);
	std::map<int, MtpStorage*>::iterator storit;
	for (storit = storagemap.begin(); storit != storagemap.end(); storit++) {
		MTPD("MyMtpDatabase::getObjectPropertyList calling getObjectPropertyList\n");
		if (storit->second->getObjectPropertyList(handle, format, property, groupCode, depth, packet) == 0) {
			MTPD("MTP_RESPONSE_OK\n");
   			return MTP_RESPONSE_OK;
		}
	}
	MTPE("MyMtpDatabase::getObjectPropertyList MTP_RESPOSNE_INVALID_OBJECT_HANDLE %i\n", handle);
	return MTP_RESPONSE_INVALID_OBJECT_HANDLE;
}

MtpResponseCode MyMtpDatabase::getObjectInfo(MtpObjectHandle handle, MtpObjectInfo& info) {
	std::map<int, MtpStorage*>::iterator storit;
	for (storit = storagemap.begin(); storit != storagemap.end(); storit++) {
		if (storit->second->getObjectInfo(handle, info) == 0) {
			MTPD("MTP_RESPONSE_OK\n");
			return MTP_RESPONSE_OK;
		}
	}
	MTPE("MyMtpDatabase::getObjectInfo MTP_RESPONSE_INVALID_OBJECT_HANDLE %i\n", handle);
	return MTP_RESPONSE_INVALID_OBJECT_HANDLE;
}

void* MyMtpDatabase::getThumbnail(MtpObjectHandle handle, size_t& outThumbSize) {
	MtpString path;
	int64_t length;
	MtpObjectFormat format;
	void* result = NULL;
	outThumbSize = 0;
	MTPE("MyMtpDatabase::getThumbnail not implemented, returning 0\n");
	return 0;
}

MtpResponseCode MyMtpDatabase::getObjectFilePath(MtpObjectHandle handle, MtpString& outFilePath, int64_t& outFileLength, MtpObjectFormat& outFormat) {
	std::map<int, MtpStorage*>::iterator storit;
	for (storit = storagemap.begin(); storit != storagemap.end(); storit++) {
		MTPD("MyMtpDatabase::getObjectFilePath calling getObjectFilePath\n");
		if (storit->second->getObjectFilePath(handle, outFilePath, outFileLength, outFormat) == 0) {
			MTPD("MTP_RESPONSE_OK\n");
			return MTP_RESPONSE_OK;
		}
	}
	MTPE("MyMtpDatabase::getObjectFilePath MTP_RESPOSNE_INVALID_OBJECT_HANDLE %i\n", handle);
	return MTP_RESPONSE_INVALID_OBJECT_HANDLE;
}

MtpResponseCode MyMtpDatabase::deleteFile(MtpObjectHandle handle) {
	MTPD("deleteFile\n");
	std::map<int, MtpStorage*>::iterator storit;
	for (storit = storagemap.begin(); storit != storagemap.end(); storit++) {
		if (storit->second->deleteFile(handle) == 0) {
			MTPD("MTP_RESPONSE_OK\n");
			return MTP_RESPONSE_OK;
		}
	}
	MTPE("MyMtpDatabase::deleteFile MTP_RESPONSE_INVALID_OBJECT_HANDLE %i\n", handle);
	return MTP_RESPONSE_INVALID_OBJECT_HANDLE;
}

struct PropertyTableEntry {
	MtpObjectProperty   property;
	int				 type;
};

static const PropertyTableEntry   kObjectPropertyTable[] = {
	{   MTP_PROPERTY_STORAGE_ID,		MTP_TYPE_UINT32	 },
	{   MTP_PROPERTY_OBJECT_FORMAT,	 MTP_TYPE_UINT16	 },
	{   MTP_PROPERTY_PROTECTION_STATUS, MTP_TYPE_UINT16	 },
	{   MTP_PROPERTY_OBJECT_SIZE,	   MTP_TYPE_UINT64	 },
	{   MTP_PROPERTY_OBJECT_FILE_NAME,  MTP_TYPE_STR		},
	{   MTP_PROPERTY_DATE_MODIFIED,	 MTP_TYPE_STR		},
	{   MTP_PROPERTY_PARENT_OBJECT,	 MTP_TYPE_UINT32	 },
	{   MTP_PROPERTY_PERSISTENT_UID,	MTP_TYPE_UINT128	},
	{   MTP_PROPERTY_NAME,			  MTP_TYPE_STR		},
	{   MTP_PROPERTY_DISPLAY_NAME,	  MTP_TYPE_STR		},
	{   MTP_PROPERTY_DATE_ADDED,		MTP_TYPE_STR		},
	{   MTP_PROPERTY_ARTIST,			MTP_TYPE_STR		},
	{   MTP_PROPERTY_ALBUM_NAME,		MTP_TYPE_STR		},
	{   MTP_PROPERTY_ALBUM_ARTIST,	  MTP_TYPE_STR		},
	{   MTP_PROPERTY_TRACK,			 MTP_TYPE_UINT16	 },
	{   MTP_PROPERTY_ORIGINAL_RELEASE_DATE, MTP_TYPE_STR	},
	{   MTP_PROPERTY_GENRE,			 MTP_TYPE_STR		},
	{   MTP_PROPERTY_COMPOSER,		  MTP_TYPE_STR		},
	{   MTP_PROPERTY_DURATION,		  MTP_TYPE_UINT32	 },
	{   MTP_PROPERTY_DESCRIPTION,	   MTP_TYPE_STR		},
};

static const PropertyTableEntry   kDevicePropertyTable[] = {
	{   MTP_DEVICE_PROPERTY_SYNCHRONIZATION_PARTNER,	MTP_TYPE_STR },
	{   MTP_DEVICE_PROPERTY_DEVICE_FRIENDLY_NAME,	   MTP_TYPE_STR },
	{   MTP_DEVICE_PROPERTY_IMAGE_SIZE,				 MTP_TYPE_STR },
};

bool MyMtpDatabase::getObjectPropertyInfo(MtpObjectProperty property, int& type) {
	int count = sizeof(kObjectPropertyTable) / sizeof(kObjectPropertyTable[0]);
	const PropertyTableEntry* entry = kObjectPropertyTable;
	MTPD("MyMtpDatabase::getObjectPropertyInfo size is: %i\n", count);
	for (int i = 0; i < count; i++, entry++) {
		if (entry->property == property) {
			type = entry->type;
			return true;
		}
	}
	return false;
}

bool MyMtpDatabase::getDevicePropertyInfo(MtpDeviceProperty property, int& type) {
	int count = sizeof(kDevicePropertyTable) / sizeof(kDevicePropertyTable[0]);
	const PropertyTableEntry* entry = kDevicePropertyTable;
	MTPD("MyMtpDatabase::getDevicePropertyInfo count is: %i\n", count);
	for (int i = 0; i < count; i++, entry++) {
		if (entry->property == property) {
			type = entry->type;
			MTPD("type: %x\n", type);
			return true;
		}
	}
	return false;
}

MtpObjectHandleList* MyMtpDatabase::getObjectReferences(MtpObjectHandle handle) {
	// call function and place files with associated handles into int array
	MTPD("MyMtpDatabase::getObjectReferences returning null, this seems to be what Android always does.\n");
	MTPD("handle: %d\n", handle);
	// Windows + Android seems to always return a NULL in this function, c == null path
	// The way that this is handled in Android then is to do this:
	return NULL;
}

MtpResponseCode MyMtpDatabase::setObjectReferences(MtpObjectHandle handle,
													MtpObjectHandleList* references) {
	int count = references->size();
	MTPE("MyMtpDatabase::setObjectReferences not implemented, returning 0\n");
	return 0;
}

MtpProperty* MyMtpDatabase::getObjectPropertyDesc(MtpObjectProperty property,
											MtpObjectFormat format) {
	MTPD("MyMtpDatabase::getObjectPropertyDesc start\n");
	MtpProperty* result = NULL;
	switch (property) {
		case MTP_PROPERTY_OBJECT_FORMAT:
			// use format as default value
			result = new MtpProperty(property, MTP_TYPE_UINT16, false, format);
			break;
		case MTP_PROPERTY_PROTECTION_STATUS:
		case MTP_PROPERTY_TRACK:
			result = new MtpProperty(property, MTP_TYPE_UINT16);
			break;
		case MTP_PROPERTY_STORAGE_ID:
		case MTP_PROPERTY_PARENT_OBJECT:
		case MTP_PROPERTY_DURATION:
			result = new MtpProperty(property, MTP_TYPE_UINT32);
			break;
		case MTP_PROPERTY_OBJECT_SIZE:
			result = new MtpProperty(property, MTP_TYPE_UINT64);
			break;
		case MTP_PROPERTY_PERSISTENT_UID:
			result = new MtpProperty(property, MTP_TYPE_UINT128);
			break;
		case MTP_PROPERTY_NAME:
		case MTP_PROPERTY_DISPLAY_NAME:
		case MTP_PROPERTY_ARTIST:
		case MTP_PROPERTY_ALBUM_NAME:
		case MTP_PROPERTY_ALBUM_ARTIST:
		case MTP_PROPERTY_GENRE:
		case MTP_PROPERTY_COMPOSER:
		case MTP_PROPERTY_DESCRIPTION:
			result = new MtpProperty(property, MTP_TYPE_STR);
			break;
		case MTP_PROPERTY_DATE_MODIFIED:
		case MTP_PROPERTY_DATE_ADDED:
		case MTP_PROPERTY_ORIGINAL_RELEASE_DATE:
			result = new MtpProperty(property, MTP_TYPE_STR);
			result->setFormDateTime();
			break;
		case MTP_PROPERTY_OBJECT_FILE_NAME:
			// We allow renaming files and folders
			result = new MtpProperty(property, MTP_TYPE_STR, true);
			break;
	}
	return result;
}

MtpProperty* MyMtpDatabase::getDevicePropertyDesc(MtpDeviceProperty property) {
	MtpProperty* result = NULL;
	int ret;
	bool writable = false;
	switch (property) {
		case MTP_DEVICE_PROPERTY_SYNCHRONIZATION_PARTNER:
		case MTP_DEVICE_PROPERTY_DEVICE_FRIENDLY_NAME:
			writable = true;
			// fall through
		case MTP_DEVICE_PROPERTY_IMAGE_SIZE:
			result = new MtpProperty(property, MTP_TYPE_STR, writable);

			// get current value
			// TODO: add actual values
			result->setCurrentValue(0);
			result->setDefaultValue(0);
			break;
		}

	return result;
}

void MyMtpDatabase::sessionStarted() {
	MTPD("MyMtpDatabase::sessionStarted not implemented or does nothing, returning\n");
	return;
}

void MyMtpDatabase::sessionEnded() {
	MTPD("MyMtpDatabase::sessionEnded not implemented or does nothing, returning\n");
	return;
}

// ----------------------------------------------------------------------------

void MyMtpDatabase::lockMutex(void) {
	std::map<int, MtpStorage*>::iterator storit;
	for (storit = storagemap.begin(); storit != storagemap.end(); storit++) {
		storit->second->lockMutex(0);
	}
}

void MyMtpDatabase::unlockMutex(void) {
	std::map<int, MtpStorage*>::iterator storit;
	for (storit = storagemap.begin(); storit != storagemap.end(); storit++) {
		storit->second->unlockMutex(0);
	}
}
