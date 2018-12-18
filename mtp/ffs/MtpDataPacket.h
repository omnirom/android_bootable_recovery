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

#ifndef _MTP_DATA_PACKET_H
#define _MTP_DATA_PACKET_H

#include "MtpPacket.h"
#include "mtp.h"

struct usb_device;
struct usb_request;

class IMtpHandle;
class MtpStringBuffer;

class MtpDataPacket : public MtpPacket {
private:
	// current offset for get/put methods
	size_t				mOffset;

public:
						MtpDataPacket();
	virtual				~MtpDataPacket();

	virtual void		reset();

	void				setOperationCode(MtpOperationCode code);
	void				setTransactionID(MtpTransactionID id);

	inline const uint8_t*	  getData() const { return mBuffer + MTP_CONTAINER_HEADER_SIZE; }

	bool				getUInt8(uint8_t& value);
	inline bool			getInt8(int8_t& value) { return getUInt8((uint8_t&)value); }
	bool				getUInt16(uint16_t& value);
	inline bool			getInt16(int16_t& value) { return getUInt16((uint16_t&)value); }
	bool				getUInt32(uint32_t& value);
	inline bool			getInt32(int32_t& value) { return getUInt32((uint32_t&)value); }
	bool				getUInt64(uint64_t& value);
	inline bool			getInt64(int64_t& value) { return getUInt64((uint64_t&)value); }
	bool				getUInt128(uint128_t& value);
	inline bool			getInt128(int128_t& value) { return getUInt128((uint128_t&)value); }
	bool				getString(MtpStringBuffer& string);

	Int8List*			getAInt8();
	UInt8List*			getAUInt8();
	Int16List*			getAInt16();
	UInt16List*			getAUInt16();
	Int32List*			getAInt32();
	UInt32List*			getAUInt32();
	Int64List*			getAInt64();
	UInt64List*			getAUInt64();

	void				putInt8(int8_t value);
	void				putUInt8(uint8_t value);
	void				putInt16(int16_t value);
	void				putUInt16(uint16_t value);
	void				putInt32(int32_t value);
	void				putUInt32(uint32_t value);
	void				putInt64(int64_t value);
	void				putUInt64(uint64_t value);
	void				putInt128(const int128_t& value);
	void				putUInt128(const uint128_t& value);
	void				putInt128(int64_t value);
	void				putUInt128(uint64_t value);

	void				putAInt8(const int8_t* values, int count);
	void				putAUInt8(const uint8_t* values, int count);
	void				putAInt16(const int16_t* values, int count);
	void				putAUInt16(const uint16_t* values, int count);
	void				putAUInt16(const UInt16List* values);
	void				putAInt32(const int32_t* values, int count);
	void				putAUInt32(const uint32_t* values, int count);
	void				putAUInt32(const UInt32List* list);
	void				putAInt64(const int64_t* values, int count);
	void				putAUInt64(const uint64_t* values, int count);
	void				putString(const MtpStringBuffer& string);
	void				putString(const char* string);
	void				putString(const uint16_t* string);
	inline void			putEmptyString() { putUInt8(0); }
	inline void			putEmptyArray() { putUInt32(0); }

#ifdef MTP_DEVICE
	// fill our buffer with data from the given usb handle
	int					read(IMtpHandle *h);

	// write our data to the given usb handle
	int					write(IMtpHandle *h);
	int					writeData(IMtpHandle *h, void* data, uint32_t length);
#endif

#ifdef MTP_HOST
	int					read(struct usb_request *request);
	int					readData(struct usb_request *request, void* buffer, int length);
	int					readDataAsync(struct usb_request *req);
	int					readDataWait(struct usb_device *device);
	int					readDataHeader(struct usb_request *ep);

	// Write a whole data packet with payload to the end point given by a request. |divisionMode|
	// specifies whether to divide header and payload. See |UrbPacketDivisionMode| for meanings of
	// each value. Return the number of bytes (including header size) sent to the device on success.
	// Otherwise -1.
	int					write(struct usb_request *request, UrbPacketDivisionMode divisionMode);
	// Similar to previous write method but it reads the payload from |fd|. If |size| is larger than
	// MTP_BUFFER_SIZE, the data will be sent by multiple bulk transfer requests.
	int					write(struct usb_request *request, UrbPacketDivisionMode divisionMode,
							  int fd, size_t size);
#endif

	inline bool			hasData() const { return mPacketSize > MTP_CONTAINER_HEADER_SIZE; }
	inline uint32_t		getContainerLength() const { return MtpPacket::getUInt32(MTP_CONTAINER_LENGTH_OFFSET); }
	void*				getData(int* outLength) const;
};

#endif // _MTP_DATA_PACKET_H
