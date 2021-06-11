/*
 * Copyright (C) 2016 The Android Open Source Project
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
#ifndef _IMTP_HANDLE_H
#define _IMTP_HANDLE_H

#include <linux/usb/f_mtp.h>

class IMtpHandle {
public:
	// Return number of bytes read/written, or -1 and errno is set
	virtual int read(void *data, size_t len) = 0;
	virtual int write(const void *data, size_t len) = 0;

	// Return 0 if send/receive is successful, or -1 and errno is set
	virtual int receiveFile(mtp_file_range mfr, bool zero_packet) = 0;
	virtual int sendFile(mtp_file_range mfr) = 0;
	virtual int sendEvent(mtp_event me) = 0;

	// Return 0 if operation is successful, or -1 else
	virtual int start(bool ptp) = 0;

	virtual bool writeDescriptors(bool ptp) = 0;

	virtual void close() = 0;

	virtual ~IMtpHandle() {}
};

#endif // _IMTP_HANDLE_H

