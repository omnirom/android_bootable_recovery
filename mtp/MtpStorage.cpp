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

#include "MtpDebug.h"
#include "MtpStorage.h"
#include "MtpDataPacket.h"
#include "MtpServer.h"
#include "MtpEventPacket.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <sys/inotify.h>
#include <fcntl.h>
#include <sstream>

#define WATCH_FLAGS ( IN_CREATE | IN_DELETE | IN_MOVE | IN_MODIFY )

static int mtpid = 0;

MtpStorage::MtpStorage(MtpStorageID id, const char* filePath,
		const char* description, uint64_t reserveSpace,
		bool removable, uint64_t maxFileSize, MtpServer* refserver)
	:	mStorageID(id),
		mFilePath(filePath),
		mDescription(description),
		mMaxCapacity(0),
		mMaxFileSize(maxFileSize),
		mReserveSpace(reserveSpace),
		mRemovable(removable),
		mServer(refserver)
{
	MTPI("MtpStorage id: %d path: %s\n", id, filePath);
	mtpparentid = 0;
	inotify_thread = 0;
	sendEvents = false;
	use_mutex = true;
	if (pthread_mutex_init(&mtpMutex, NULL) != 0) {
		MTPE("Failed to init mtpMutex\n");
		use_mutex = false;
	}
	if (pthread_mutex_init(&inMutex, NULL) != 0) {
		MTPE("Failed to init inMutex\n");
		use_mutex = false;
	}
	
}

MtpStorage::~MtpStorage() {
	if (inotify_thread) {
		pthread_kill(inotify_thread, 0);
		for (std::map<int, std::string>::iterator i = inotifymap.begin(); i != inotifymap.end(); i++) {
			inotify_rm_watch(inotify_fd, i->first);
		}
		close(inotify_fd);
	}
	for (iter i = mtpmap.begin(); i != mtpmap.end(); i++) {
		delete i->second;
	}
	if (use_mutex) {
		use_mutex = false;
		pthread_mutex_destroy(&mtpMutex);
		pthread_mutex_destroy(&inMutex);
	}
}

int MtpStorage::getType() const {
	return (mRemovable ? MTP_STORAGE_REMOVABLE_RAM :  MTP_STORAGE_FIXED_RAM);
}

int MtpStorage::getFileSystemType() const {
	return MTP_STORAGE_FILESYSTEM_HIERARCHICAL;
}

int MtpStorage::getAccessCapability() const {
	return MTP_STORAGE_READ_WRITE;
}

uint64_t MtpStorage::getMaxCapacity() {
	if (mMaxCapacity == 0) {
		struct statfs   stat;
		if (statfs(getPath(), &stat))
			return -1;
		mMaxCapacity = (uint64_t)stat.f_blocks * (uint64_t)stat.f_bsize;
	}
	return mMaxCapacity;
}

uint64_t MtpStorage::getFreeSpace() {
	struct statfs   stat;
	if (statfs(getPath(), &stat))
		return -1;
	uint64_t freeSpace = (uint64_t)stat.f_bavail * (uint64_t)stat.f_bsize;
	return (freeSpace > mReserveSpace ? freeSpace - mReserveSpace : 0);
}

const char* MtpStorage::getDescription() const {
	return (const char *)mDescription;
}

int MtpStorage::createDB() {
	std::string mtpParent = "";
	mtpstorageparent = getPath();
	readParentDirs(getPath());
	while (!mtpParentList.empty()) {
		mtpParent = mtpParentList.front();
		mtpParentList.pop_front();
		readParentDirs(mtpParent);
	}
	MTPD("MtpStorage::createDB DONE\n");
	if (use_mutex) {
		MTPD("Starting inotify thread\n");
		sendEvents = true;
		inotify_thread = inotify();
	} else {
		MTPD("NOT starting inotify thread\n");
	}
	return 0;
}

MtpObjectHandleList* MtpStorage::getObjectList(MtpStorageID storageID, MtpObjectHandle parent) {
	std::vector<int> mtpids;
	int local_mtpparentid;
	MTPD("MtpStorage::getObjectList\n");
	MTPD("parent: %d\n", parent);
	//append object id  (numerical #s) of database to int array
	MtpObjectHandleList* list = new MtpObjectHandleList();
	if (parent == MTP_PARENT_ROOT) {
		MTPD("parent == MTP_PARENT_ROOT\n");
		local_mtpparentid = 1;
	}
	else {
		for (iter i = mtpmap.begin(); i != mtpmap.end(); ++i) {
			MTPD("root: %d\n", i->second->Root());
			Node* node = i->second->findNode(parent, i->second->Root());
			if (node != NULL) {
				local_mtpparentid = i->second->getMtpParentId(node);
				MTPD("path: %s\n", i->second->getPath(node).c_str());
				MTPD("mtpparentid: %d going to endloop\n", local_mtpparentid);
				goto endloop;
			}
		}
	}
	MTPD("got to endloop\n");
	endloop:

	if (mtpmap[local_mtpparentid] == NULL) {
		MTPD("mtpmap[mtpparentid] == NULL, returning\n");
		return list;
	}

	MTPD("root: %d\n", mtpmap[local_mtpparentid]->Root());
	mtpmap[local_mtpparentid]->getmtpids(mtpmap[local_mtpparentid]->Root(), &mtpids);
	MTPD("here, mtpids->size(): %i\n", mtpids.size());

	for (unsigned index = 0; index < mtpids.size(); index++) {
		MTPD("mtpidhere[%i]: %d\n", index, mtpids.at(index));
		list->push(mtpids.at(index));
	}
	return list;
}

int MtpStorage::getObjectInfo(MtpObjectHandle handle, MtpObjectInfo& info) {
	struct stat st;
	uint64_t size = 0;
	MTPD("MtpStorage::getObjectInfo handle: %d\n", handle);
	for (iter i = mtpmap.begin(); i != mtpmap.end(); i++) {
		Node* node = i->second->findNode(handle, i->second->Root());
		MTPD("node returned: %d\n", node);
		if (node != NULL) {
				MTPD("found mtpid: %d\n", node->Mtpid());
				info.mStorageID = getStorageID();
				MTPD("info.mStorageID: %d\n", info.mStorageID);
				info.mParent = node->getMtpParentId();
				MTPD("mParent: %d\n", info.mParent);
				if (lstat(node->getPath().c_str(), &st) == 0)
					size = st.st_size;
				MTPD("size is: %llu\n", size);
				info.mCompressedSize = size;//(size > 0xFFFFFFFFLL ? 0xFFFFFFFF : size);
				info.mDateModified = st.st_mtime;
				if (S_ISDIR(st.st_mode)) {
					info.mFormat = MTP_FORMAT_ASSOCIATION;
				}
				else {
					info.mFormat = MTP_FORMAT_UNDEFINED;
				}
				info.mName = strdup(basename(node->getPath().c_str()));
				MTPD("MtpStorage::getObjectInfo found, Exiting getObjectInfo()\n");
				return 0;
		}
	}
	// Item is not on this storage device
	return -1;
}

MtpObjectHandle MtpStorage::beginSendObject(const char* path,
											MtpObjectFormat format,
											MtpObjectHandle parent,
											MtpStorageID storage,
											uint64_t size,
											time_t modified) {
	MTPD("MtpStorage::beginSendObject(), path: '%s', parent: %d, storage: %d, format: %04x\n", path, parent, storage, format);
	Node* node;
	std::string parentdir;
	std::string pathstr(path);
	int parent_id;
	parentdir = pathstr.substr(0, pathstr.find_last_of('/'));
	MTPD("MtpStorage::beginSendObject() parentdir: %s\n", parentdir.c_str());
	if (parentdir.compare(mtpstorageparent) == 0) {
		// root directory
		MTPD("MtpStorage::beginSendObject() root dir\n");
		parent_id = 1;
		++mtpid;
		node = mtpmap[parent_id]->addNode(mtpid, path);
		MTPD("node: %d\n", node);
		node->addProperties(storage, 0);
		if (format == MTP_FORMAT_ASSOCIATION) {
			createEmptyDir(path);
		}
		return mtpid;
	} else {
		for (iter i = mtpmap.begin(); i != mtpmap.end(); i++) {
			node = i->second->findNodePath(parentdir, i->second->Root());
			if (node != NULL) {
				MTPD("mtpid: %d\n", mtpid);
				MTPD("path: %s\n", i->second->getPath(node).c_str());
				parentdir = i->second->getPath(node);
				parent = i->second->getMtpParentId(node);
				if (parent == 0) {
					MTPD("MtpStorage::beginSendObject parent is 0, error.\n");
					return -1;
				} else {
					++mtpid;
					node = mtpmap[parent]->addNode(mtpid, path);
					node->addProperties(getStorageID(), getParentObject(parentdir));
					for (iter i2 = mtpmap.begin(); i2 != mtpmap.end(); i2++) {
						node = i2->second->findNodePath(path, i2->second->Root());
						if (node != NULL) {
							i2->second->setMtpParentId(parent, node);
						}
					}
					if (format == MTP_FORMAT_ASSOCIATION) {
						createEmptyDir(path);
					}
				}
				return mtpid;
			}
		}
	}
	MTPE("MtpStorage::beginSendObject(), path: '%s', parent: %d, storage: %d, format: %04x\n", path, parent, storage, format);
	return -1;
}

int MtpStorage::getObjectFilePath(MtpObjectHandle handle, MtpString& outFilePath, int64_t& outFileLength, MtpObjectFormat& outFormat) {
	struct stat st;
	Node* node;
	MTPD("MtpStorage::getObjectFilePath handle: %i\n", handle);
	for (iter i = mtpmap.begin(); i != mtpmap.end(); i++) {
		MTPD("handle: %d\n", handle);
		node = i->second->findNode(handle, i->second->Root());
		MTPD("node returned: %d\n", node);
		if (node != NULL) {
			if (lstat(node->getPath().c_str(), &st) == 0)
				outFileLength = st.st_size;
			else
				outFileLength = 0;
			outFilePath = strdup(node->getPath().c_str());
			MTPD("outFilePath: %s\n", node->getPath().c_str());
			goto end;
		}
	}
	// Item is not on this storage
	return -1;
end:
	outFormat = MTP_FORMAT_ASSOCIATION;
	return 0;
}

int MtpStorage::readParentDirs(std::string path) {
	struct dirent *de;
	struct stat st;
	DIR *d;
	std::string parent, item, prevparent = "";
	Node* node;
	int storageID = getStorageID();

	d = opendir(path.c_str());
	MTPD("opening '%s'\n", path.c_str());
	if (d == NULL) {
		MTPD("error opening '%s' -- error: %s\n", path.c_str(), strerror(errno));
		closedir(d);
	}
	while ((de = readdir(d)) != NULL) {
		// Because exfat-fuse causes issues with dirent, we will use stat
		// for some things that dirent should be able to do
		item = path + "/" + de->d_name;
		if (lstat(item.c_str(), &st)) {
			MTPE("Error running lstat on '%s'\n", item.c_str());
			return -1;
		}
		if ((st.st_mode & S_IFDIR) && strcmp(de->d_name, ".") == 0)
			continue;
		if ((st.st_mode & S_IFDIR) && strcmp(de->d_name, "..") != 0) {
			// Handle dirs
			MTPD("dir: %s\n", item.c_str());
			mtpParentList.push_back(item);
			parent = item.substr(0, item.find_last_of('/'));
			++mtpid;
			MTPD("parent: %s\n", parent.c_str());
			MTPD("mtpid: %d\n", mtpid);
			if (prevparent != parent) {
				mtpparentid++;
				MTPD("Handle dirs, prevparent != parent, mtpparentid: %d\n", mtpparentid);
				mtpmap[mtpparentid] = new Tree();
				MTPD("prevparent addNode\n");
				for (iter i = mtpmap.begin(); i != mtpmap.end(); ++i) {
					node = i->second->findNodePath(parent, i->second->Root());
					if (node != NULL) {
						i->second->setMtpParentId(mtpparentid, node);
					}
				}
				node = mtpmap[mtpparentid]->addNode(mtpid, item);
				node->addProperties(storageID, getParentObject(path));
				if (sendEvents)
					mServer->sendObjectAdded(mtpid);
			}
			else {
				MTPD("add node\n");
				mtpmap[mtpparentid]->addNode(mtpid, item);
				for (iter i = mtpmap.begin(); i != mtpmap.end(); ++i) {
					node = i->second->findNodePath(item, i->second->Root());
					if (node != NULL) {
						i->second->setMtpParentId(mtpparentid, node);
						node->addProperties(storageID, getParentObject(path));
					}
				}
				if (sendEvents)
					mServer->sendObjectAdded(mtpid);
			}
			prevparent = parent;
		}
		else {
			if (strcmp(de->d_name, "..") != 0) {
				// Handle files
				item = path + "/" + de->d_name;
				MTPD("file: %s\n", item.c_str());
				parent = item.substr(0, item.find_last_of('/'));
				MTPD("parent: %s\n", parent.c_str());
				++mtpid;
				MTPD("mtpid: %d\n", mtpid);
				if (prevparent != parent) {
					mtpparentid++;
					MTPD("mtpparentid1: %d\n", mtpparentid);
					for (iter i = mtpmap.begin(); i != mtpmap.end(); ++i) {
						node = i->second->findNodePath(path, i->second->Root());
						if (node != NULL) {
							i->second->setMtpParentId(mtpparentid, node);
							node->addProperties(storageID, getParentObject(path));
						}
					}
				}
				if (mtpmap[mtpparentid] == NULL) {
					mtpmap[mtpparentid] = new Tree();
				}
				MTPD("blank addNode\n");
				node = mtpmap[mtpparentid]->addNode(mtpid, item);
				node->addProperties(storageID, getParentObject(path));
				prevparent = parent;
				if (sendEvents)
					mServer->sendObjectAdded(mtpid);
			}
			else {
				// Handle empty dirs?
				MTPD("checking for empty dir '%s'\n", path.c_str());
				int count = 0;
				DIR *dirc;
				struct dirent *ep;
				dirc = opendir(path.c_str());
				if (dirc != NULL) {
					while ((ep = readdir(dirc)))
						++count;
					MTPD("count: %d\n", count);
					closedir(dirc);
				}
				if (count == 2) {
					MTPD("'%s' is an empty dir\n", path.c_str());
					createEmptyDir(path.c_str());
					goto end;
				}
			}
		}
	}
	end:
		closedir(d);
		return 0;
}

void MtpStorage::deleteTrees(int parent) {
	Node* node = mtpmap[parent]->Root();
	MTPD("MtpStorage::deleteTrees deleting %i\n", parent);
	while (node != NULL) {
		if (node->getIntProperty(MTP_PROPERTY_OBJECT_FORMAT) == MTP_FORMAT_ASSOCIATION) {
			deleteTrees(node->getMtpParentId());
		}
		node = mtpmap[parent]->getNext(node);
	}
	delete mtpmap[parent];
	mtpmap.erase(parent);
	MTPD("MtpStorage::deleteTrees deleted %i\n", parent);
}

int MtpStorage::deleteFile(MtpObjectHandle handle) {
	int local_parent_id = 0;
	for (iter i = mtpmap.begin(); i != mtpmap.end(); i++) {
		MTPD("MtpStorage::deleteFile handle: %d\n", handle);
		Node* node = i->second->findNode(handle, i->second->Root());
		MTPD("MtpStorage::deleteFile node returned: %d\n", node);
		if (node != NULL) {
			if (node->getIntProperty(MTP_PROPERTY_OBJECT_FORMAT) == MTP_FORMAT_ASSOCIATION) {
				local_parent_id = node->getMtpParentId();
			}
			MTPD("deleting handle: %d\n", handle);
			i->second->deleteNode(handle);
			MTPD("deleted\n");
			goto end;
		}
	}
	return -1;
end:
	if (local_parent_id) {
		deleteTrees(local_parent_id);
	}
	return 0;
}

int MtpStorage::getObjectPropertyList(MtpObjectHandle handle, uint32_t format, uint32_t property, int groupCode, int depth, MtpDataPacket& packet) {
	Node *n;
	int local_mtpid = 0;
	int local_mtpparentid = 0;
	std::vector<int> propertyCodes;
	std::vector<int> dataTypes;
	std::vector<std::string> valueStrs;
	std::vector<int> longValues;
	int count = 0;
	MTPD("MtpStorage::getObjectPropertyList handle: %d, format: %d, property: %lx\n", handle, format, property);
	if (property == MTP_PROPERTY_OBJECT_FORMAT) {
		MTPD("MtpStorage::getObjectPropertyList MTP_PROPERTY_OBJECT_FORMAT\n");
		for (iter i = mtpmap.begin(); i != mtpmap.end(); i++) {
			MTPD("root: %d\n", i->second->Root());
			Node *node = i->second->findNode(handle, i->second->Root());
			MTPD("index: %d\n", index);
			MTPD("node: %d\n", node);
			if (node != NULL) {
				uint64_t longval = node->getIntProperty(MTP_PROPERTY_OBJECT_FORMAT);
				local_mtpparentid = i->second->getMtpParentId(node);
				MTPD("object format longValue: %llu\n", longval);
				propertyCodes.push_back(MTP_PROPERTY_OBJECT_FORMAT);
				longValues.push_back(node->getIntProperty(MTP_PROPERTY_OBJECT_FORMAT));
				valueStrs.push_back("");
				dataTypes.push_back(4);
				count = 1;
				local_mtpid = node->Mtpid();
				goto endloop;
			}
		}
	}
	else if (property == MTP_PROPERTY_STORAGE_ID) {
		MTPD("MtpStorage::getObjectPropertyList MTP_PROPERTY_STORAGE_ID\n");
		for (iter i = mtpmap.begin(); i != mtpmap.end(); i++) {
			MTPD("root: %d\n", i->second->Root());
			Node *node = i->second->findNode(handle, i->second->Root());
			if (node != NULL) {
				propertyCodes.push_back(MTP_PROPERTY_STORAGE_ID);
				longValues.push_back(getStorageID());
				valueStrs.push_back("");
				dataTypes.push_back(4);
				count = 1;
				local_mtpid = node->Mtpid();
				goto endloop;
			}
		}
	}
	else if (property == MTP_PARENT_ROOT) {
		MTPD("MtpStorage::getObjectPropertyList MTP_PARENT_ROOT\n");
		for (iter i = mtpmap.begin(); i != mtpmap.end(); i++) {
			MTPD("root: %d\n", i->second->Root());
			Node* node = i->second->findNode(handle, i->second->Root());
			if (node != NULL) {
				local_mtpparentid = i->second->getMtpParentId(node);
				MTPD("path: %s\n", i->second->getPath(node).c_str());
				MTPD("mtpparentid: %d going to endloop\n", local_mtpparentid);
				std::vector<Node::mtpProperty> mtpprop = node->getMtpProps();
				count = mtpprop.size();
				for (int i = 0; i < count; ++i) {
					propertyCodes.push_back(mtpprop[i].property);
					longValues.push_back(mtpprop[i].valueInt);
					valueStrs.push_back(mtpprop[i].valueStr);
					dataTypes.push_back(mtpprop[i].dataType);
				}
				local_mtpid = node->Mtpid();
				goto endloop;
			}
		}
	}
	else if (property == MTP_PROPERTY_PROTECTION_STATUS) {
		MTPD("MtpStorage::getObjectPropertyList MTP_PROPERTY_PROTECTION_STATUS\n");
		for (iter i = mtpmap.begin(); i != mtpmap.end(); i++) {
			MTPD("root: %d\n", i->second->Root());
			Node *node = i->second->findNode(handle, i->second->Root());
			if (node != NULL) {
				propertyCodes.push_back(MTP_PROPERTY_PROTECTION_STATUS);
				longValues.push_back(0);
				valueStrs.push_back("");
				dataTypes.push_back(8);
				count = 1;
				local_mtpid = node->Mtpid();
				goto endloop;
			}
		}
	}
	else if (property == MTP_PROPERTY_OBJECT_SIZE) {
		MTPD("MtpStorage::getObjectPropertyList MTP_PROPERTY_OBJECT_SIZE\n");
		for (iter i = mtpmap.begin(); i != mtpmap.end(); i++) {
			MTPD("root: %d\n", i->second->Root());
			Node *node = i->second->findNode(handle, i->second->Root());
			if (node != NULL) {
				struct stat st;
				uint64_t size = 0;
				if (lstat(node->getPath().c_str(), &st) == 0)
					size = st.st_size;
				propertyCodes.push_back(MTP_PROPERTY_OBJECT_SIZE);
				longValues.push_back(size);
				valueStrs.push_back("");
				dataTypes.push_back(8);
				count = 1;
				local_mtpid = node->Mtpid();
				goto endloop;
			}
		}
	}
	else {
		// Either the property is not supported or the handle is not on this storage
		return -1;
	}
	// handle not found on this storage
	return -1;

endloop:
	MTPD("mtpparentid: %d\n", local_mtpparentid);
	MTPD("count: %d\n", count);
	packet.putUInt32(count);

	if (count > 0) {
		std::string stringValuesArray;
		for (int i = 0; i < count; ++i) {
			packet.putUInt32(local_mtpid);
			packet.putUInt16(propertyCodes[i]);
			MTPD("dataTypes: %d\n", dataTypes[i]);
			packet.putUInt16(dataTypes[i]);
			MTPD("propertyCode: %s\n", MtpDebug::getObjectPropCodeName(propertyCodes[i]));
			MTPD("longValues: %d\n", longValues[i]);
			switch (dataTypes[i]) {
				case MTP_TYPE_INT8:
					MTPD("MTP_TYPE_INT8\n");
					packet.putInt8(longValues[i]);
					break;
				case MTP_TYPE_UINT8:
					MTPD("MTP_TYPE_UINT8\n");
					packet.putUInt8(longValues[i]);
					break;
				case MTP_TYPE_INT16:
					MTPD("MTP_TYPE_INT16\n");
					packet.putInt16(longValues[i]);
					break;
				case MTP_TYPE_UINT16:
					MTPD("MTP_TYPE_UINT16\n");
					packet.putUInt16(longValues[i]);
					break;
				case MTP_TYPE_INT32:
					MTPD("MTP_TYPE_INT32\n");
					packet.putInt32(longValues[i]);
					break;
				case MTP_TYPE_UINT32:
					MTPD("MTP_TYPE_UINT32\n");
					packet.putUInt32(longValues[i]);
					break;
				case MTP_TYPE_INT64:
					MTPD("MTP_TYPE_INT64\n");
					packet.putInt64(longValues[i]);
					break;
				case MTP_TYPE_UINT64:
					MTPD("MTP_TYPE_UINT64\n");
					packet.putUInt64(longValues[i]);
					break;
				case MTP_TYPE_INT128:
					MTPD("MTP_TYPE_INT128\n");
					packet.putInt128(longValues[i]);
					break;
				case MTP_TYPE_UINT128:
					MTPD("MTP_TYPE_UINT128\n");
					packet.putUInt128(longValues[i]);
					break;
				case MTP_TYPE_STR:
					MTPD("MTP_TYPE_STR: %s\n", valueStrs[i].c_str());
					packet.putString((const char*) valueStrs[i].c_str());
					break;
				default:
					MTPE("bad or unsupported data type: %i in MyMtpDatabase::getObjectPropertyList", dataTypes[i]);
					break;
			}
		}
	}
	return 0;
}

int MtpStorage::renameObject(MtpObjectHandle handle, std::string newName) {
	int index;
	MTPD("MtpStorage::renameObject, handle: %d, new name: '%s'\n", handle, newName.c_str());
	if (handle == MTP_PARENT_ROOT) {
		MTPE("parent == MTP_PARENT_ROOT, cannot rename root\n");
		return -1;
	} else {
		for (iter i = mtpmap.begin(); i != mtpmap.end(); i++) {
			MTPD("root: %d\n", i->second->Root());
			Node* node = i->second->findNode(handle, i->second->Root());
			if (node != NULL) {
				std::string oldName = i->second->getPath(node);
				std::string parentdir = oldName.substr(0, oldName.find_last_of('/'));
				std::string newFullName = parentdir + "/" + newName;
				MTPD("old: '%s', new: '%s'\n", oldName.c_str(), newFullName.c_str());
				if (rename(oldName.c_str(), newFullName.c_str()) == 0) {
					node->rename(newFullName);
					return 0;
				} else {
					MTPE("MtpStorage::renameObject failed, handle: %d, new name: '%s'\n", handle, newName.c_str());
					return -1;
				}
			}
		}
	}
	// handle not found on this storage
	return -1;
}

void MtpStorage::createEmptyDir(const char* path) {
	Node *node;
	++mtpparentid;
	MtpStorageID storage = getStorageID();
	MTPD("MtpStorage::createEmptyDir path: '%s', storage: %i, mtpparentid: %d\n", path, storage, mtpparentid);
	mtpmap[mtpparentid] = new Tree();
	for (iter i = mtpmap.begin(); i != mtpmap.end(); ++i) {
		node = i->second->findNodePath(path, i->second->Root());
		if (node != NULL) {
			mtpmap[mtpparentid]->setMtpParentId(mtpparentid, node);
		}
	}
}

int MtpStorage::getObjectPropertyValue(MtpObjectHandle handle, MtpObjectProperty property, uint64_t &longValue) {
	Node *node;
	for (iter i = mtpmap.begin(); i != mtpmap.end(); i++) {
		node = i->second->findNode(handle, i->second->Root());
		if (node != NULL) {
			longValue = node->getIntProperty(property);
			return 0;
		}
	}
	// handle not found on this storage
	return -1;
}
pthread_t MtpStorage::inotify(void) {
	pthread_t thread;
	ThreadPtr inotifyptr = &MtpStorage::inotify_t;
	PThreadPtr p = *(PThreadPtr*)&inotifyptr;
	pthread_create(&thread, NULL, p, this);
	return thread;
}

int MtpStorage::addInotifyDirs(std::string path) {
	struct dirent *de;
	DIR *d;
	struct stat st;
	std::string inotifypath;

	d = opendir(path.c_str());
	if (d == NULL) {
		MTPE("MtpStorage::addInotifyDirs unable to open '%s'\n", path.c_str());
		closedir(d);
		return -1;
	}

	while ((de = readdir(d)) != NULL) {
		inotifypath = path + "/" + de->d_name;
		if (lstat(inotifypath.c_str(), &st)) {
			MTPE("Error using lstat on '%s'\n", inotifypath.c_str());
			return -1;
		}
		if (!(st.st_mode & S_IFDIR) || strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
			continue;
		if (addInotifyDirs(inotifypath)) {
			closedir(d);
			return -1;
		}
		inotify_wd = inotify_add_watch(inotify_fd, inotifypath.c_str(), WATCH_FLAGS);
		inotifymap[inotify_wd] = inotifypath;
		MTPD("added inotify dir: '%s'\n", inotifypath.c_str());
	}
	closedir(d);
	return 0;
}

int MtpStorage::inotify_t(void) {
	int len, i = 0;
	int local_mtpparentid;
	Node* node = NULL;
	struct stat st;
	#define EVENT_SIZE ( sizeof(struct inotify_event) )
	#define EVENT_BUF_LEN ( 1024 * ( EVENT_SIZE + 16) )
	char buf[EVENT_BUF_LEN];
	std::string item, parent = "";

	MTPD("starting inotify thread\n");
	inotify_fd = inotify_init();

	if (inotify_fd < 0){
		MTPE("Can't run inotify for mtp server\n");
	}

	inotify_wd = inotify_add_watch(inotify_fd, getPath(), WATCH_FLAGS);
	inotifymap[inotify_wd] = getPath();
	if (addInotifyDirs(getPath())) {
		MTPE("MtpStorage::inotify_t failed to add watches to directories\n");
		for (std::map<int, std::string>::iterator i = inotifymap.begin(); i != inotifymap.end(); i++) {
			inotify_rm_watch(inotify_fd, i->first);
		}
		close(inotify_fd);
		return -1;
	}

	while (true) {
		i = 0;
		len = read(inotify_fd, buf, EVENT_BUF_LEN);

		if (len < 0) {
			MTPE("inotify_t Can't read inotify events\n");
		}

		while (i < len) {
			struct inotify_event *event = ( struct inotify_event * ) &buf[ i ];
			if ( event->len ) {
				if (inotifymap[event->wd].empty()) {
					MTPE("Unable to locate inotify_wd: %i\n", event->wd);
					goto end;
				} else {
					item = inotifymap[event->wd];
					item = item	+ "/" + event->name;
					MTPD("inotify_t item: '%s'\n", item.c_str());
					if (event->mask & IN_CREATE || event->mask & IN_MOVED_TO) {
						lockMutex(1);
						if (event->mask & IN_ISDIR) {
							MTPD("inotify_t create is dir\n");
						} else {
							MTPD("inotify_t create is file\n");
						}
						for (iter i = mtpmap.begin(); i != mtpmap.end(); ++i) {
							node = i->second->findNodePath(item, i->second->Root());
							if (node != NULL)
								break;
						}
						if (node == NULL) {
							parent = item.substr(0, item.find_last_of('/'));
							MTPD("parent: %s\n", parent.c_str());
							if (parent == getPath()) {
								local_mtpparentid = 1;
							} else {
								for (iter i = mtpmap.begin(); i != mtpmap.end(); ++i) {
									node = i->second->findNodePath(parent, i->second->Root());
									MTPD("searching for node: %d\n", (int)node);
									if (node != NULL) {
										local_mtpparentid = i->second->getMtpParentId(node);
										break;
									}
								}
								if (node == NULL) {
									MTPE("inotify_t unable to locate mtparentid\n");
									goto end;
								}
							}
							++mtpid;
							MTPD("mtpid: %d\n", mtpid);
							MTPD("mtpparentid1: %d\n", local_mtpparentid);
							node = mtpmap[local_mtpparentid]->addNode(mtpid, item);
							mtpmap[local_mtpparentid]->setMtpParentId(local_mtpparentid, node);
							node->addProperties(getStorageID(), getParentObject(parent));
							if (event->mask & IN_ISDIR) {
								createEmptyDir(item.c_str());
							}
							mServer->sendObjectAdded(mtpid);
						} else {
							MTPD("inotify_t item already exists.\n");
						}
						if (event->mask & IN_ISDIR) {
							inotify_wd = inotify_add_watch(inotify_fd, item.c_str(), WATCH_FLAGS);
							inotifymap[inotify_wd] = item;
							MTPD("added inotify dir: '%s'\n", item.c_str());
							MTPD("inotify_t scanning new dir\n");
							readParentDirs(item);
							std::string mtpParent;
							while (!mtpParentList.empty()) {
								mtpParent = mtpParentList.front();
								mtpParentList.pop_front();
								readParentDirs(mtpParent);
								inotify_wd = inotify_add_watch(inotify_fd, mtpParent.c_str(), WATCH_FLAGS);
								inotifymap[inotify_wd] = mtpParent;
								MTPD("added inotify dir: '%s'\n", mtpParent.c_str());
							}
						}
					} else if (event->mask & IN_DELETE || event->mask & IN_MOVED_FROM) {
						lockMutex(1);
						if (event->mask & IN_ISDIR) {
							MTPD("inotify_t Directory %s deleted\n", event->name);
						} else {
							MTPD("inotify_t File %s deleted\n", event->name);
						}
						for (iter i = mtpmap.begin(); i != mtpmap.end(); ++i) {
							node = i->second->findNodePath(item, i->second->Root());
							if (node != NULL)
								break;
						}
						if (node != NULL && node->Mtpid() > 0) {
							int local_id = node->Mtpid();
							node = NULL;
							deleteFile(local_id);
							mServer->sendObjectRemoved(local_id);
						} else {
							MTPD("inotify_t already removed.\n");
						}
						if (event->mask & IN_ISDIR) {
							std::string orig_item = item + "/";
							size_t item_size = orig_item.size();
							std::string path_check;
							for (std::map<int, std::string>::iterator i = inotifymap.begin(); i != inotifymap.end(); i++) {
								if ((i->second.size() > item_size && i->second.substr(0, item_size) == orig_item) || i->second == item) {
									inotify_rm_watch(inotify_fd, i->first);
									MTPD("inotify_t removing watch on '%s'\n", i->second.c_str());
									inotifymap.erase(i->first);
								}
							}
						}
					} else if (event->mask & IN_MODIFY) {
						MTPD("inotify_t item %s modified.\n", event->name);
						for (iter i = mtpmap.begin(); i != mtpmap.end(); ++i) {
							node = i->second->findNodePath(item, i->second->Root());
							if (node != NULL)
								break;
						}
						if (node != NULL) {
							uint64_t orig_size = node->getIntProperty(MTP_PROPERTY_OBJECT_SIZE);
							struct stat st;
							uint64_t new_size = 0;
							if (lstat(item.c_str(), &st) == 0)
								new_size = (uint64_t)st.st_size;
							if (orig_size != new_size) {
								MTPD("size changed from %llu to %llu on mtpid: %i\n", orig_size, new_size, node->Mtpid());
								node->updateProperty(MTP_PROPERTY_OBJECT_SIZE, new_size, "", MTP_TYPE_UINT64);
								mServer->sendObjectUpdated(node->Mtpid());
							}
						} else {
							MTPE("inotify_t modified item not found\n");
						}
					}
				}
			}
end:
			unlockMutex(1);
			i += EVENT_SIZE + event->len;
		}
	}

	for (std::map<int, std::string>::iterator i = inotifymap.begin(); i != inotifymap.end(); i++) {
		inotify_rm_watch(inotify_fd, i->first);
	}
	close(inotify_fd);
	return 0;
}

int MtpStorage::getParentObject(std::string parent_path) {
	Node* node;
	if (parent_path == getPath()) {
		MTPD("MtpStorage::getParentObject for: '%s' returning: 0 for root\n", parent_path.c_str());
		return 0;
	}
	for (iter i = mtpmap.begin(); i != mtpmap.end(); ++i) {
		node = i->second->findNodePath(parent_path, i->second->Root());
		if (node != NULL) {
			MTPD("MtpStorage::getParentObject for: '%s' returning: %i\n", parent_path.c_str(), node->Mtpid());
			return node->Mtpid();
		}
	}
	MTPE("MtpStorage::getParentObject for: '%s' unable to locate node\n", parent_path.c_str());
	return -1;
}

void MtpStorage::lockMutex(int thread_type) {
	if (!use_mutex)
		return; // mutex is disabled
	if (thread_type) {
		// inotify thread
		pthread_mutex_lock(&inMutex);
		while (pthread_mutex_trylock(&mtpMutex)) {
			pthread_mutex_unlock(&inMutex);
			usleep(32000);
			pthread_mutex_lock(&inMutex);
		}
	} else {
		// main mtp thread
		pthread_mutex_lock(&mtpMutex);
		while (pthread_mutex_trylock(&inMutex)) {
			pthread_mutex_unlock(&mtpMutex);
			usleep(13000);
			pthread_mutex_lock(&mtpMutex);
		}
	}
}

void MtpStorage::unlockMutex(int thread_type) {
	if (!use_mutex)
		return; // mutex is disabled
	pthread_mutex_unlock(&inMutex);
	pthread_mutex_unlock(&mtpMutex);
}
