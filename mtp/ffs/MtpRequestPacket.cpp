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

#define LOG_TAG "MtpRequestPacket"

#include <stdio.h>
#include <sys/types.h>
#include <fcntl.h>

#include "IMtpHandle.h"
#include "MtpDebug.h"
#include "MtpRequestPacket.h"

#include <usbhost/usbhost.h>

MtpRequestPacket::MtpRequestPacket()
	:	MtpPacket(512),
		mParameterCount(0)
{
}

MtpRequestPacket::~MtpRequestPacket() {
}

#ifdef MTP_DEVICE
int MtpRequestPacket::read(IMtpHandle *h) {
	int ret = h->read(mBuffer, mBufferSize);
	if (ret < 0) {
		// file read error
		return ret;
	}

	// request packet should have 12 byte header followed by 0 to 5 32-bit arguments
	const size_t read_size = static_cast<size_t>(ret);
	if (read_size >= MTP_CONTAINER_HEADER_SIZE
			&& read_size <= MTP_CONTAINER_HEADER_SIZE + 5 * sizeof(uint32_t)
			&& ((read_size - MTP_CONTAINER_HEADER_SIZE) & 3) == 0) {
		mPacketSize = read_size;
		mParameterCount = (read_size - MTP_CONTAINER_HEADER_SIZE) / sizeof(uint32_t);
	} else {
		MTPE("Malformed MTP request packet");
		ret = -1;
	}
	return ret;
}
#endif

#ifdef MTP_HOST
	// write our buffer to the given endpoint (host mode)
int MtpRequestPacket::write(struct usb_request *request)
{
	putUInt32(MTP_CONTAINER_LENGTH_OFFSET, mPacketSize);
	putUInt16(MTP_CONTAINER_TYPE_OFFSET, MTP_CONTAINER_TYPE_COMMAND);
	request->buffer = mBuffer;
	request->buffer_length = mPacketSize;
	return transfer(request);
}
#endif
