/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Copyright (C) 2014 TeamWin - bigbiff and Dees_Troy mtp database conversion to C++
 */

#ifndef _MTP_STORAGE_H
#define _MTP_STORAGE_H

#include "mtp.h"
#include "MtpObjectInfo.h"
#include <string>
#include <deque>
#include <map>
#include <libgen.h>
#include <pthread.h>
#include "btree.hpp"
#include "MtpServer.h"
#include "../tw_atomic.hpp"

class MtpDatabase;
struct inotify_event;

class MtpStorage {

private:
    MtpStorageID            mStorageID;
    MtpString               mFilePath;
    MtpString               mDescription;
    uint64_t                mMaxCapacity;
    uint64_t                mMaxFileSize;
    // amount of free space to leave unallocated
    uint64_t                mReserveSpace;
    bool                    mRemovable;
	MtpServer*				mServer;
    typedef std::map<int, Tree*> maptree;
    typedef maptree::iterator iter;
    maptree mtpmap;
	std::string mtpstorageparent;
	android::Mutex           mMutex;

public:
                            MtpStorage(MtpStorageID id, const char* filePath,
                                    const char* description, uint64_t reserveSpace,
                                    bool removable, uint64_t maxFileSize, MtpServer* refserver);
    virtual                 ~MtpStorage();

    inline MtpStorageID     getStorageID() const { return mStorageID; }
    int                     getType() const;
    int                     getFileSystemType() const;
    int                     getAccessCapability() const;
    uint64_t                getMaxCapacity();
    uint64_t                getFreeSpace();
    const char*             getDescription() const;
    inline const char*      getPath() const { return (const char *)mFilePath; }
    inline bool             isRemovable() const { return mRemovable; }
    inline uint64_t         getMaxFileSize() const { return mMaxFileSize; }

	struct PropEntry {
		MtpObjectHandle handle;
		uint16_t property;
		uint16_t datatype;
		uint64_t intvalue;
		std::string strvalue;
	};

	int readDir(const std::string& path, Tree* tree);
	int createDB();
	MtpObjectHandleList* getObjectList(MtpStorageID storageID, MtpObjectHandle parent);
	int getObjectInfo(MtpObjectHandle handle, MtpObjectInfo& info);
	MtpObjectHandle beginSendObject(const char* path, MtpObjectFormat format, MtpObjectHandle parent, uint64_t size, time_t modified);
	void endSendObject(const char* path, MtpObjectHandle handle, MtpObjectFormat format, bool succeeded);
	int getObjectPropertyList(MtpObjectHandle handle, uint32_t format, uint32_t property, int groupCode, int depth, MtpDataPacket& packet);
	int getObjectFilePath(MtpObjectHandle handle, MtpString& outFilePath, int64_t& outFileLength, MtpObjectFormat& outFormat);
	int deleteFile(MtpObjectHandle handle);
	int renameObject(MtpObjectHandle handle, std::string newName);
	int getObjectPropertyValue(MtpObjectHandle handle, MtpObjectProperty property, PropEntry& prop);
	void lockMutex(int thread_type);
	void unlockMutex(int thread_type);

private:
	pthread_t inotify();
	int inotify_t();
	typedef int (MtpStorage::*ThreadPtr)(void);
	typedef void* (*PThreadPtr)(void *);
	std::map<int, Tree*> inotifymap;	// inotify wd -> tree
	pthread_t inotify_thread;
	int inotify_fd;
	int addInotify(Tree* tree);
	void handleInotifyEvent(struct inotify_event* event);

	bool sendEvents;
	MtpObjectHandle handleCurrentlySending;

	Node* addNewNode(bool isDir, Tree* tree, const std::string& name);
	Node* findNode(MtpObjectHandle handle);
	Node* findNodeByPath(const std::string& path);
	std::string getNodePath(Node* node);

	void queryNodeProperties(std::vector<PropEntry>& results, Node* node, uint32_t property, int groupCode, MtpStorageID storageID);

	bool use_mutex;
	pthread_mutex_t inMutex; // inotify mutex
	pthread_mutex_t mtpMutex; // main mtp mutex
	TWAtomicInt inotify_thread_kill;
};

#endif // _MTP_STORAGE_H
