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
#include "MtpDatabase.h"

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
#include "../tw_atomic.hpp"

#define WATCH_FLAGS ( IN_CREATE | IN_DELETE | IN_MOVE | IN_MODIFY )

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
	inotify_thread = 0;
	inotify_fd = -1;
	// Threading has not started yet so we should be safe to set these directly instead of using atomics
	inotify_thread_kill.set_value(0);
	sendEvents = false;
	handleCurrentlySending = 0;
	use_mutex = true;
	if (pthread_mutex_init(&mtpMutex, NULL) != 0) {
		MTPE("Failed to init mtpMutex\n");
		use_mutex = false;
	}
	if (pthread_mutex_init(&inMutex, NULL) != 0) {
		MTPE("Failed to init inMutex\n");
		pthread_mutex_destroy(&mtpMutex);
		use_mutex = false;
	}
}

MtpStorage::~MtpStorage() {
	if (inotify_thread) {
		inotify_thread_kill.set_value(1);
		MTPD("joining inotify_thread after sending the kill notification.\n");
		pthread_join(inotify_thread, NULL); // There's not much we can do if there's an error here
		inotify_thread = 0;
		MTPD("~MtpStorage removing inotify watches and closing inotify_fd\n");
		for (std::map<int, Tree*>::iterator i = inotifymap.begin(); i != inotifymap.end(); i++) {
			inotify_rm_watch(inotify_fd, i->first);
		}
		close(inotify_fd);
		inotifymap.clear();
	}
	// Deleting the root tree causes a cascade in btree.cpp that ends up
	// deleting all of the trees and nodes.
	delete mtpmap[0];
	mtpmap.clear();
	if (use_mutex) {
		use_mutex = false;
		MTPD("~MtpStorage destroying mutexes\n");
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
	// root directory is special: handle 0, parent 0, and empty path
	mtpmap[0] = new Tree(0, 0, "");
	MTPD("MtpStorage::createDB DONE\n");
	if (use_mutex) {
		sendEvents = true;
		MTPD("inotify_init\n");
		inotify_fd = inotify_init();
		if (inotify_fd < 0) {
			MTPE("Can't run inotify_init for mtp server: %s\n", strerror(errno));
		} else {
			MTPD("Starting inotify thread\n");
			inotify_thread = inotify();
		}
	} else {
		MTPD("NOT starting inotify thread\n");
	}
	// for debugging and caching purposes, read the root dir already now
	readDir(mtpstorageparent, mtpmap[0]);
	// all other dirs are read on demand
	return 0;
}

MtpObjectHandleList* MtpStorage::getObjectList(MtpStorageID storageID, MtpObjectHandle parent) {
	MTPD("MtpStorage::getObjectList, parent: %u\n", parent);
	//append object id  (numerical #s) of database to int array
	MtpObjectHandleList* list = new MtpObjectHandleList();
	if (parent == MTP_PARENT_ROOT) {
		MTPD("parent == MTP_PARENT_ROOT\n");
		parent = 0;
	}

	if (mtpmap.find(parent) == mtpmap.end()) {
		MTPE("parent handle not found, returning empty list\n");
		return list;
	}

	Tree* tree = mtpmap[parent];
	if (!tree->wasAlreadyRead())
	{
		std::string path = getNodePath(tree);
		MTPD("reading directory on demand for tree %p (%u), path: %s\n", tree, tree->Mtpid(), path.c_str());
		readDir(path, tree);
	}

	mtpmap[parent]->getmtpids(list);
	MTPD("returning %u objects in %s.\n", list->size(), tree->getName().c_str());
	return list;
}

int MtpStorage::getObjectInfo(MtpObjectHandle handle, MtpObjectInfo& info) {
	struct stat st;
	uint64_t size = 0;
	MTPD("MtpStorage::getObjectInfo, handle: %u\n", handle);
	Node* node = findNode(handle);
	if (!node) {
		// Item is not on this storage device
		return -1;
	}
	
	info.mStorageID = getStorageID();
	MTPD("info.mStorageID: %u\n", info.mStorageID);
	info.mParent = node->getMtpParentId();
	MTPD("mParent: %u\n", info.mParent);
	// TODO: do we want to lstat again here, or read from the node properties?
	if (lstat(getNodePath(node).c_str(), &st) == 0)
		size = st.st_size;
	MTPD("size is: %llu\n", size);
	info.mCompressedSize = (size > 0xFFFFFFFFLL ? 0xFFFFFFFF : size);
	info.mDateModified = st.st_mtime;
	if (S_ISDIR(st.st_mode)) {
		info.mFormat = MTP_FORMAT_ASSOCIATION;
	}
	else {
		info.mFormat = MTP_FORMAT_UNDEFINED;
	}
	info.mName = strdup(node->getName().c_str());
	MTPD("MtpStorage::getObjectInfo found, Exiting getObjectInfo()\n");
	return 0;
}

MtpObjectHandle MtpStorage::beginSendObject(const char* path,
											MtpObjectFormat format,
											MtpObjectHandle parent,
											uint64_t size,
											time_t modified) {
	MTPD("MtpStorage::beginSendObject(), path: '%s', parent: %u, format: %04x\n", path, parent, format);
	iter it = mtpmap.find(parent);
	if (it == mtpmap.end()) {
		MTPE("parent node not found, returning error\n");
		return kInvalidObjectHandle;
	}
	Tree* tree = it->second;

	std::string pathstr(path);
	size_t slashpos = pathstr.find_last_of('/');
	if (slashpos == std::string::npos) {
		MTPE("path has no slash, returning error\n");
		return kInvalidObjectHandle;
	}
	std::string parentdir = pathstr.substr(0, slashpos);
	std::string basename = pathstr.substr(slashpos + 1);
	if (parent != 0 && parentdir != getNodePath(tree)) {
		MTPE("beginSendObject into path '%s' but parent tree has path '%s', returning error\n", parentdir.c_str(), getNodePath(tree).c_str());
		return kInvalidObjectHandle;
	}

	MTPD("MtpStorage::beginSendObject() parentdir: %s basename: %s\n", parentdir.c_str(), basename.c_str());
	// note: for directories, the mkdir call is done later in MtpServer, here we just reserve a handle
	bool isDir = format == MTP_FORMAT_ASSOCIATION;
	Node* node = addNewNode(isDir, tree, basename);
	handleCurrentlySending = node->Mtpid();	// suppress inotify for this node while sending

	return node->Mtpid();
}

void MtpStorage::endSendObject(const char* path, MtpObjectHandle handle, MtpObjectFormat format, bool succeeded)
{
	Node* node = findNode(handle);
	if (!node)
		return;	// just ignore if this is for another storage

	node->addProperties(path, mStorageID);
	handleCurrentlySending = 0;
	// TODO: are we supposed to send an event about an upload by the initiator?
	if (sendEvents)
		mServer->sendObjectAdded(node->Mtpid());
}

int MtpStorage::getObjectFilePath(MtpObjectHandle handle, MtpString& outFilePath, int64_t& outFileLength, MtpObjectFormat& outFormat) {
	MTPD("MtpStorage::getObjectFilePath handle: %u\n", handle);
	Node* node = findNode(handle);
	if (!node)
	{
		// Item is not on this storage device
		return -1;
	}
	// TODO: do we want to lstat here, or just read the info from the node?
	struct stat st;
	if (lstat(getNodePath(node).c_str(), &st) == 0)
		outFileLength = st.st_size;
	else
		outFileLength = 0;
	outFilePath = getNodePath(node).c_str();
	MTPD("outFilePath: %s\n", outFilePath.string());
	outFormat = node->isDir() ? MTP_FORMAT_ASSOCIATION : MTP_FORMAT_UNDEFINED;
	return 0;
}

int MtpStorage::readDir(const std::string& path, Tree* tree)
{
	struct dirent *de;
	int storageID = getStorageID();
	MtpObjectHandle parent = tree->Mtpid();

	DIR *d = opendir(path.c_str());
	MTPD("reading dir '%s', parent handle %u\n", path.c_str(), parent);
	if (d == NULL) {
		MTPE("error opening '%s' -- error: %s\n", path.c_str(), strerror(errno));
		return -1;
	}
	// TODO: for refreshing dirs: capture old entries here
	while ((de = readdir(d)) != NULL) {
		// Because exfat-fuse causes issues with dirent, we will use stat
		// for some things that dirent should be able to do
		std::string item = path + "/" + de->d_name;
		struct stat st;
		if (lstat(item.c_str(), &st)) {
			MTPE("Error running lstat on '%s'\n", item.c_str());
			return -1;
		}
		// TODO: if we want to use this for refreshing dirs too, first find existing name and overwrite
		if (strcmp(de->d_name, ".") == 0)
			continue;
		if (strcmp(de->d_name, "..") == 0)
			continue;
		Node* node = addNewNode(st.st_mode & S_IFDIR, tree, de->d_name);
		node->addProperties(item, storageID);
		//if (sendEvents)
		//	mServer->sendObjectAdded(node->Mtpid());
		//	sending events here makes simple-mtpfs very slow, and it is probably the wrong thing to do anyway
	}
	closedir(d);
	// TODO: for refreshing dirs: remove entries that no longer exist (with their nodes)
	tree->setAlreadyRead(true);
	addInotify(tree);
	return 0;
}

int MtpStorage::deleteFile(MtpObjectHandle handle) {
	MTPD("MtpStorage::deleteFile handle: %u\n", handle);
	Node* node = findNode(handle);
	if (!node) {
		// Item is not on this storage device
		return -1;
	}
	MtpObjectHandle parent = node->getMtpParentId();
	Tree* tree = mtpmap[parent];
	if (!tree) {
		MTPE("parent tree for handle %u not found\n", parent);
		return -1;
	}
	if (node->isDir()) {
		MTPD("deleting tree from mtpmap: %u\n", handle);
		mtpmap.erase(handle);
	}

	MTPD("deleting handle: %u\n", handle);
	tree->deleteNode(handle);
	MTPD("deleted\n");
	return 0;
}

void MtpStorage::queryNodeProperties(std::vector<MtpStorage::PropEntry>& results, Node* node, uint32_t property, int groupCode, MtpStorageID storageID)
{
	MTPD("queryNodeProperties handle %u, path: %s\n", node->Mtpid(), getNodePath(node).c_str());
	PropEntry pe;
	pe.handle = node->Mtpid();
	pe.property = property;

	if (property == 0xffffffff)
	{
		// add all properties
		MTPD("MtpStorage::queryNodeProperties for all properties\n");
		std::vector<Node::mtpProperty> mtpprop = node->getMtpProps();
		for (size_t i = 0; i < mtpprop.size(); ++i) {
			pe.property = mtpprop[i].property;
			pe.datatype = mtpprop[i].dataType;
			pe.intvalue = mtpprop[i].valueInt;
			pe.strvalue = mtpprop[i].valueStr;
			results.push_back(pe);
		}
		return;
	}
	else if (property == 0)
	{
		// TODO: use groupCode
	}

	// single property
	// TODO: this should probably be moved to the Node class and/or merged with getObjectPropertyValue
	switch (property) {
//		case MTP_PROPERTY_OBJECT_FORMAT:
//			pe.datatype = MTP_TYPE_UINT16;
//			pe.intvalue = node->getIntProperty(MTP_PROPERTY_OBJECT_FORMAT);
//			break;

		case MTP_PROPERTY_STORAGE_ID:
			pe.datatype = MTP_TYPE_UINT32;
			pe.intvalue = storageID;
			break;

		case MTP_PROPERTY_PROTECTION_STATUS:
			pe.datatype = MTP_TYPE_UINT16;
			pe.intvalue = 0;
			break;

		case MTP_PROPERTY_OBJECT_SIZE:
		{
			pe.datatype = MTP_TYPE_UINT64;
			struct stat st;
			pe.intvalue = 0;
			if (lstat(getNodePath(node).c_str(), &st) == 0)
				pe.intvalue = st.st_size;
			break;
		}

		default:
		{
			const Node::mtpProperty& prop = node->getProperty(property);
			if (prop.property != property)
			{
				MTPD("queryNodeProperties: unknown property %x\n", property);
				return;
			}
			pe.datatype = prop.dataType;
			pe.intvalue = prop.valueInt;
			pe.strvalue = prop.valueStr;
			// TODO: all the special case stuff in MyMtpDatabase::getObjectPropertyValue is missing here
		}

	}
	results.push_back(pe);
}

int MtpStorage::getObjectPropertyList(MtpObjectHandle handle, uint32_t format, uint32_t property, int groupCode, int depth, MtpDataPacket& packet) {
	MTPD("MtpStorage::getObjectPropertyList handle: %u, format: %x, property: %x\n", handle, format, property);
	if (groupCode != 0)
	{
		MTPE("getObjectPropertyList: groupCode unsupported\n");
		return -1; // TODO: RESPONSE_SPECIFICATION_BY_GROUP_UNSUPPORTED
	}
	// TODO: support all the special stuff, like:
	// handle == 0 -> all objects at the root level
	// handle == 0xffffffff -> all objects (on all storages? how could we support that?)
	// format == 0 -> all formats, otherwise filter by ObjectFormatCode
	// property == 0xffffffff -> all properties except those with group code 0xffffffff
	// if property == 0 then use groupCode
	//   groupCode == 0 -> return Specification_By_Group_Unsupported
	// depth == 0xffffffff -> all objects incl. and below handle

	std::vector<PropEntry> results;

	if (handle == 0xffffffff) {
		// TODO: all object on all storages (needs a different design, result packet needs to be built by server instead of storage)
	} else if (handle == 0)	{
		// all objects at the root level
		Tree* root = mtpmap[0];
		MtpObjectHandleList list;
		root->getmtpids(&list);
		for (MtpObjectHandleList::iterator it = list.begin(); it != list.end(); ++it) {
			Node* node = root->findNode(*it);
			if (!node) {
				MTPE("BUG: node not found for root entry with handle %u\n", *it);
				break;
			}
			queryNodeProperties(results, node, property, groupCode, mStorageID);
		}
	} else {
		// single object
		Node* node = findNode(handle);
		if (!node) {
			// Item is not on this storage device
			return -1;
		}
		queryNodeProperties(results, node, property, groupCode, mStorageID);
	}

	MTPD("count: %u\n", results.size());
	packet.putUInt32(results.size());

	for (size_t i = 0; i < results.size(); ++i) {
		PropEntry& p = results[i];
		MTPD("handle: %u, propertyCode: %x = %s, datatype: %x, value: %llu\n",
				p.handle, p.property, MtpDebug::getObjectPropCodeName(p.property),
				p.datatype, p.intvalue);
		packet.putUInt32(p.handle);
		packet.putUInt16(p.property);
		packet.putUInt16(p.datatype);
		switch (p.datatype) {
			case MTP_TYPE_INT8:
				MTPD("MTP_TYPE_INT8\n");
				packet.putInt8(p.intvalue);
				break;
			case MTP_TYPE_UINT8:
				MTPD("MTP_TYPE_UINT8\n");
				packet.putUInt8(p.intvalue);
				break;
			case MTP_TYPE_INT16:
				MTPD("MTP_TYPE_INT16\n");
				packet.putInt16(p.intvalue);
				break;
			case MTP_TYPE_UINT16:
				MTPD("MTP_TYPE_UINT16\n");
				packet.putUInt16(p.intvalue);
				break;
			case MTP_TYPE_INT32:
				MTPD("MTP_TYPE_INT32\n");
				packet.putInt32(p.intvalue);
				break;
			case MTP_TYPE_UINT32:
				MTPD("MTP_TYPE_UINT32\n");
				packet.putUInt32(p.intvalue);
				break;
			case MTP_TYPE_INT64:
				MTPD("MTP_TYPE_INT64\n");
				packet.putInt64(p.intvalue);
				break;
			case MTP_TYPE_UINT64:
				MTPD("MTP_TYPE_UINT64\n");
				packet.putUInt64(p.intvalue);
				break;
			case MTP_TYPE_INT128:
				MTPD("MTP_TYPE_INT128\n");
				packet.putInt128(p.intvalue);
				break;
			case MTP_TYPE_UINT128:
				MTPD("MTP_TYPE_UINT128\n");
				packet.putUInt128(p.intvalue);
				break;
			case MTP_TYPE_STR:
				MTPD("MTP_TYPE_STR: %s\n", p.strvalue.c_str());
				packet.putString(p.strvalue.c_str());
				break;
			default:
				MTPE("bad or unsupported data type: %x in MyMtpDatabase::getObjectPropertyList", p.datatype);
				break;
		}
	}
	return 0;
}

int MtpStorage::renameObject(MtpObjectHandle handle, std::string newName) {
	MTPD("MtpStorage::renameObject, handle: %u, new name: '%s'\n", handle, newName.c_str());
	if (handle == MTP_PARENT_ROOT) {
		MTPE("parent == MTP_PARENT_ROOT, cannot rename root\n");
		return -1;
	} else {
		for (iter i = mtpmap.begin(); i != mtpmap.end(); i++) {
			Node* node = i->second->findNode(handle);
			if (node != NULL) {
				std::string oldName = getNodePath(node);
				std::string parentdir = oldName.substr(0, oldName.find_last_of('/'));
				std::string newFullName = parentdir + "/" + newName;
				MTPD("old: '%s', new: '%s'\n", oldName.c_str(), newFullName.c_str());
				if (rename(oldName.c_str(), newFullName.c_str()) == 0) {
					node->rename(newName);
					return 0;
				} else {
					MTPE("MtpStorage::renameObject failed, handle: %u, new name: '%s'\n", handle, newName.c_str());
					return -1;
				}
			}
		}
	}
	// handle not found on this storage
	return -1;
}

int MtpStorage::getObjectPropertyValue(MtpObjectHandle handle, MtpObjectProperty property, MtpStorage::PropEntry& pe) {
	Node *node;
	for (iter i = mtpmap.begin(); i != mtpmap.end(); i++) {
		node = i->second->findNode(handle);
		if (node != NULL) {
			const Node::mtpProperty& prop = node->getProperty(property);
			if (prop.property != property) {
				MTPD("getObjectPropertyValue: unknown property %x for handle %u\n", property, handle);
				return -1;
			}
			pe.datatype = prop.dataType;
			pe.intvalue = prop.valueInt;
			pe.strvalue = prop.valueStr;
			pe.handle = handle;
			pe.property = property;
			return 0;
		}
	}
	// handle not found on this storage
	return -1;
}

pthread_t MtpStorage::inotify(void) {
	pthread_t thread;
	pthread_attr_t tattr;

	if (pthread_attr_init(&tattr)) {
		MTPE("Unable to pthread_attr_init\n");
		return 0;
	}
	if (pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_JOINABLE)) {
		MTPE("Error setting pthread_attr_setdetachstate\n");
		return 0;
	}
	ThreadPtr inotifyptr = &MtpStorage::inotify_t;
	PThreadPtr p = *(PThreadPtr*)&inotifyptr;
	pthread_create(&thread, &tattr, p, this);
	if (pthread_attr_destroy(&tattr)) {
		MTPE("Failed to pthread_attr_destroy\n");
	}
	return thread;
}

int MtpStorage::addInotify(Tree* tree) {
	if (inotify_fd < 0) {
		MTPE("inotify_fd not set or error: %i\n", inotify_fd);
		return -1;
	}
	std::string path = getNodePath(tree);
	MTPD("adding inotify for tree %x, dir: %s\n", tree, path.c_str());
	int wd = inotify_add_watch(inotify_fd, path.c_str(), WATCH_FLAGS);
	if (wd < 0) {
		MTPE("inotify_add_watch failed: %s\n", strerror(errno));
		return -1;
	}
	inotifymap[wd] = tree;
	return 0;
}

void MtpStorage::handleInotifyEvent(struct inotify_event* event)
{
	std::map<int, Tree*>::iterator it = inotifymap.find(event->wd);
	if (it == inotifymap.end()) {
		MTPE("Unable to locate inotify_wd: %i\n", event->wd);
		return;
	}
	Tree* tree = it->second;
	MTPD("inotify_t tree: %x '%s'\n", tree, tree->getName().c_str());
	Node* node = tree->findEntryByName(basename(event->name));
	if (node && node->Mtpid() == handleCurrentlySending) {
		MTPD("ignoring inotify event for currently uploading file, handle: %u\n", node->Mtpid());
		return;
	}
	if (event->mask & IN_CREATE || event->mask & IN_MOVED_TO) {
		if (event->mask & IN_ISDIR) {
			MTPD("inotify_t create is dir\n");
		} else {
			MTPD("inotify_t create is file\n");
		}
		if (node == NULL) {
			node = addNewNode(event->mask & IN_ISDIR, tree, event->name);
			std::string item = getNodePath(tree) + "/" + event->name;
			node->addProperties(item, getStorageID());
			mServer->sendObjectAdded(node->Mtpid());
		} else {
			MTPD("inotify_t item already exists.\n");
		}
		if (event->mask & IN_ISDIR) {
			// TODO: do we need to do anything here? probably not until someone reads from the dir...
		}
	} else if (event->mask & IN_DELETE || event->mask & IN_MOVED_FROM) {
		if (event->mask & IN_ISDIR) {
			MTPD("inotify_t Directory %s deleted\n", event->name);
		} else {
			MTPD("inotify_t File %s deleted\n", event->name);
		}
		if (node)
		{
			if (event->mask & IN_ISDIR) {
				for (std::map<int, Tree*>::iterator it = inotifymap.begin(); it != inotifymap.end(); ++it) {
					if (it->second == node) {
						inotify_rm_watch(inotify_fd, it->first);
						MTPD("inotify_t removing watch on '%s'\n", getNodePath(it->second).c_str());
						inotifymap.erase(it->first);
						break;
					}

				}
			}
			MtpObjectHandle handle = node->Mtpid();
			deleteFile(handle);
			mServer->sendObjectRemoved(handle);
		} else {
			MTPD("inotify_t already removed.\n");
		}
	} else if (event->mask & IN_MODIFY) {
		MTPD("inotify_t item %s modified.\n", event->name);
		if (node != NULL) {
			uint64_t orig_size = node->getProperty(MTP_PROPERTY_OBJECT_SIZE).valueInt;
			struct stat st;
			uint64_t new_size = 0;
			if (lstat(getNodePath(node).c_str(), &st) == 0)
				new_size = (uint64_t)st.st_size;
			if (orig_size != new_size) {
				MTPD("size changed from %llu to %llu on mtpid: %u\n", orig_size, new_size, node->Mtpid());
				node->updateProperty(MTP_PROPERTY_OBJECT_SIZE, new_size, "", MTP_TYPE_UINT64);
				mServer->sendObjectUpdated(node->Mtpid());
			}
		} else {
			MTPE("inotify_t modified item not found\n");
		}
	} else if (event->mask & IN_DELETE_SELF || event->mask & IN_MOVE_SELF) {
		// TODO: is this always already handled by IN_DELETE for the parent dir?
	}
}

int MtpStorage::inotify_t(void) {
	#define EVENT_SIZE ( sizeof(struct inotify_event) )
	#define EVENT_BUF_LEN ( 1024 * ( EVENT_SIZE + 16) )
	char buf[EVENT_BUF_LEN];
	fd_set fdset;
	struct timeval seltmout;
	int sel_ret;

	MTPD("inotify thread starting.\n");

	while (inotify_thread_kill.get_value() == 0) {
		FD_ZERO(&fdset);
		FD_SET(inotify_fd, &fdset);
		seltmout.tv_sec = 0;
		seltmout.tv_usec = 25000;
		sel_ret = select(inotify_fd + 1, &fdset, NULL, NULL, &seltmout);
		if (sel_ret == 0)
			continue;
		int i = 0;
		int len = read(inotify_fd, buf, EVENT_BUF_LEN);

		if (len < 0) {
			if (errno == EINTR)
				continue;
			MTPE("inotify_t Can't read inotify events\n");
		}

		while (i < len && inotify_thread_kill.get_value() == 0) {
			struct inotify_event *event = (struct inotify_event *) &buf[i];
			if (event->len) {
				MTPD("inotify event: wd: %i, mask: %x, name: %s\n", event->wd, event->mask, event->name);
				lockMutex(1);
				handleInotifyEvent(event);
				unlockMutex(1);
			}
			i += EVENT_SIZE + event->len;
		}
	}
	MTPD("inotify_thread_kill received!\n");
	// This cleanup is handled in the destructor.
	/*for (std::map<int, Tree*>::iterator i = inotifymap.begin(); i != inotifymap.end(); i++) {
		inotify_rm_watch(inotify_fd, i->first);
	}
	close(inotify_fd);*/
	return 0;
}

Node* MtpStorage::findNodeByPath(const std::string& path) {
	MTPD("findNodeByPath: %s\n", path.c_str());
	std::string match = path.substr(0, mtpstorageparent.size());
	if (match != mtpstorageparent) {
		// not on this device
		MTPD("no match: %s is not on storage %s\n", match.c_str(), mtpstorageparent.c_str());
		return NULL;
	}

	// TODO: fix and test this
	std::string p = path.substr(mtpstorageparent.size()+1);	// cut off "/" after storage root too
	Tree* tree = mtpmap[0]; // start at storage root

	Node* node = NULL;
	while (!p.empty()) {
		size_t slashpos = p.find('/');
		std::string e;
		if (slashpos != std::string::npos) {
			e = p;
			p.clear();
		} else {
			e = p.substr(0, slashpos);
			p = p.substr(slashpos + 1);
		}
		MTPD("path element: %s, rest: %s\n", e.c_str(), p.c_str());
		node = tree->findEntryByName(e);
		if (!node) {
			MTPE("path element of %s not found: %s\n", path.c_str(), e.c_str());
			return NULL;
		}
		if (node->isDir())
			tree = static_cast<Tree*>(node);
		else if (!p.empty()) {
			MTPE("path element of %s is not a directory: %s node: %p\n", path.c_str(), e.c_str(), node);
			return NULL;
		}
	}
	MTPD("findNodeByPath: found node %p, handle: %u, name: %s\n", node, node->Mtpid(), node->getName().c_str());
	return node;
}

Node* MtpStorage::addNewNode(bool isDir, Tree* tree, const std::string& name)
{
	// global counter for new object handles
	static MtpObjectHandle mtpid = 0;

	++mtpid;
	MTPD("adding new %s node for %s, new handle: %u\n", isDir ? "dir" : "file", name.c_str(), mtpid);
	MtpObjectHandle parent = tree->Mtpid();
	MTPD("parent tree: %x, handle: %u, name: %s\n", tree, parent, tree->getName().c_str());
	Node* node;
	if (isDir)
		node = mtpmap[mtpid] = new Tree(mtpid, parent, name);
	else
		node = new Node(mtpid, parent, name);
	tree->addEntry(node);
	return node;
}

Node* MtpStorage::findNode(MtpObjectHandle handle) {
	for (iter i = mtpmap.begin(); i != mtpmap.end(); i++) {
		Node* node = i->second->findNode(handle);
		if (node != NULL) {
			MTPD("findNode: found node %p for handle %u, name: %s\n", node, handle, node->getName().c_str());
			if (node->Mtpid() != handle)
			{
				MTPE("BUG: entry for handle %u points to node with handle %u\n", handle, node->Mtpid());
			}
			return node;
		}
	}
	// Item is not on this storage device
	MTPD("MtpStorage::findNode: no node found for handle %u on storage %u, searched %u trees\n", handle, mStorageID, mtpmap.size());
	return NULL;
}

std::string MtpStorage::getNodePath(Node* node) {
	std::string path;
	MTPD("getNodePath: node %p, handle %u\n", node, node->Mtpid());
	while (node)
	{
		path = "/" + node->getName() + path;
		MtpObjectHandle parent = node->getMtpParentId();
		if (parent == 0)	// root
			break;
		node = findNode(parent);
	}
	path = mtpstorageparent + path;
	MTPD("getNodePath: path %s\n", path.c_str());
	return path;
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
