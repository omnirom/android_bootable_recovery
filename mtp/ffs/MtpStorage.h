/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *		http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _MTP_STORAGE_H
#define _MTP_STORAGE_H

#include "MtpObjectInfo.h"
#include "MtpServer.h"
#include "MtpStringBuffer.h"
#include "MtpTypes.h"
#include "mtp.h"
#include "btree.hpp"
#include "tw_atomic.hpp"

class MtpDatabase;

class MtpStorage {

public:
	struct PropEntry {
		   MtpObjectHandle handle;
		   uint16_t property;
		   uint16_t datatype;
		   uint64_t intvalue;
		   std::string strvalue;
	};

private:
	MtpStorageID			mStorageID;
	MtpStringBuffer			mFilePath;
	MtpStringBuffer			mDescription;
	uint64_t				mMaxCapacity;
	uint64_t				mMaxFileSize;
	bool					mRemovable;
	typedef					std::map<int, Tree*> maptree;
	typedef					maptree::iterator iter;
	maptree					mtpmap;
	std::string				mtpstorageparent;
	MtpObjectHandle			handleCurrentlySending;
	int						inotify_fd;
	std::map<int, Tree*>	inotifymap;		   // inotify wd -> tree
	bool					sendEvents;
	MtpServer*				mServer;
	typedef					int (MtpStorage::*ThreadPtr)(void);
	typedef					void* (*PThreadPtr)(void *);
	bool					use_mutex;
	pthread_mutex_t			inMutex; // inotify mutex
	pthread_mutex_t			mtpMutex; // main mtp mutex
	TWAtomicInt				inotify_thread_kill;
	pthread_t				inotify_thread;
	Node*					findNode(MtpObjectHandle handle);
	std::string				getNodePath(Node* node);
	Node*					addNewNode(bool isDir, Tree* tree, const std::string& name);
	void					queryNodeProperties(std::vector<PropEntry>& results, Node* node, uint32_t property, int groupCode, MtpStorageID storageID);
	int						addInotify(Tree* tree);
	void					handleInotifyEvent(struct inotify_event* event);

public:
	MtpStorage(MtpStorageID id, const char* filePath,
								const char* description,
								bool removable, uint64_t maxFileSize, MtpServer* refserver);
	virtual					~MtpStorage();
	inline MtpStorageID		getStorageID() const { return mStorageID; }
	int						getType() const;
	int						getFileSystemType() const;
	int						getAccessCapability() const;
	uint64_t				getMaxCapacity();
	uint64_t				getFreeSpace();
	const char*				getDescription() const;
	inline const char*		getPath() const { return (const char *)mFilePath; }
	inline bool				isRemovable() const { return mRemovable; }
	inline uint64_t			getMaxFileSize() const { return mMaxFileSize; }
	int						renameObject(MtpObjectHandle handle, std::string newName);
	MtpObjectHandle			beginSendObject(const char* path, MtpObjectFormat format, MtpObjectHandle parent, uint64_t size, time_t modified);
	MtpObjectHandleList*	getObjectList(MtpStorageID storageID, MtpObjectHandle parent);
	int						getObjectPropertyList(MtpObjectHandle handle, uint32_t format, uint32_t property, int groupCode, int depth, MtpDataPacket& packet);
	int						readDir(const std::string& path, Tree* tree);
	int						getObjectPropertyValue(MtpObjectHandle handle, MtpObjectProperty property, PropEntry& prop);
	int						getObjectInfo(MtpObjectHandle handle, MtpObjectInfo& info);
	void					endSendObject(const char* path, MtpObjectHandle handle, MtpObjectFormat format, bool succeeded);
	int						getObjectFilePath(MtpObjectHandle handle, MtpStringBuffer& outFilePath, int64_t& outFileLength, MtpObjectFormat& outFormat);
	int						deleteFile(MtpObjectHandle handle);
	int						createDB();
	pthread_t				inotify();
	int						inotify_t();
	void					lockMutex(int thread_type);
	void					unlockMutex(int thread_type);
};

#endif // _MTP_STORAGE_H
