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

#include <iostream>
#include <vector>
#include <sstream>
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


Node::Node() {
	mtpid= -1;
	path = "";
	left = NULL;
	right = NULL;
	parent = NULL;
	mtpparentid = 0;
}

void Node::setMtpid(int aMtpid) { mtpid = aMtpid; }
void Node::setPath(std::string aPath) { path = aPath; }
void Node::rename(std::string aPath) {
	path = aPath;
	updateProperty(MTP_PROPERTY_OBJECT_FILE_NAME, 0, basename(aPath.c_str()), MTP_TYPE_STR);
	updateProperty(MTP_PROPERTY_NAME, 0, basename(aPath.c_str()), MTP_TYPE_STR);
	updateProperty(MTP_PROPERTY_DISPLAY_NAME, 0, basename(aPath.c_str()), MTP_TYPE_STR);
}
void Node::setLeft(Node* aLeft) { left = aLeft; }
void Node::setRight(Node* aRight) { right = aRight; }
void Node::setParent(Node* aParent) { parent = aParent; }
void Node::setMtpParentId(int id) {
	mtpparentid = id;
	MTPD("setting mtpparentid: %i on mtpid: %i\n", mtpparentid, mtpid);
}
int Node::Mtpid() { return mtpid; }
int Node::getMtpParentId() { return mtpparentid; }
std::string Node::getPath() { return path; }
Node* Node::Left() { return left; }
Node* Node::Right() { return right; }
Node* Node::Parent() { return parent; }

uint64_t Node::getIntProperty(uint64_t property) {
	for (unsigned index = 0; index < mtpProp.size(); ++index) {
		if (mtpProp[index].property == property)
			return mtpProp[index].valueInt;
	}
	MTPE("Node::getIntProperty failed to find property %x, returning -1\n", (unsigned)property);
	return -1;
}

void Node::addProperty(uint64_t property, uint64_t valueInt, std::string valueStr, int dataType) {
	MTPD("adding str property: %lld, valueInt: %lld, valueStr: %s, dataType: %d\n", property, valueInt, valueStr.c_str(), dataType);
	struct mtpProperty prop;
	prop.property = property;
	prop.valueInt = valueInt;
	prop.valueStr = valueStr;
	prop.dataType = dataType;
	mtpProp.push_back(prop);
}

void Node::updateProperty(uint64_t property, uint64_t valueInt, std::string valueStr, int dataType) {
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

void Node::addProperties(int storageID, int parent_object) {
	struct stat st;
	int mFormat = 0;
	uint64_t puid;
	off_t file_size = 0;
	std::string mtimeStr = "00101T000000";

	std::string mtpidStr = static_cast<std::ostringstream*>( &(std::ostringstream() << mtpid) )->str();
	std::string storageIDStr = static_cast<std::ostringstream*>( &(std::ostringstream() << storageID) )->str();
	std::string puidStr = storageIDStr + mtpidStr;
	if ( ! (std::istringstream(puidStr) >> puid) ) puid = 0;
	mFormat = MTP_FORMAT_UNDEFINED;   // file
	if (lstat(getPath().c_str(), &st) == 0) {
		file_size = st.st_size;
		if (S_ISDIR(st.st_mode))
			mFormat = MTP_FORMAT_ASSOCIATION; // folder
		mtimeStr = static_cast<std::ostringstream*>( &(std::ostringstream() << st.st_mtime) )->str();
	}

	addProperty(MTP_PROPERTY_STORAGE_ID, storageID, "", MTP_TYPE_UINT32);
	addProperty(MTP_PROPERTY_OBJECT_FORMAT, mFormat, "", MTP_TYPE_UINT16);
	addProperty(MTP_PROPERTY_PROTECTION_STATUS, 0, "", MTP_TYPE_UINT16);
	addProperty(MTP_PROPERTY_OBJECT_SIZE, file_size, "", MTP_TYPE_UINT64);
	addProperty(MTP_PROPERTY_OBJECT_FILE_NAME, 0, basename(getPath().c_str()), MTP_TYPE_STR);
	MTPD("mtpid: %i, filename: '%s', parent object: %i\n", mtpid, basename(getPath().c_str()), parent_object);
	addProperty(MTP_PROPERTY_DATE_MODIFIED, 0, mtimeStr, MTP_TYPE_STR);
	addProperty(MTP_PROPERTY_PARENT_OBJECT, parent_object, "", MTP_TYPE_UINT32);
	addProperty(MTP_PROPERTY_PERSISTENT_UID, puid, "", MTP_TYPE_UINT128);
	addProperty(MTP_PROPERTY_NAME, 0, basename(getPath().c_str()), MTP_TYPE_STR);
	addProperty(MTP_PROPERTY_DISPLAY_NAME, 0, basename(getPath().c_str()), MTP_TYPE_STR);
	addProperty(MTP_PROPERTY_DATE_ADDED, 0, mtimeStr, MTP_TYPE_STR);
	addProperty(MTP_PROPERTY_DESCRIPTION, 0, "", MTP_TYPE_STR);
	addProperty(MTP_PROPERTY_ARTIST, 0, "", MTP_TYPE_STR);
	addProperty(MTP_PROPERTY_ALBUM_NAME, 0, "", MTP_TYPE_STR);
	addProperty(MTP_PROPERTY_ALBUM_ARTIST, 0, "", MTP_TYPE_STR);
	addProperty(MTP_PROPERTY_TRACK, 0, "", MTP_TYPE_UINT16);
	addProperty(MTP_PROPERTY_ORIGINAL_RELEASE_DATE, 0, "00101T000000", MTP_TYPE_STR);
	addProperty(MTP_PROPERTY_DURATION, 0, "", MTP_TYPE_UINT32);
	addProperty(MTP_PROPERTY_GENRE, 0, "", MTP_TYPE_STR);
	addProperty(MTP_PROPERTY_COMPOSER, 0, "", MTP_TYPE_STR);
	addProperty(MTP_PROPERTY_ARTIST, 0, "", MTP_TYPE_STR);
	addProperty(MTP_PROPERTY_ALBUM_NAME, 0, "", MTP_TYPE_STR);
	addProperty(MTP_PROPERTY_DURATION, 0, "", MTP_TYPE_UINT32);
	addProperty(MTP_PROPERTY_DESCRIPTION, 0, "", MTP_TYPE_STR);
}
