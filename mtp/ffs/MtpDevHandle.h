/*
 * Copyright (C) 2017 The Android Open Source Project
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

#ifndef _MTP_DEV_HANDLE_H
#define _MTP_DEV_HANDLE_H

#include <android-base/unique_fd.h>
#include "IMtpHandle.h"

class MtpDevHandle : public IMtpHandle {
private:
	android::base::unique_fd mFd;

public:
	MtpDevHandle(int controlFd);
	~MtpDevHandle();
	int read(void *data, size_t len);
	int write(const void *data, size_t len);

	int receiveFile(mtp_file_range mfr, bool);
	int sendFile(mtp_file_range mfr);
	int sendEvent(mtp_event me);

	int start(bool ptp);
	void close();
	bool writeDescriptors(bool usePtp);
};

#endif // _MTP_FFS_HANDLE_H
