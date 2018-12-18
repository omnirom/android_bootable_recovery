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

#define LOG_TAG "MtpObjectInfo"

#include "MtpDebug.h"
#include "MtpDataPacket.h"
#include "MtpObjectInfo.h"
#include "MtpStringBuffer.h"
#include "MtpUtils.h"

MtpObjectInfo::MtpObjectInfo(MtpObjectHandle handle)
	:	mHandle(handle),
		mStorageID(0),
		mFormat(0),
		mProtectionStatus(0),
		mCompressedSize(0),
		mThumbFormat(0),
		mThumbCompressedSize(0),
		mThumbPixWidth(0),
		mThumbPixHeight(0),
		mImagePixWidth(0),
		mImagePixHeight(0),
		mImagePixDepth(0),
		mParent(0),
		mAssociationType(0),
		mAssociationDesc(0),
		mSequenceNumber(0),
		mName(NULL),
		mDateCreated(0),
		mDateModified(0),
		mKeywords(NULL)
{
}

MtpObjectInfo::~MtpObjectInfo() {
	if (mName)
		free(mName);
	if (mKeywords)
		free(mKeywords);
}

bool MtpObjectInfo::read(MtpDataPacket& packet) {
	MtpStringBuffer string;
	time_t time;

	if (!packet.getUInt32(mStorageID)) return false;
	if (!packet.getUInt16(mFormat)) return false;
	if (!packet.getUInt16(mProtectionStatus)) return false;
	if (!packet.getUInt32(mCompressedSize)) return false;
	if (!packet.getUInt16(mThumbFormat)) return false;
	if (!packet.getUInt32(mThumbCompressedSize)) return false;
	if (!packet.getUInt32(mThumbPixWidth)) return false;
	if (!packet.getUInt32(mThumbPixHeight)) return false;
	if (!packet.getUInt32(mImagePixWidth)) return false;
	if (!packet.getUInt32(mImagePixHeight)) return false;
	if (!packet.getUInt32(mImagePixDepth)) return false;
	if (!packet.getUInt32(mParent)) return false;
	if (!packet.getUInt16(mAssociationType)) return false;
	if (!packet.getUInt32(mAssociationDesc)) return false;
	if (!packet.getUInt32(mSequenceNumber)) return false;

	if (!packet.getString(string)) return false;
	mName = strdup((const char *)string);
	if (!mName) return false;

	if (!packet.getString(string)) return false;
	if (parseDateTime((const char*)string, time))
		mDateCreated = time;

	if (!packet.getString(string)) return false;
	if (parseDateTime((const char*)string, time))
		mDateModified = time;

	if (!packet.getString(string)) return false;
	mKeywords = strdup((const char *)string);
	if (!mKeywords) return false;

	return true;
}

void MtpObjectInfo::print() {
	MTPD("MtpObject Info %08X: %s\n", mHandle, mName);
	MTPD("	mStorageID: %08X mFormat: %04X mProtectionStatus: %d\n",
			mStorageID, mFormat, mProtectionStatus);
	MTPD("	mCompressedSize: %d mThumbFormat: %04X mThumbCompressedSize: %d\n",
			mCompressedSize, mFormat, mThumbCompressedSize);
	MTPD("	mThumbPixWidth: %d mThumbPixHeight: %d\n", mThumbPixWidth, mThumbPixHeight);
	MTPD("	mImagePixWidth: %d mImagePixHeight: %d mImagePixDepth: %d\n",
			mImagePixWidth, mImagePixHeight, mImagePixDepth);
	MTPD("	mParent: %08X mAssociationType: %04X mAssociationDesc: %04X\n",
			mParent, mAssociationType, mAssociationDesc);
	MTPD("	mSequenceNumber: %d mDateCreated: %ld mDateModified: %ld mKeywords: %s\n",
			mSequenceNumber, mDateCreated, mDateModified, mKeywords);
}
