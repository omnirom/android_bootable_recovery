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

#ifndef _MTP_TYPES_H
#define _MTP_TYPES_H

#include <stdint.h>
#include <vector>

typedef int32_t int128_t[4];
typedef uint32_t uint128_t[4];

typedef uint16_t MtpOperationCode;
typedef uint16_t MtpResponseCode;
typedef uint16_t MtpEventCode;
typedef uint32_t MtpSessionID;
typedef uint32_t MtpStorageID;
typedef uint32_t MtpTransactionID;
typedef uint16_t MtpPropertyCode;
typedef uint16_t MtpDataType;
typedef uint16_t MtpObjectFormat;
typedef MtpPropertyCode MtpDeviceProperty;
typedef MtpPropertyCode MtpObjectProperty;

// object handles are unique across all storage but only within a single session.
// object handles cannot be reused after an object is deleted.
// values 0x00000000 and 0xFFFFFFFF are reserved for special purposes.
typedef uint32_t MtpObjectHandle;

// Special values
#define MTP_PARENT_ROOT			0xFFFFFFFF		 // parent is root of the storage
#define kInvalidObjectHandle	0xFFFFFFFF

class MtpStorage;
class MtpDevice;
class MtpProperty;

typedef std::vector<MtpStorage *> MtpStorageList;
typedef std::vector<MtpDevice*> MtpDeviceList;
typedef std::vector<MtpProperty*> MtpPropertyList;

typedef std::vector<uint8_t> UInt8List;
typedef std::vector<uint16_t> UInt16List;
typedef std::vector<uint32_t> UInt32List;
typedef std::vector<uint64_t> UInt64List;
typedef std::vector<int8_t> Int8List;
typedef std::vector<int16_t> Int16List;
typedef std::vector<int32_t> Int32List;
typedef std::vector<int64_t> Int64List;

typedef UInt16List MtpObjectPropertyList;
typedef UInt16List MtpDevicePropertyList;
typedef UInt16List MtpObjectFormatList;
typedef UInt32List MtpObjectHandleList;
typedef UInt16List MtpObjectPropertyList;
typedef UInt32List MtpStorageIDList;

enum UrbPacketDivisionMode {
	// First packet only contains a header.
	FIRST_PACKET_ONLY_HEADER,
	// First packet contains payload much as possible.
	FIRST_PACKET_HAS_PAYLOAD
};

#endif // _MTP_TYPES_H
