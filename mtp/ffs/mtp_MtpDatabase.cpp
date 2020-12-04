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

#include <assert.h>
#include <cutils/properties.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <map>
#include <string>

#include "MtpDataPacket.h"
#include "MtpDatabase.h"
#include "MtpDebug.h"
#include "MtpObjectInfo.h"
#include "MtpProperty.h"
#include "MtpStorage.h"
#include "MtpStringBuffer.h"
#include "MtpUtils.h"
#include "mtp.h"
#include "mtp_MtpDatabase.hpp"

IMtpDatabase::IMtpDatabase() {
  storagenum = 0;
  count = -1;
}

IMtpDatabase::~IMtpDatabase() {
  std::map<int, MtpStorage*>::iterator i;
  for (i = storagemap.begin(); i != storagemap.end(); i++) {
	delete i->second;
  }
}

int IMtpDatabase::DEVICE_PROPERTIES[3] = { MTP_DEVICE_PROPERTY_SYNCHRONIZATION_PARTNER,
										   MTP_DEVICE_PROPERTY_DEVICE_FRIENDLY_NAME,
										   MTP_DEVICE_PROPERTY_IMAGE_SIZE };

int IMtpDatabase::FILE_PROPERTIES[10] = {
  // NOTE must match beginning of AUDIO_PROPERTIES, VIDEO_PROPERTIES
  // and IMAGE_PROPERTIES below
  MTP_PROPERTY_STORAGE_ID, MTP_PROPERTY_OBJECT_FORMAT, MTP_PROPERTY_PROTECTION_STATUS,
  MTP_PROPERTY_OBJECT_SIZE, MTP_PROPERTY_OBJECT_FILE_NAME, MTP_PROPERTY_DATE_MODIFIED,
  MTP_PROPERTY_PARENT_OBJECT, MTP_PROPERTY_PERSISTENT_UID, MTP_PROPERTY_NAME,
  // TODO: why is DISPLAY_NAME not here?
  MTP_PROPERTY_DATE_ADDED
};

int IMtpDatabase::AUDIO_PROPERTIES[19] = {
  // NOTE must match FILE_PROPERTIES above
  MTP_PROPERTY_STORAGE_ID, MTP_PROPERTY_OBJECT_FORMAT, MTP_PROPERTY_PROTECTION_STATUS,
  MTP_PROPERTY_OBJECT_SIZE, MTP_PROPERTY_OBJECT_FILE_NAME, MTP_PROPERTY_DATE_MODIFIED,
  MTP_PROPERTY_PARENT_OBJECT, MTP_PROPERTY_PERSISTENT_UID, MTP_PROPERTY_NAME,
  MTP_PROPERTY_DISPLAY_NAME, MTP_PROPERTY_DATE_ADDED,

  // audio specific properties
  MTP_PROPERTY_ARTIST, MTP_PROPERTY_ALBUM_NAME, MTP_PROPERTY_ALBUM_ARTIST, MTP_PROPERTY_TRACK,
  MTP_PROPERTY_ORIGINAL_RELEASE_DATE, MTP_PROPERTY_DURATION, MTP_PROPERTY_GENRE,
  MTP_PROPERTY_COMPOSER
};

int IMtpDatabase::VIDEO_PROPERTIES[15] = {
  // NOTE must match FILE_PROPERTIES above
  MTP_PROPERTY_STORAGE_ID, MTP_PROPERTY_OBJECT_FORMAT, MTP_PROPERTY_PROTECTION_STATUS,
  MTP_PROPERTY_OBJECT_SIZE, MTP_PROPERTY_OBJECT_FILE_NAME, MTP_PROPERTY_DATE_MODIFIED,
  MTP_PROPERTY_PARENT_OBJECT, MTP_PROPERTY_PERSISTENT_UID, MTP_PROPERTY_NAME,
  MTP_PROPERTY_DISPLAY_NAME, MTP_PROPERTY_DATE_ADDED,

  // video specific properties
  MTP_PROPERTY_ARTIST, MTP_PROPERTY_ALBUM_NAME, MTP_PROPERTY_DURATION, MTP_PROPERTY_DESCRIPTION
};

int IMtpDatabase::IMAGE_PROPERTIES[12] = {
  // NOTE must match FILE_PROPERTIES above
  MTP_PROPERTY_STORAGE_ID, MTP_PROPERTY_OBJECT_FORMAT, MTP_PROPERTY_PROTECTION_STATUS,
  MTP_PROPERTY_OBJECT_SIZE, MTP_PROPERTY_OBJECT_FILE_NAME, MTP_PROPERTY_DATE_MODIFIED,
  MTP_PROPERTY_PARENT_OBJECT, MTP_PROPERTY_PERSISTENT_UID, MTP_PROPERTY_NAME,
  MTP_PROPERTY_DISPLAY_NAME, MTP_PROPERTY_DATE_ADDED,

  // image specific properties
  MTP_PROPERTY_DESCRIPTION
};

int IMtpDatabase::ALL_PROPERTIES[25] = {
  // NOTE must match FILE_PROPERTIES above
  MTP_PROPERTY_STORAGE_ID, MTP_PROPERTY_OBJECT_FORMAT, MTP_PROPERTY_PROTECTION_STATUS,
  MTP_PROPERTY_OBJECT_SIZE, MTP_PROPERTY_OBJECT_FILE_NAME, MTP_PROPERTY_DATE_MODIFIED,
  MTP_PROPERTY_PARENT_OBJECT, MTP_PROPERTY_PERSISTENT_UID, MTP_PROPERTY_NAME,
  MTP_PROPERTY_DISPLAY_NAME, MTP_PROPERTY_DATE_ADDED,

  // image specific properties
  MTP_PROPERTY_DESCRIPTION,

  // audio specific properties
  MTP_PROPERTY_ARTIST, MTP_PROPERTY_ALBUM_NAME, MTP_PROPERTY_ALBUM_ARTIST, MTP_PROPERTY_TRACK,
  MTP_PROPERTY_ORIGINAL_RELEASE_DATE, MTP_PROPERTY_DURATION, MTP_PROPERTY_GENRE,
  MTP_PROPERTY_COMPOSER,

  // video specific properties
  MTP_PROPERTY_ARTIST, MTP_PROPERTY_ALBUM_NAME, MTP_PROPERTY_DURATION, MTP_PROPERTY_DESCRIPTION,

  // image specific properties
  MTP_PROPERTY_DESCRIPTION
};

int IMtpDatabase::SUPPORTED_PLAYBACK_FORMATS[26] = { SUPPORTED_PLAYBACK_FORMAT_UNDEFINED,
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
													 SUPPORTED_PLAYBACK_FORMAT_FLAC };

MtpObjectHandle IMtpDatabase::beginSendObject(const char* path, MtpObjectFormat format,
											  MtpObjectHandle parent, MtpStorageID storageID,
											  uint64_t size, time_t modified) {
  if (storagemap.find(storageID) == storagemap.end()) return kInvalidObjectHandle;
  return storagemap[storageID]->beginSendObject(path, format, parent, size, modified);
}

void IMtpDatabase::endSendObject(const char* path, MtpObjectHandle handle, MtpObjectFormat format,
								 bool succeeded) {
  MTPD("endSendObject() %s\n", path);
  if (!succeeded) {
	MTPE("endSendObject() failed, unlinking %s\n", path);
	unlink(path);
  }
  std::map<int, MtpStorage*>::iterator storit;
  for (storit = storagemap.begin(); storit != storagemap.end(); storit++)
	storit->second->endSendObject(path, handle, format, succeeded);
}

void IMtpDatabase::createDB(MtpStorage* storage, MtpStorageID storageID) {
  storagemap[storageID] = storage;
  storage->createDB();
}

void IMtpDatabase::destroyDB(MtpStorageID storageID) {
  MtpStorage* storage = storagemap[storageID];
  storagemap.erase(storageID);
  delete storage;
}

MtpObjectHandleList* IMtpDatabase::getObjectList(MtpStorageID storageID,
												 __attribute__((unused)) MtpObjectFormat format,
												 MtpObjectHandle parent) {
  MTPD("IMtpDatabase::getObjectList::storageID: %d\n", storageID);
  MtpObjectHandleList* list = storagemap[storageID]->getObjectList(storageID, parent);
  MTPD("IMtpDatabase::getObjectList::list size: %d\n", list->size());
  return list;
}

int IMtpDatabase::getNumObjects(MtpStorageID storageID,
								__attribute__((unused)) MtpObjectFormat format,
								MtpObjectHandle parent) {
  MtpObjectHandleList* list = storagemap[storageID]->getObjectList(storageID, parent);
  int size = list->size();
  delete list;
  return size;
}

MtpObjectFormatList* IMtpDatabase::getSupportedPlaybackFormats() {
  // This function tells the host PC which file formats the device supports
  MtpObjectFormatList* list = new MtpObjectFormatList();
  int length = sizeof(SUPPORTED_PLAYBACK_FORMATS) / sizeof(SUPPORTED_PLAYBACK_FORMATS[0]);
  MTPD("IMtpDatabase::getSupportedPlaybackFormats length: %i\n", length);
  for (int i = 0; i < length; i++) {
	MTPD("supported playback format: %x\n", SUPPORTED_PLAYBACK_FORMATS[i]);
	list->push_back(SUPPORTED_PLAYBACK_FORMATS[i]);
  }
  return list;
}

MtpObjectFormatList* IMtpDatabase::getSupportedCaptureFormats() {
  // Android OS implementation of this function returns NULL
  // so we are not implementing this function either.
  MTPD(
	  "IMtpDatabase::getSupportedCaptureFormats returning NULL (This is what Android does as "
	  "well).\n");
  return NULL;
}

MtpObjectPropertyList* IMtpDatabase::getSupportedObjectProperties(MtpObjectFormat format) {
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
  MTPD("IMtpDatabase::getSupportedObjectProperties length is: %i, format: %x", length, format);
  for (int i = 0; i < length; i++) {
	MTPD("supported object property: %x\n", properties[i]);
	list->push_back(properties[i]);
  }
  return list;
}

MtpDevicePropertyList* IMtpDatabase::getSupportedDeviceProperties() {
  MtpDevicePropertyList* list = new MtpDevicePropertyList();
  int length = sizeof(DEVICE_PROPERTIES) / sizeof(DEVICE_PROPERTIES[0]);
  MTPD("IMtpDatabase::getSupportedDeviceProperties length was: %i\n", length);
  for (int i = 0; i < length; i++) list->push_back(DEVICE_PROPERTIES[i]);
  return list;
}

MtpResponseCode IMtpDatabase::getObjectPropertyValue(MtpObjectHandle handle,
													 MtpObjectProperty property,
													 MtpDataPacket& packet) {
  MTPD("IMtpDatabase::getObjectPropertyValue mtpid: %u, property: %x\n", handle, property);
  int type;
  MtpResponseCode result = MTP_RESPONSE_INVALID_OBJECT_HANDLE;
  MtpStorage::PropEntry prop;
  if (!getObjectPropertyInfo(property, type)) {
	MTPE("IMtpDatabase::getObjectPropertyValue returning MTP_RESPONSE_OBJECT_PROP_NOT_SUPPORTED\n");
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
	MTPE("IMtpDatabase::getObjectPropertyValue unable to locate handle: %u\n", handle);
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
	case MTP_TYPE_STR: {
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
	  // MTPE("STRING unsupported type in getObjectPropertyValue\n");
	  // result = MTP_RESPONSE_INVALID_OBJECT_PROP_FORMAT;
	  break;
	}
	default:
	  MTPE("unsupported type in getObjectPropertyValue\n");
	  result = MTP_RESPONSE_INVALID_OBJECT_PROP_FORMAT;
  }
out:
  return result;
}

MtpResponseCode IMtpDatabase::setObjectPropertyValue(MtpObjectHandle handle,
													 MtpObjectProperty property,
													 MtpDataPacket& packet) {
  int type;
  MTPD("IMtpDatabase::setObjectPropertyValue start\n");
  if (!getObjectPropertyInfo(property, type)) {
	MTPE("IMtpDatabase::setObjectPropertyValue returning MTP_RESPONSE_OBJECT_PROP_NOT_SUPPORTED\n");
	return MTP_RESPONSE_OBJECT_PROP_NOT_SUPPORTED;
  }
  MTPD("IMtpDatabase::setObjectPropertyValue continuing\n");

  int8_t int8_t_value;
  uint8_t uint8_t_value;
  int16_t int16_t_value;
  uint16_t uint16_t_value;
  int32_t int32_t_value;
  uint32_t uint32_t_value;
  int64_t int64_t_value;
  uint64_t uint64_t_value;
  std::string stringValue;

  switch (type) {
	case MTP_TYPE_INT8:
	  MTPD("int8\n");
	  packet.getInt8(int8_t_value);
	  break;
	case MTP_TYPE_UINT8:
	  MTPD("uint8\n");
	  packet.getUInt8(uint8_t_value);
	  break;
	case MTP_TYPE_INT16:
	  MTPD("int16\n");
	  packet.getInt16(int16_t_value);
	  break;
	case MTP_TYPE_UINT16:
	  MTPD("uint16\n");
	  packet.getUInt16(uint16_t_value);
	  break;
	case MTP_TYPE_INT32:
	  MTPD("int32\n");
	  packet.getInt32(int32_t_value);
	  break;
	case MTP_TYPE_UINT32:
	  MTPD("uint32\n");
	  packet.getUInt32(uint32_t_value);
	  break;
	case MTP_TYPE_INT64:
	  MTPD("int64\n");
	  packet.getInt64(int64_t_value);
	  break;
	case MTP_TYPE_UINT64:
	  MTPD("uint64\n");
	  packet.getUInt64(uint64_t_value);
	  break;
	case MTP_TYPE_STR: {
	  MTPD("string\n");
	  MtpStringBuffer buffer;
	  packet.getString(buffer);
	  stringValue = buffer;
	  break;
	}
	default:
	  MTPE("IMtpDatabase::setObjectPropertyValue unsupported type %i in getObjectPropertyValue\n",
		   type);
	  return MTP_RESPONSE_INVALID_OBJECT_PROP_FORMAT;
  }

  int result = MTP_RESPONSE_OBJECT_PROP_NOT_SUPPORTED;

  switch (property) {
	case MTP_PROPERTY_OBJECT_FILE_NAME: {
	  MTPD("IMtpDatabase::setObjectPropertyValue renaming file, handle: %d, new name: '%s'\n",
		   handle, stringValue.c_str());
	  std::map<int, MtpStorage*>::iterator storit;
	  for (storit = storagemap.begin(); storit != storagemap.end(); storit++) {
		if (storit->second->renameObject(handle, stringValue) == 0) {
		  MTPD("MTP_RESPONSE_OK\n");
		  result = MTP_RESPONSE_OK;
		  break;
		}
	  }
	} break;

	default:
	  MTPE("IMtpDatabase::setObjectPropertyValue property %x not supported.\n", property);
	  result = MTP_RESPONSE_OBJECT_PROP_NOT_SUPPORTED;
  }
  MTPD("IMtpDatabase::setObjectPropertyValue returning %d\n", result);
  return result;
}

MtpResponseCode IMtpDatabase::getDevicePropertyValue(MtpDeviceProperty property,
													 MtpDataPacket& packet) {
  int type, result = 0;
  char prop_value[PROPERTY_VALUE_MAX];
  MTPD("property %s\n", MtpDebug::getDevicePropCodeName(property));
  if (!getDevicePropertyInfo(property, type)) {
	MTPE("IMtpDatabase::getDevicePropertyValue MTP_RESPONSE_DEVICE_PROP_NOT_SUPPORTED\n");
	return MTP_RESPONSE_DEVICE_PROP_NOT_SUPPORTED;
  }
  MTPD("property %s\n", MtpDebug::getDevicePropCodeName(property));
  MTPD("property %x\n", property);
  MTPD("MTP_DEVICE_PROPERTY_DEVICE_FRIENDLY_NAME %x\n", MTP_DEVICE_PROPERTY_DEVICE_FRIENDLY_NAME);
  switch (property) {
	case MTP_DEVICE_PROPERTY_SYNCHRONIZATION_PARTNER:
	case MTP_DEVICE_PROPERTY_DEVICE_FRIENDLY_NAME:
	  result = MTP_RESPONSE_OK;
	  break;
	default: {
	  MTPE("IMtpDatabase::getDevicePropertyValue property %x not supported\n", property);
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
	case MTP_TYPE_UINT8: {
	  MTPD("MTP_TYPE_UINT8\n");
	  packet.putUInt8(longValue);
	  break;
	}
	case MTP_TYPE_INT16: {
	  MTPD("MTP_TYPE_INT16\n");
	  packet.putInt16(longValue);
	  break;
	}
	case MTP_TYPE_UINT16: {
	  MTPD("MTP_TYPE_UINT16\n");
	  packet.putUInt16(longValue);
	  break;
	}
	case MTP_TYPE_INT32: {
	  MTPD("MTP_TYPE_INT32\n");
	  packet.putInt32(longValue);
	  break;
	}
	case MTP_TYPE_UINT32: {
	  MTPD("MTP_TYPE_UINT32\n");
	  packet.putUInt32(longValue);
	  break;
	}
	case MTP_TYPE_INT64: {
	  MTPD("MTP_TYPE_INT64\n");
	  packet.putInt64(longValue);
	  break;
	}
	case MTP_TYPE_UINT64: {
	  MTPD("MTP_TYPE_UINT64\n");
	  packet.putUInt64(longValue);
	  break;
	}
	case MTP_TYPE_INT128: {
	  MTPD("MTP_TYPE_INT128\n");
	  packet.putInt128(longValue);
	  break;
	}
	case MTP_TYPE_UINT128: {
	  MTPD("MTP_TYPE_UINT128\n");
	  packet.putInt128(longValue);
	  break;
	}
	case MTP_TYPE_STR: {
	  MTPD("MTP_TYPE_STR\n");
	  char* str = prop_value;
	  packet.putString(str);
	  break;
	}
	default:
	  MTPE("IMtpDatabase::getDevicePropertyValue unsupported type %i in getDevicePropertyValue\n",
		   type);
	  return MTP_RESPONSE_INVALID_DEVICE_PROP_FORMAT;
  }

  return MTP_RESPONSE_OK;
}

MtpResponseCode IMtpDatabase::setDevicePropertyValue(__attribute__((unused))
													 MtpDeviceProperty property,
													 __attribute__((unused))
													 MtpDataPacket& packet) {
  MTPE("IMtpDatabase::setDevicePropertyValue not implemented, returning 0\n");
  return 0;
}

MtpResponseCode IMtpDatabase::resetDeviceProperty(__attribute__((unused))
												  MtpDeviceProperty property) {
  MTPE("IMtpDatabase::resetDeviceProperty not implemented, returning -1\n");
  return -1;
}

MtpResponseCode IMtpDatabase::getObjectPropertyList(MtpObjectHandle handle, uint32_t format,
													uint32_t property, int groupCode, int depth,
													MtpDataPacket& packet) {
  MTPD("getObjectPropertyList()\n");
  MTPD("property: %x\n", property);
  std::map<int, MtpStorage*>::iterator storit;
  for (storit = storagemap.begin(); storit != storagemap.end(); storit++) {
	MTPD("IMtpDatabase::getObjectPropertyList calling getObjectPropertyList\n");
	if (storit->second->getObjectPropertyList(handle, format, property, groupCode, depth, packet) ==
		0) {
	  MTPD("MTP_RESPONSE_OK\n");
	  return MTP_RESPONSE_OK;
	}
  }
  MTPE("IMtpDatabase::getObjectPropertyList MTP_RESPONSE_INVALID_OBJECT_HANDLE %i\n", handle);
  return MTP_RESPONSE_INVALID_OBJECT_HANDLE;
}

MtpResponseCode IMtpDatabase::getObjectInfo(MtpObjectHandle handle, MtpObjectInfo& info) {
  std::map<int, MtpStorage*>::iterator storit;
  for (storit = storagemap.begin(); storit != storagemap.end(); storit++) {
	if (storit->second->getObjectInfo(handle, info) == 0) {
	  MTPD("MTP_RESPONSE_OK\n");
	  return MTP_RESPONSE_OK;
	}
  }
  MTPE("IMtpDatabase::getObjectInfo MTP_RESPONSE_INVALID_OBJECT_HANDLE %i\n", handle);
  return MTP_RESPONSE_INVALID_OBJECT_HANDLE;
}

void* IMtpDatabase::getThumbnail(__attribute__((unused)) MtpObjectHandle handle,
								 __attribute__((unused)) size_t& outThumbSize) {
  MTPE("IMtpDatabase::getThumbnail not implemented, returning 0\n");
  return 0;
}

MtpResponseCode IMtpDatabase::getObjectFilePath(MtpObjectHandle handle,
												MtpStringBuffer& outFilePath,
												int64_t& outFileLength,
												MtpObjectFormat& outFormat) {
  std::map<int, MtpStorage*>::iterator storit;
  for (storit = storagemap.begin(); storit != storagemap.end(); storit++) {
	MTPD("IMtpDatabase::getObjectFilePath calling getObjectFilePath\n");
	if (storit->second->getObjectFilePath(handle, outFilePath, outFileLength, outFormat) == 0) {
	  MTPD("MTP_RESPONSE_OK\n");
	  return MTP_RESPONSE_OK;
	}
  }
  MTPE("IMtpDatabase::getObjectFilePath MTP_RESPONSE_INVALID_OBJECT_HANDLE %i\n", handle);
  return MTP_RESPONSE_INVALID_OBJECT_HANDLE;
}

// MtpResponseCode IMtpDatabase::deleteFile(MtpObjectHandle handle) {
//	MTPD("IMtpDatabase::deleteFile\n");
//	std::map<int, MtpStorage*>::iterator storit;
//	for (storit = storagemap.begin(); storit != storagemap.end(); storit++) {
//		if (storit->second->deleteFile(handle) == 0) {
//			MTPD("MTP_RESPONSE_OK\n");
//			return MTP_RESPONSE_OK;
//		}
//	}
//	MTPE("IMtpDatabase::deleteFile MTP_RESPONSE_INVALID_OBJECT_HANDLE %i\n", handle);
//	return MTP_RESPONSE_INVALID_OBJECT_HANDLE;
// }

struct PropertyTableEntry {
  MtpObjectProperty property;
  int type;
};

static const PropertyTableEntry kObjectPropertyTable[] = {
  { MTP_PROPERTY_STORAGE_ID, MTP_TYPE_UINT32 },
  { MTP_PROPERTY_OBJECT_FORMAT, MTP_TYPE_UINT16 },
  { MTP_PROPERTY_PROTECTION_STATUS, MTP_TYPE_UINT16 },
  { MTP_PROPERTY_OBJECT_SIZE, MTP_TYPE_UINT64 },
  { MTP_PROPERTY_OBJECT_FILE_NAME, MTP_TYPE_STR },
  { MTP_PROPERTY_DATE_MODIFIED, MTP_TYPE_STR },
  { MTP_PROPERTY_PARENT_OBJECT, MTP_TYPE_UINT32 },
  { MTP_PROPERTY_PERSISTENT_UID, MTP_TYPE_UINT128 },
  { MTP_PROPERTY_NAME, MTP_TYPE_STR },
  { MTP_PROPERTY_DISPLAY_NAME, MTP_TYPE_STR },
  { MTP_PROPERTY_DATE_ADDED, MTP_TYPE_STR },
  { MTP_PROPERTY_ARTIST, MTP_TYPE_STR },
  { MTP_PROPERTY_ALBUM_NAME, MTP_TYPE_STR },
  { MTP_PROPERTY_ALBUM_ARTIST, MTP_TYPE_STR },
  { MTP_PROPERTY_TRACK, MTP_TYPE_UINT16 },
  { MTP_PROPERTY_ORIGINAL_RELEASE_DATE, MTP_TYPE_STR },
  { MTP_PROPERTY_GENRE, MTP_TYPE_STR },
  { MTP_PROPERTY_COMPOSER, MTP_TYPE_STR },
  { MTP_PROPERTY_DURATION, MTP_TYPE_UINT32 },
  { MTP_PROPERTY_DESCRIPTION, MTP_TYPE_STR },
};

static const PropertyTableEntry kDevicePropertyTable[] = {
  { MTP_DEVICE_PROPERTY_SYNCHRONIZATION_PARTNER, MTP_TYPE_STR },
  { MTP_DEVICE_PROPERTY_DEVICE_FRIENDLY_NAME, MTP_TYPE_STR },
  { MTP_DEVICE_PROPERTY_IMAGE_SIZE, MTP_TYPE_STR },
};

bool IMtpDatabase::getObjectPropertyInfo(MtpObjectProperty property, int& type) {
  int count = sizeof(kObjectPropertyTable) / sizeof(kObjectPropertyTable[0]);
  const PropertyTableEntry* entry = kObjectPropertyTable;
  MTPD("IMtpDatabase::getObjectPropertyInfo size is: %i\n", count);
  for (int i = 0; i < count; i++, entry++) {
	if (entry->property == property) {
	  type = entry->type;
	  return true;
	}
  }
  return false;
}

bool IMtpDatabase::getDevicePropertyInfo(MtpDeviceProperty property, int& type) {
  int count = sizeof(kDevicePropertyTable) / sizeof(kDevicePropertyTable[0]);
  const PropertyTableEntry* entry = kDevicePropertyTable;
  MTPD("IMtpDatabase::getDevicePropertyInfo count is: %i\n", count);
  for (int i = 0; i < count; i++, entry++) {
	if (entry->property == property) {
	  type = entry->type;
	  MTPD("type: %x\n", type);
	  return true;
	}
  }
  return false;
}

MtpObjectHandleList* IMtpDatabase::getObjectReferences(MtpObjectHandle handle) {
  // call function and place files with associated handles into int array
  MTPD(
	  "IMtpDatabase::getObjectReferences returning null, this seems to be what Android always "
	  "does.\n");
  MTPD("handle: %d\n", handle);
  // Windows + Android seems to always return a NULL in this function, c == null path
  // The way that this is handled in Android then is to do this:
  return NULL;
}

MtpResponseCode IMtpDatabase::setObjectReferences(__attribute__((unused)) MtpObjectHandle handle,
												  __attribute__((unused))
												  MtpObjectHandleList* references) {
  MTPE("IMtpDatabase::setObjectReferences not implemented, returning 0\n");
  return 0;
}

MtpProperty* IMtpDatabase::getObjectPropertyDesc(MtpObjectProperty property,
												 MtpObjectFormat format) {
  MTPD("IMtpDatabase::getObjectPropertyDesc start\n");
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

MtpProperty* IMtpDatabase::getDevicePropertyDesc(MtpDeviceProperty property) {
  MtpProperty* result = NULL;
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

void IMtpDatabase::sessionStarted() {
  MTPD("IMtpDatabase::sessionStarted not implemented or does nothing, returning\n");
  return;
}

void IMtpDatabase::sessionEnded() {
  MTPD("IMtpDatabase::sessionEnded not implemented or does nothing, returning\n");
  return;
}

// ----------------------------------------------------------------------------

void IMtpDatabase::lockMutex(void) {
  std::map<int, MtpStorage*>::iterator storit;
  for (storit = storagemap.begin(); storit != storagemap.end(); storit++) {
	storit->second->lockMutex(0);
  }
}

void IMtpDatabase::unlockMutex(void) {
  std::map<int, MtpStorage*>::iterator storit;
  for (storit = storagemap.begin(); storit != storagemap.end(); storit++) {
	storit->second->unlockMutex(0);
  }
}

MtpResponseCode IMtpDatabase::beginDeleteObject(MtpObjectHandle handle) {
  MTPD("IMtoDatabase::beginDeleteObject handle: %u\n", handle);
  std::map<int, MtpStorage*>::iterator storit;
  for (storit = storagemap.begin(); storit != storagemap.end(); storit++) {
	if (storit->second->deleteFile(handle) == 0) {
	  MTPD("IMtpDatabase::beginDeleteObject::MTP_RESPONSE_OK\n");
	  return MTP_RESPONSE_OK;
	}
  }
  return MTP_RESPONSE_INVALID_OBJECT_HANDLE;
}

void IMtpDatabase::endDeleteObject(MtpObjectHandle handle __unused, bool succeeded __unused) {
  MTPD("IMtpDatabase::endDeleteObject not implemented yet\n");
}

void IMtpDatabase::rescanFile(const char* path __unused, MtpObjectHandle handle __unused,
							  MtpObjectFormat format __unused) {
  MTPD("IMtpDatabase::rescanFile not implemented yet\n");
}

MtpResponseCode IMtpDatabase::beginMoveObject(MtpObjectHandle handle __unused,
											  MtpObjectHandle newParent __unused,
											  MtpStorageID newStorage __unused) {
  MTPD("IMtpDatabase::beginMoveObject not implemented yet\n");
  return MTP_RESPONSE_INVALID_OBJECT_HANDLE;
}

void IMtpDatabase::endMoveObject(MtpObjectHandle oldParent __unused,
								 MtpObjectHandle newParent __unused,
								 MtpStorageID oldStorage __unused, MtpStorageID newStorage __unused,
								 MtpObjectHandle handle __unused, bool succeeded __unused) {
  MTPD("IMtpDatabase::endMoveObject not implemented yet\n");
}

MtpResponseCode IMtpDatabase::beginCopyObject(MtpObjectHandle handle __unused,
											  MtpObjectHandle newParent __unused,
											  MtpStorageID newStorage __unused) {
  MTPD("IMtpDatabase::beginCopyObject not implemented yet\n");
  return MTP_RESPONSE_INVALID_OBJECT_HANDLE;
}

void IMtpDatabase::endCopyObject(MtpObjectHandle handle __unused, bool succeeded __unused) {
  MTPD("IMtpDatabase::endCopyObject not implemented yet\n");
}
