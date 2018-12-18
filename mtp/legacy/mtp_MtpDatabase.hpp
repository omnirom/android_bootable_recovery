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

#ifndef MTP_MTPDATABASE_HPP
#define MTP_MTPDATABASE_HPP

#include <utils/Log.h>

#include <stdio.h>
#include <assert.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/types.h>
#include <map>
#include <string>
#include <deque>

#include "MtpDatabase.h"
#include "MtpDataPacket.h"
#include "MtpObjectInfo.h"
#include "MtpProperty.h"
#include "MtpStringBuffer.h"
#include "MtpUtils.h"
#include "mtp.h"

class MyMtpDatabase : public MtpDatabase {
private:
	int* getSupportedObjectProperties(int format);

    static int FILE_PROPERTIES[10];
	static int DEVICE_PROPERTIES[3];
    static int AUDIO_PROPERTIES[19];
    static int VIDEO_PROPERTIES[15];
    static int IMAGE_PROPERTIES[12];
    static int ALL_PROPERTIES[25];
    static int SUPPORTED_PLAYBACK_FORMATS[26];
	int storagenum;
	int count;
	std::string lastfile;
	std::map<int, MtpStorage*> storagemap;
	void countDirs(std::string path);
	int readParentDirs(std::string path, int storageID);

public:
                                    MyMtpDatabase();
    virtual                         ~MyMtpDatabase();

	void					createDB(MtpStorage* storage, MtpStorageID storageID);
	void					destroyDB(MtpStorageID storageID);
    virtual MtpObjectHandle         beginSendObject(const char* path,
                                            MtpObjectFormat format,
                                            MtpObjectHandle parent,
                                            MtpStorageID storage,
                                            uint64_t size,
                                            time_t modified);

    virtual void                    endSendObject(const char* path,
                                            MtpObjectHandle handle,
                                            MtpObjectFormat format,
                                            bool succeeded);

    virtual MtpObjectHandleList*    getObjectList(MtpStorageID storageID,
                                    MtpObjectFormat format,
                                    MtpObjectHandle parent);

    virtual int                     getNumObjects(MtpStorageID storageID,
                                            MtpObjectFormat format,
                                            MtpObjectHandle parent);

    // callee should delete[] the results from these
    // results can be NULL
    virtual MtpObjectFormatList*    getSupportedPlaybackFormats();
    virtual MtpObjectFormatList*    getSupportedCaptureFormats();
    virtual MtpObjectPropertyList*  getSupportedObjectProperties(MtpObjectFormat format);
    virtual MtpDevicePropertyList*  getSupportedDeviceProperties();

    virtual MtpResponseCode         getObjectPropertyValue(MtpObjectHandle handle,
                                            MtpObjectProperty property,
                                            MtpDataPacket& packet);

    virtual MtpResponseCode         setObjectPropertyValue(MtpObjectHandle handle,
                                            MtpObjectProperty property,
                                            MtpDataPacket& packet);

    virtual MtpResponseCode         getDevicePropertyValue(MtpDeviceProperty property,
                                            MtpDataPacket& packet);

    virtual MtpResponseCode         setDevicePropertyValue(MtpDeviceProperty property,
                                            MtpDataPacket& packet);

    virtual MtpResponseCode         resetDeviceProperty(MtpDeviceProperty property);

    virtual MtpResponseCode         getObjectPropertyList(MtpObjectHandle handle,
                                            uint32_t format, uint32_t property,
                                            int groupCode, int depth,
                                            MtpDataPacket& packet);

    virtual MtpResponseCode         getObjectInfo(MtpObjectHandle handle,
                                            MtpObjectInfo& info);

    virtual void*                   getThumbnail(MtpObjectHandle handle, size_t& outThumbSize);

    virtual MtpResponseCode         getObjectFilePath(MtpObjectHandle handle,
                                            MtpString& outFilePath,
                                            int64_t& outFileLength,
                                            MtpObjectFormat& outFormat);
    virtual MtpResponseCode         deleteFile(MtpObjectHandle handle);

    bool                            getObjectPropertyInfo(MtpObjectProperty property, int& type);
    bool                            getDevicePropertyInfo(MtpDeviceProperty property, int& type);

    virtual MtpObjectHandleList*    getObjectReferences(MtpObjectHandle handle);

    virtual MtpResponseCode         setObjectReferences(MtpObjectHandle handle,
                                            MtpObjectHandleList* references);

    virtual MtpProperty*            getObjectPropertyDesc(MtpObjectProperty property,
                                            MtpObjectFormat format);

    virtual MtpProperty*            getDevicePropertyDesc(MtpDeviceProperty property);

    virtual void                    sessionStarted();

    virtual void                    sessionEnded();
    virtual void                    lockMutex();
    virtual void                    unlockMutex();
};
#endif
