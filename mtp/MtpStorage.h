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

class MtpDatabase;

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
	std::deque<std::string> mtpParentList;
	int mtpparentid;
    Tree *mtpdbtree;
    typedef std::map<int, Tree*> maptree;
    typedef maptree::iterator iter;
    maptree mtpmap;
	std::string mtpstorageparent;
	pthread_t inotify_thread;
	int inotify_fd;
	int inotify_wd;
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
	int readParentDirs(std::string path);
	int createDB();
	MtpObjectHandleList* getObjectList(MtpStorageID storageID, MtpObjectHandle parent);
	int getObjectInfo(MtpObjectHandle handle, MtpObjectInfo& info);
	MtpObjectHandle beginSendObject(const char* path, MtpObjectFormat format, MtpObjectHandle parent, MtpStorageID storage, uint64_t size, time_t modified);
	int getObjectPropertyList(MtpObjectHandle handle, uint32_t format, uint32_t property, int groupCode, int depth, MtpDataPacket& packet);
	int getObjectFilePath(MtpObjectHandle handle, MtpString& outFilePath, int64_t& outFileLength, MtpObjectFormat& outFormat);
	int deleteFile(MtpObjectHandle handle);
	int renameObject(MtpObjectHandle handle, std::string newName);
	int getObjectPropertyValue(MtpObjectHandle handle, MtpObjectProperty property, uint64_t &longValue);
	void lockMutex(int thread_type);
	void unlockMutex(int thread_type);

private:
	void createEmptyDir(const char* path);
	pthread_t inotify();
	int inotify_t();
	typedef int (MtpStorage::*ThreadPtr)(void);
	typedef void* (*PThreadPtr)(void *);
	std::map<int, std::string> inotifymap;
	int addInotifyDirs(std::string path);
	void deleteTrees(int parent);
	bool sendEvents;
	int getParentObject(std::string parent_path);
	bool use_mutex;
	pthread_mutex_t inMutex; // inotify mutex
	pthread_mutex_t mtpMutex; // main mtp mutex
};

#endif // _MTP_STORAGE_H
