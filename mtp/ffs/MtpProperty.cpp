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

#define LOG_TAG "MtpProperty"

#include <inttypes.h>
#include <cutils/compiler.h>
#include <iomanip>
#include <sstream>
#include <string>

#include "MtpDataPacket.h"
#include "MtpDebug.h"
#include "MtpProperty.h"
#include "MtpStringBuffer.h"
#include "MtpUtils.h"

MtpProperty::MtpProperty()
	:	mCode(0),
		mType(0),
		mWriteable(false),
		mDefaultArrayLength(0),
		mDefaultArrayValues(NULL),
		mCurrentArrayLength(0),
		mCurrentArrayValues(NULL),
		mGroupCode(0),
		mFormFlag(kFormNone),
		mEnumLength(0),
		mEnumValues(NULL)
{
	memset(&mDefaultValue, 0, sizeof(mDefaultValue));
	memset(&mCurrentValue, 0, sizeof(mCurrentValue));
	memset(&mMinimumValue, 0, sizeof(mMinimumValue));
	memset(&mMaximumValue, 0, sizeof(mMaximumValue));
}

MtpProperty::MtpProperty(MtpPropertyCode propCode,
						 MtpDataType type,
						 bool writeable,
						 int defaultValue)
	:	mCode(propCode),
		mType(type),
		mWriteable(writeable),
		mDefaultArrayLength(0),
		mDefaultArrayValues(NULL),
		mCurrentArrayLength(0),
		mCurrentArrayValues(NULL),
		mGroupCode(0),
		mFormFlag(kFormNone),
		mEnumLength(0),
		mEnumValues(NULL)
{
	memset(&mDefaultValue, 0, sizeof(mDefaultValue));
	memset(&mCurrentValue, 0, sizeof(mCurrentValue));
	memset(&mMinimumValue, 0, sizeof(mMinimumValue));
	memset(&mMaximumValue, 0, sizeof(mMaximumValue));

	if (defaultValue) {
		switch (type) {
			case MTP_TYPE_INT8:
				mDefaultValue.u.i8 = defaultValue;
				break;
			case MTP_TYPE_UINT8:
				mDefaultValue.u.u8 = defaultValue;
				break;
			case MTP_TYPE_INT16:
				mDefaultValue.u.i16 = defaultValue;
				break;
			case MTP_TYPE_UINT16:
				mDefaultValue.u.u16 = defaultValue;
				break;
			case MTP_TYPE_INT32:
				mDefaultValue.u.i32 = defaultValue;
				break;
			case MTP_TYPE_UINT32:
				mDefaultValue.u.u32 = defaultValue;
				break;
			case MTP_TYPE_INT64:
				mDefaultValue.u.i64 = defaultValue;
				break;
			case MTP_TYPE_UINT64:
				mDefaultValue.u.u64 = defaultValue;
				break;
			default:
				MTPE("unknown type %04X in MtpProperty::MtpProperty", type);
		}
	}
}

MtpProperty::~MtpProperty() {
	if (mType == MTP_TYPE_STR) {
		// free all strings
		free(mDefaultValue.str);
		free(mCurrentValue.str);
		free(mMinimumValue.str);
		free(mMaximumValue.str);
		if (mDefaultArrayValues) {
			for (uint32_t i = 0; i < mDefaultArrayLength; i++)
				free(mDefaultArrayValues[i].str);
		}
		if (mCurrentArrayValues) {
			for (uint32_t i = 0; i < mCurrentArrayLength; i++)
				free(mCurrentArrayValues[i].str);
		}
		if (mEnumValues) {
			for (uint16_t i = 0; i < mEnumLength; i++)
				free(mEnumValues[i].str);
		}
	}
	delete[] mDefaultArrayValues;
	delete[] mCurrentArrayValues;
	delete[] mEnumValues;
}

bool MtpProperty::read(MtpDataPacket& packet) {
	uint8_t temp8;

	if (!packet.getUInt16(mCode)) return false;
	bool deviceProp = isDeviceProperty();
	if (!packet.getUInt16(mType)) return false;
	if (!packet.getUInt8(temp8)) return false;
	mWriteable = (temp8 == 1);
	switch (mType) {
		case MTP_TYPE_AINT8:
		case MTP_TYPE_AUINT8:
		case MTP_TYPE_AINT16:
		case MTP_TYPE_AUINT16:
		case MTP_TYPE_AINT32:
		case MTP_TYPE_AUINT32:
		case MTP_TYPE_AINT64:
		case MTP_TYPE_AUINT64:
		case MTP_TYPE_AINT128:
		case MTP_TYPE_AUINT128:
			mDefaultArrayValues = readArrayValues(packet, mDefaultArrayLength);
			if (!mDefaultArrayValues) return false;
			if (deviceProp) {
				mCurrentArrayValues = readArrayValues(packet, mCurrentArrayLength);
				if (!mCurrentArrayValues) return false;
			}
			break;
		default:
			if (!readValue(packet, mDefaultValue)) return false;
			if (deviceProp) {
				if (!readValue(packet, mCurrentValue)) return false;
			}
	}
	if (!deviceProp) {
		if (!packet.getUInt32(mGroupCode)) return false;
	}
	if (!packet.getUInt8(mFormFlag)) return false;

	if (mFormFlag == kFormRange) {
			if (!readValue(packet, mMinimumValue)) return false;
			if (!readValue(packet, mMaximumValue)) return false;
			if (!readValue(packet, mStepSize)) return false;
	} else if (mFormFlag == kFormEnum) {
		if (!packet.getUInt16(mEnumLength)) return false;
		mEnumValues = new MtpPropertyValue[mEnumLength];
		for (int i = 0; i < mEnumLength; i++) {
			if (!readValue(packet, mEnumValues[i])) return false;
		}
	}

	return true;
}

void MtpProperty::write(MtpDataPacket& packet) {
	bool deviceProp = isDeviceProperty();

	packet.putUInt16(mCode);
	packet.putUInt16(mType);
	packet.putUInt8(mWriteable ? 1 : 0);

	switch (mType) {
		case MTP_TYPE_AINT8:
		case MTP_TYPE_AUINT8:
		case MTP_TYPE_AINT16:
		case MTP_TYPE_AUINT16:
		case MTP_TYPE_AINT32:
		case MTP_TYPE_AUINT32:
		case MTP_TYPE_AINT64:
		case MTP_TYPE_AUINT64:
		case MTP_TYPE_AINT128:
		case MTP_TYPE_AUINT128:
			writeArrayValues(packet, mDefaultArrayValues, mDefaultArrayLength);
			if (deviceProp)
				writeArrayValues(packet, mCurrentArrayValues, mCurrentArrayLength);
			break;
		default:
			writeValue(packet, mDefaultValue);
			if (deviceProp)
				writeValue(packet, mCurrentValue);
	}
	if (!deviceProp)
		packet.putUInt32(mGroupCode);
	packet.putUInt8(mFormFlag);
	if (mFormFlag == kFormRange) {
			writeValue(packet, mMinimumValue);
			writeValue(packet, mMaximumValue);
			writeValue(packet, mStepSize);
	} else if (mFormFlag == kFormEnum) {
		packet.putUInt16(mEnumLength);
		for (int i = 0; i < mEnumLength; i++)
			writeValue(packet, mEnumValues[i]);
	}
}

void MtpProperty::setDefaultValue(const uint16_t* string) {
	free(mDefaultValue.str);
	if (string) {
		MtpStringBuffer buffer(string);
		mDefaultValue.str = strdup(buffer);
	}
	else
		mDefaultValue.str = NULL;
}

void MtpProperty::setCurrentValue(const uint16_t* string) {
	free(mCurrentValue.str);
	if (string) {
		MtpStringBuffer buffer(string);
		mCurrentValue.str = strdup(buffer);
	}
	else
		mCurrentValue.str = NULL;
}

void MtpProperty::setCurrentValue(MtpDataPacket& packet) {
	free(mCurrentValue.str);
	mCurrentValue.str = NULL;
	readValue(packet, mCurrentValue);
}

void MtpProperty::setFormRange(int min, int max, int step) {
	mFormFlag = kFormRange;
	switch (mType) {
		case MTP_TYPE_INT8:
			mMinimumValue.u.i8 = min;
			mMaximumValue.u.i8 = max;
			mStepSize.u.i8 = step;
			break;
		case MTP_TYPE_UINT8:
			mMinimumValue.u.u8 = min;
			mMaximumValue.u.u8 = max;
			mStepSize.u.u8 = step;
			break;
		case MTP_TYPE_INT16:
			mMinimumValue.u.i16 = min;
			mMaximumValue.u.i16 = max;
			mStepSize.u.i16 = step;
			break;
		case MTP_TYPE_UINT16:
			mMinimumValue.u.u16 = min;
			mMaximumValue.u.u16 = max;
			mStepSize.u.u16 = step;
			break;
		case MTP_TYPE_INT32:
			mMinimumValue.u.i32 = min;
			mMaximumValue.u.i32 = max;
			mStepSize.u.i32 = step;
			break;
		case MTP_TYPE_UINT32:
			mMinimumValue.u.u32 = min;
			mMaximumValue.u.u32 = max;
			mStepSize.u.u32 = step;
			break;
		case MTP_TYPE_INT64:
			mMinimumValue.u.i64 = min;
			mMaximumValue.u.i64 = max;
			mStepSize.u.i64 = step;
			break;
		case MTP_TYPE_UINT64:
			mMinimumValue.u.u64 = min;
			mMaximumValue.u.u64 = max;
			mStepSize.u.u64 = step;
			break;
		default:
			MTPE("unsupported type for MtpProperty::setRange");
			break;
	}
}

void MtpProperty::setFormEnum(const int* values, int count) {
	 mFormFlag = kFormEnum;
	 delete[] mEnumValues;
	 mEnumValues = new MtpPropertyValue[count];
	 mEnumLength = count;

	for (int i = 0; i < count; i++) {
		int value = *values++;
			switch (mType) {
				case MTP_TYPE_INT8:
					mEnumValues[i].u.i8 = value;
					break;
				case MTP_TYPE_UINT8:
					mEnumValues[i].u.u8 = value;
					break;
				case MTP_TYPE_INT16:
					mEnumValues[i].u.i16 = value;
					break;
				case MTP_TYPE_UINT16:
					mEnumValues[i].u.u16 = value;
					break;
				case MTP_TYPE_INT32:
					mEnumValues[i].u.i32 = value;
					break;
				case MTP_TYPE_UINT32:
					mEnumValues[i].u.u32 = value;
					break;
				case MTP_TYPE_INT64:
					mEnumValues[i].u.i64 = value;
					break;
				case MTP_TYPE_UINT64:
					mEnumValues[i].u.u64 = value;
					break;
				default:
					MTPE("unsupported type for MtpProperty::setEnum");
					break;
		}
	}
}

void MtpProperty::setFormDateTime() {
	 mFormFlag = kFormDateTime;
}

void MtpProperty::print() {
	std::string buffer;
	bool deviceProp = isDeviceProperty();
	if (deviceProp)
		MTPD("	  %s (%04X)", MtpDebug::getDevicePropCodeName(mCode), mCode);
	else
		MTPD("	  %s (%04X)", MtpDebug::getObjectPropCodeName(mCode), mCode);
	MTPD("	  type %04X", mType);
	MTPD("	  writeable %s", (mWriteable ? "true" : "false"));
	buffer = "	  default value: ";
	print(mDefaultValue, buffer);
	MTPD("%s", buffer.c_str());
	if (deviceProp) {
		buffer = "	  current value: ";
		print(mCurrentValue, buffer);
		MTPD("%s", buffer.c_str());
	}
	switch (mFormFlag) {
		case kFormNone:
			break;
		case kFormRange:
			buffer = "	  Range (";
			print(mMinimumValue, buffer);
			buffer += ", ";
			print(mMaximumValue, buffer);
			buffer += ", ";
			print(mStepSize, buffer);
			buffer += ")";
			MTPD("%s", buffer.c_str());
			break;
		case kFormEnum:
			buffer = "	  Enum { ";
			for (int i = 0; i < mEnumLength; i++) {
				print(mEnumValues[i], buffer);
				buffer += " ";
			}
			buffer += "}";
			MTPD("%s", buffer.c_str());
			break;
		case kFormDateTime:
			MTPD("	  DateTime\n");
			break;
		default:
			MTPD("	  form %d\n", mFormFlag);
			break;
	}
}

void MtpProperty::print(MtpPropertyValue& value, std::string& buffer) {
	std::ostringstream s;
	switch (mType) {
		case MTP_TYPE_INT8:
			buffer += std::to_string(value.u.i8);
			break;
		case MTP_TYPE_UINT8:
			buffer += std::to_string(value.u.u8);
			break;
		case MTP_TYPE_INT16:
			buffer += std::to_string(value.u.i16);
			break;
		case MTP_TYPE_UINT16:
			buffer += std::to_string(value.u.u16);
			break;
		case MTP_TYPE_INT32:
			buffer += std::to_string(value.u.i32);
			break;
		case MTP_TYPE_UINT32:
			buffer += std::to_string(value.u.u32);
			break;
		case MTP_TYPE_INT64:
			buffer += std::to_string(value.u.i64);
			break;
		case MTP_TYPE_UINT64:
			buffer += std::to_string(value.u.u64);
			break;
		case MTP_TYPE_INT128:
			for (auto i : value.u.i128) {
				s << std::hex << std::setfill('0') << std::uppercase << i;
			}
			buffer += s.str();
			break;
		case MTP_TYPE_UINT128:
			for (auto i : value.u.u128) {
				s << std::hex << std::setfill('0') << std::uppercase << i;
			}
			buffer += s.str();
			break;
		case MTP_TYPE_STR:
			buffer += value.str;
			break;
		default:
			MTPE("unsupported type for MtpProperty::print\n");
			break;
	}
}

bool MtpProperty::readValue(MtpDataPacket& packet, MtpPropertyValue& value) {
	MtpStringBuffer stringBuffer;

	switch (mType) {
		case MTP_TYPE_INT8:
		case MTP_TYPE_AINT8:
			if (!packet.getInt8(value.u.i8)) return false;
			break;
		case MTP_TYPE_UINT8:
		case MTP_TYPE_AUINT8:
			if (!packet.getUInt8(value.u.u8)) return false;
			break;
		case MTP_TYPE_INT16:
		case MTP_TYPE_AINT16:
			if (!packet.getInt16(value.u.i16)) return false;
			break;
		case MTP_TYPE_UINT16:
		case MTP_TYPE_AUINT16:
			if (!packet.getUInt16(value.u.u16)) return false;
			break;
		case MTP_TYPE_INT32:
		case MTP_TYPE_AINT32:
			if (!packet.getInt32(value.u.i32)) return false;
			break;
		case MTP_TYPE_UINT32:
		case MTP_TYPE_AUINT32:
			if (!packet.getUInt32(value.u.u32)) return false;
			break;
		case MTP_TYPE_INT64:
		case MTP_TYPE_AINT64:
			if (!packet.getInt64(value.u.i64)) return false;
			break;
		case MTP_TYPE_UINT64:
		case MTP_TYPE_AUINT64:
			if (!packet.getUInt64(value.u.u64)) return false;
			break;
		case MTP_TYPE_INT128:
		case MTP_TYPE_AINT128:
			if (!packet.getInt128(value.u.i128)) return false;
			break;
		case MTP_TYPE_UINT128:
		case MTP_TYPE_AUINT128:
			if (!packet.getUInt128(value.u.u128)) return false;
			break;
		case MTP_TYPE_STR:
			if (!packet.getString(stringBuffer)) return false;
			value.str = strdup(stringBuffer);
			break;
		default:
			MTPE("unknown type %04X in MtpProperty::readValue", mType);
			return false;
	}
	return true;
}

void MtpProperty::writeValue(MtpDataPacket& packet, MtpPropertyValue& value) {
	MtpStringBuffer stringBuffer;

	switch (mType) {
		case MTP_TYPE_INT8:
		case MTP_TYPE_AINT8:
			packet.putInt8(value.u.i8);
			break;
		case MTP_TYPE_UINT8:
		case MTP_TYPE_AUINT8:
			packet.putUInt8(value.u.u8);
			break;
		case MTP_TYPE_INT16:
		case MTP_TYPE_AINT16:
			packet.putInt16(value.u.i16);
			break;
		case MTP_TYPE_UINT16:
		case MTP_TYPE_AUINT16:
			packet.putUInt16(value.u.u16);
			break;
		case MTP_TYPE_INT32:
		case MTP_TYPE_AINT32:
			packet.putInt32(value.u.i32);
			break;
		case MTP_TYPE_UINT32:
		case MTP_TYPE_AUINT32:
			packet.putUInt32(value.u.u32);
			break;
		case MTP_TYPE_INT64:
		case MTP_TYPE_AINT64:
			packet.putInt64(value.u.i64);
			break;
		case MTP_TYPE_UINT64:
		case MTP_TYPE_AUINT64:
			packet.putUInt64(value.u.u64);
			break;
		case MTP_TYPE_INT128:
		case MTP_TYPE_AINT128:
			packet.putInt128(value.u.i128);
			break;
		case MTP_TYPE_UINT128:
		case MTP_TYPE_AUINT128:
			packet.putUInt128(value.u.u128);
			break;
		case MTP_TYPE_STR:
			if (value.str)
				packet.putString(value.str);
			else
				packet.putEmptyString();
			break;
		default:
			MTPE("unknown type %04X in MtpProperty::writeValue", mType);
	}
}

MtpPropertyValue* MtpProperty::readArrayValues(MtpDataPacket& packet, uint32_t& length) {
	if (!packet.getUInt32(length)) return NULL;

	// Fail if resulting array is over 2GB.  This is because the maximum array
	// size may be less than SIZE_MAX on some platforms.
	if ( CC_UNLIKELY(
			length == 0 ||
			length >= INT32_MAX / sizeof(MtpPropertyValue)) ) {
		length = 0;
		return NULL;
	}
	MtpPropertyValue* result = new MtpPropertyValue[length];
	for (uint32_t i = 0; i < length; i++)
		if (!readValue(packet, result[i])) {
			delete [] result;
			return NULL;
		}
	return result;
}

void MtpProperty::writeArrayValues(MtpDataPacket& packet, MtpPropertyValue* values, uint32_t length) {
	packet.putUInt32(length);
	for (uint32_t i = 0; i < length; i++)
		writeValue(packet, values[i]);
}
