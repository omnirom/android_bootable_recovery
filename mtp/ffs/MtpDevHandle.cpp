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
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or		   implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <android-base/logging.h>
#include <cutils/properties.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/usb/ch9.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/endian.h>
#include <unistd.h>

#include "MtpDevHandle.h"

constexpr char mtp_dev_path[] = "/dev/mtp_usb";

MtpDevHandle::MtpDevHandle(int controlFd) {
	mFd.reset(controlFd);
}

MtpDevHandle::~MtpDevHandle() {}

int MtpDevHandle::read(void *data, size_t len) {
	return ::read(mFd, data, len);
}

int MtpDevHandle::write(const void *data, size_t len) {
	return ::write(mFd, data, len);
}

int MtpDevHandle::receiveFile(mtp_file_range mfr, bool) {
	return ioctl(mFd, MTP_RECEIVE_FILE, reinterpret_cast<unsigned long>(&mfr));
}

int MtpDevHandle::sendFile(mtp_file_range mfr) {
	return ioctl(mFd, MTP_SEND_FILE_WITH_HEADER, reinterpret_cast<unsigned long>(&mfr));
}

int MtpDevHandle::sendEvent(mtp_event me) {
	return ioctl(mFd, MTP_SEND_EVENT, reinterpret_cast<unsigned long>(&me));
}

int MtpDevHandle::start(bool /* ptp */) {
	mFd.reset(TEMP_FAILURE_RETRY(open(mtp_dev_path, O_RDWR)));
	if (mFd == -1) return -1;
	return 0;
}

void MtpDevHandle::close() {
	mFd.reset();
}

bool MtpDevHandle::writeDescriptors(bool usePtp) { return usePtp; }
