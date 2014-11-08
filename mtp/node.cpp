/*
 * Copyright (C) 2014 TeamWin - bigbiff and Dees_Troy mtp database conversion to C++
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <vector>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#include <limits.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <libgen.h>

#include "btree.hpp"
#include "mtp.h"
#include "MtpDebug.h"


Node::Node()
	: handle(-1), parent(0), name("")
{
}

Node::Node(MtpObjectHandle handle, MtpObjectHandle parent, const std::string& name)
	: handle(handle), parent(parent), name(name)
{
}

void Node::rename(const std::string& newName) {
	name = newName;
	updateProperty(MTP_PROPERTY_OBJECT_FILE_NAME, 0, name.c_str(), MTP_TYPE_STR);
	updateProperty(MTP_PROPERTY_NAME, 0, name.c_str(), MTP_TYPE_STR);
	updateProperty(MTP_PROPERTY_DISPLAY_NAME, 0, name.c_str(), MTP_TYPE_STR);
}

MtpObjectHandle Node::Mtpid() const { return handle; }
MtpObjectHandle Node::getMtpParentId() const { return parent; }
const std::string& Node::getName() const { return name; }

uint64_t Node::getIntProperty(MtpPropertyCode property) {
	for (unsigned index = 0; index < mtpProp.size(); ++index) {
		if (mtpProp[index].property == property)
			return mtpProp[index].valueInt;
	}
	MTPE("Node::getIntProperty failed to find property %x, returning -1\n", (unsigned)property);
	return -1;
}

const Node::mtpProperty& Node::getProperty(MtpPropertyCode property) {
	static const mtpProperty dummyProp;
	for (size_t i = 0; i < mtpProp.size(); ++i) {
		if (mtpProp[i].property == property)
			return mtpProp[i];
	}
	MTPE("Node::getProperty failed to find property %x, returning dummy property\n", (unsigned)property);
	return dummyProp;
}

void Node::addProperty(MtpPropertyCode property, uint64_t valueInt, std::string valueStr, MtpDataType dataType) {
//	MTPD("adding property: %lld, valueInt: %lld, valueStr: %s, dataType: %d\n", property, valueInt, valueStr.c_str(), dataType);
	struct mtpProperty prop;
	prop.property = property;
	prop.valueInt = valueInt;
	prop.valueStr = valueStr;
	prop.dataType = dataType;
	mtpProp.push_back(prop);
}

void Node::updateProperty(MtpPropertyCode property, uint64_t valueInt, std::string valueStr, MtpDataType dataType) {
	for (unsigned i = 0; i < mtpProp.size(); i++) {
		if (mtpProp[i].property == property) {
			mtpProp[i].valueInt = valueInt;
			mtpProp[i].valueStr = valueStr;
			mtpProp[i].dataType = dataType;
			return;
		}
	}
	addProperty(property, valueInt, valueStr, dataType);
}

std::vector<Node::mtpProperty>& Node::getMtpProps() {
	return mtpProp;
}

void Node::addProperties(const std::string& path, int storageID) {
	MTPD("addProperties: handle: %u, filename: '%s'\n", handle, getName().c_str());
	struct stat st;
	int mFormat = 0;
	uint64_t puid = ((uint64_t)storageID << 32) + handle;
	off_t file_size = 0;

	mFormat = MTP_FORMAT_UNDEFINED;   // file
	if (lstat(path.c_str(), &st) == 0) {
		file_size = st.st_size;
		if (S_ISDIR(st.st_mode))
			mFormat = MTP_FORMAT_ASSOCIATION; // folder
	}

	// TODO: don't store properties with constant values at all, add them at query time instead
	addProperty(MTP_PROPERTY_STORAGE_ID, storageID, "", MTP_TYPE_UINT32);
	addProperty(MTP_PROPERTY_OBJECT_FORMAT, mFormat, "", MTP_TYPE_UINT16);
	addProperty(MTP_PROPERTY_PROTECTION_STATUS, 0, "", MTP_TYPE_UINT16);
	addProperty(MTP_PROPERTY_OBJECT_SIZE, file_size, "", MTP_TYPE_UINT64);
	addProperty(MTP_PROPERTY_OBJECT_FILE_NAME, 0, getName().c_str(), MTP_TYPE_STR);
	addProperty(MTP_PROPERTY_DATE_MODIFIED, st.st_mtime, "", MTP_TYPE_UINT64);
	addProperty(MTP_PROPERTY_PARENT_OBJECT, parent, "", MTP_TYPE_UINT32);
	addProperty(MTP_PROPERTY_PERSISTENT_UID, puid, "", MTP_TYPE_UINT128);
		// TODO: we can't really support persistent UIDs without a persistent DB.
		// probably a combination of volume UUID + st_ino would come close.
		// doesn't help for fs with no native inodes numbers like fat though...
		// however, Microsoft's own impl (Zune, etc.) does not support persistent UIDs either
	addProperty(MTP_PROPERTY_NAME, 0, getName().c_str(), MTP_TYPE_STR);
	addProperty(MTP_PROPERTY_DISPLAY_NAME, 0, getName().c_str(), MTP_TYPE_STR);
	addProperty(MTP_PROPERTY_DATE_ADDED, st.st_mtime, "", MTP_TYPE_UINT64);
	addProperty(MTP_PROPERTY_DESCRIPTION, 0, "", MTP_TYPE_STR);
	addProperty(MTP_PROPERTY_ARTIST, 0, "", MTP_TYPE_STR);
	addProperty(MTP_PROPERTY_ALBUM_NAME, 0, "", MTP_TYPE_STR);
	addProperty(MTP_PROPERTY_ALBUM_ARTIST, 0, "", MTP_TYPE_STR);
	addProperty(MTP_PROPERTY_TRACK, 0, "", MTP_TYPE_UINT16);
	addProperty(MTP_PROPERTY_ORIGINAL_RELEASE_DATE, 2014, "", MTP_TYPE_UINT64);	// TODO: extract year from st.st_mtime?
	addProperty(MTP_PROPERTY_DURATION, 0, "", MTP_TYPE_UINT32);
	addProperty(MTP_PROPERTY_GENRE, 0, "", MTP_TYPE_STR);
	addProperty(MTP_PROPERTY_COMPOSER, 0, "", MTP_TYPE_STR);
	addProperty(MTP_PROPERTY_ARTIST, 0, "", MTP_TYPE_STR);
	addProperty(MTP_PROPERTY_ALBUM_NAME, 0, "", MTP_TYPE_STR);
	addProperty(MTP_PROPERTY_DURATION, 0, "", MTP_TYPE_UINT32);
	addProperty(MTP_PROPERTY_DESCRIPTION, 0, "", MTP_TYPE_STR);
}
