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
 *
 * Copyright (C) 2014 TeamWin - bigbiff and Dees_Troy mtp database conversion to C++
 */

#ifndef _MTP_EVENT_PACKET_H
#define _MTP_EVENT_PACKET_H

#include "MtpPacket.h"
#include "mtp.h"

class MtpEventPacket : public MtpPacket {

public:
						MtpEventPacket();
	virtual				~MtpEventPacket();

#ifdef MTP_DEVICE
	// write our data to the given file descriptor
	int					write(int fd);
#endif

#ifdef MTP_HOST
	// read our buffer with the given request
	int					read(struct usb_request *request);
#endif

	inline MtpEventCode		getEventCode() const { return getContainerCode(); }
	inline void				setEventCode(MtpEventCode code)
													 { return setContainerCode(code); }
};

#endif // _MTP_EVENT_PACKET_H
