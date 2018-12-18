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

#ifndef _MTP_FFS_COMPAT_HANDLE_H
#define _MTP_FFS_COMPAT_HANDLE_H

#include <MtpFfsHandle.h>

template <class T> class MtpFfsHandleTest;

class MtpFfsCompatHandle : public MtpFfsHandle {
	template <class T> friend class MtpFfsHandleTest;
private:
	int writeHandle(int fd, const void *data, size_t len);
	int readHandle(int fd, void *data, size_t len);

	size_t mMaxWrite;
	size_t mMaxRead;

public:
	int read(void* data, size_t len) override;
	int write(const void* data, size_t len) override;
	int receiveFile(mtp_file_range mfr, bool zero_packet) override;
	int sendFile(mtp_file_range mfr) override;

	/**
	 * Open ffs endpoints and allocate necessary kernel and user memory.
	 * Will sleep until endpoints are enabled, for up to 1 second.
	 */
	int start(bool ptp) override;

	MtpFfsCompatHandle(int controlFd);
	~MtpFfsCompatHandle();
};

#endif // _MTP_FFS_COMPAT_HANDLE_H

