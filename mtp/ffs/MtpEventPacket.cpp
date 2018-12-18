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

#define LOG_TAG "MtpEventPacket"

#include <stdio.h>
#include <sys/types.h>
#include <fcntl.h>

#include "IMtpHandle.h"
#include "MtpEventPacket.h"

#include <usbhost/usbhost.h>

MtpEventPacket::MtpEventPacket()
	:	MtpPacket(512)
{
}

MtpEventPacket::~MtpEventPacket() {
}

#ifdef MTP_DEVICE
int MtpEventPacket::write(IMtpHandle *h) {
	struct mtp_event	event;

	putUInt32(MTP_CONTAINER_LENGTH_OFFSET, mPacketSize);
	putUInt16(MTP_CONTAINER_TYPE_OFFSET, MTP_CONTAINER_TYPE_EVENT);

	event.data = mBuffer;
	event.length = mPacketSize;
	int ret = h->sendEvent(event);
	return (ret < 0 ? ret : 0);
}
#endif

#ifdef MTP_HOST
int MtpEventPacket::sendRequest(struct usb_request *request) {
	request->buffer = mBuffer;
	request->buffer_length = mBufferSize;
	mPacketSize = 0;
	if (usb_request_queue(request)) {
		MTPE("usb_endpoint_queue failed, errno: %d", errno);
		return -1;
	}
	return 0;
}

int MtpEventPacket::readResponse(struct usb_device *device) {
	struct usb_request* const req = usb_request_wait(device, -1);
	if (req) {
		mPacketSize = req->actual_length;
		return req->actual_length;
	} else {
		return -1;
	}
}
#endif
