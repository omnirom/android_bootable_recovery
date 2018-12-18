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

#ifndef _MTP_STORAGE_INFO_H
#define _MTP_STORAGE_INFO_H

#include "MtpTypes.h"


class MtpDataPacket;

class MtpStorageInfo {
public:
    MtpStorageID        mStorageID;
    uint16_t            mStorageType;
    uint16_t            mFileSystemType;
    uint16_t            mAccessCapability;
    uint64_t            mMaxCapacity;
    uint64_t            mFreeSpaceBytes;
    uint32_t            mFreeSpaceObjects;
    char*               mStorageDescription;
    char*               mVolumeIdentifier;

public:
                        MtpStorageInfo(MtpStorageID id);
    virtual             ~MtpStorageInfo();

    void                read(MtpDataPacket& packet);

    void                print();
};


#endif // _MTP_STORAGE_INFO_H
