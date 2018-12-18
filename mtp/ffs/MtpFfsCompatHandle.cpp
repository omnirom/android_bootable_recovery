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

#include <android-base/logging.h>
#include <android-base/properties.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/usb/ch9.h>
#include <linux/usb/functionfs.h>
#include <mutex>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/endian.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "PosixAsyncIO.h"
#include "MtpFfsCompatHandle.h"
#include "mtp.h"

#define FUNCTIONFS_ENDPOINT_ALLOC		_IOR('g', 231, __u32)

namespace {

// Must be divisible by all max packet size values
constexpr int MAX_FILE_CHUNK_SIZE = 3145728;

// Safe values since some devices cannot handle large DMAs
// To get good performance, override these with
// higher values per device using the properties
// sys.usb.ffs.max_read and sys.usb.ffs.max_write
constexpr int USB_FFS_MAX_WRITE = MTP_BUFFER_SIZE;
constexpr int USB_FFS_MAX_READ = MTP_BUFFER_SIZE;

static_assert(USB_FFS_MAX_WRITE > 0, "Max r/w values must be > 0!");
static_assert(USB_FFS_MAX_READ > 0, "Max r/w values must be > 0!");

constexpr unsigned int MAX_MTP_FILE_SIZE = 0xFFFFFFFF;

constexpr size_t ENDPOINT_ALLOC_RETRIES = 10;

} // anonymous namespace


MtpFfsCompatHandle::MtpFfsCompatHandle(int controlFd) :
	MtpFfsHandle(controlFd),
	mMaxWrite(USB_FFS_MAX_WRITE),
	mMaxRead(USB_FFS_MAX_READ) {}

MtpFfsCompatHandle::~MtpFfsCompatHandle() {}

int MtpFfsCompatHandle::writeHandle(int fd, const void* data, size_t len) {
	int ret = 0;
	const char* buf = static_cast<const char*>(data);
	while (len > 0) {
		int write_len = std::min(mMaxWrite, len);
		int n = TEMP_FAILURE_RETRY(::write(fd, buf, write_len));

		if (n < 0) {
			PLOG(ERROR) << "write ERROR: fd = " << fd << ", n = " << n;
			return -1;
		} else if (n < write_len) {
			errno = EIO;
			PLOG(ERROR) << "less written than expected";
			return -1;
		}
		buf += n;
		len -= n;
		ret += n;
	}
	return ret;
}

int MtpFfsCompatHandle::readHandle(int fd, void* data, size_t len) {
	int ret = 0;
	char* buf = static_cast<char*>(data);
	while (len > 0) {
		int read_len = std::min(mMaxRead, len);
		int n = TEMP_FAILURE_RETRY(::read(fd, buf, read_len));
		if (n < 0) {
			PLOG(ERROR) << "read ERROR: fd = " << fd << ", n = " << n;
			return -1;
		}
		ret += n;
		if (n < read_len) // done reading early
			break;
		buf += n;
		len -= n;
	}
	return ret;
}

int MtpFfsCompatHandle::start(bool ptp) {
	if (!openEndpoints(ptp))
		return -1;

	for (unsigned i = 0; i < NUM_IO_BUFS; i++) {
		mIobuf[i].bufs.resize(MAX_FILE_CHUNK_SIZE);
		posix_madvise(mIobuf[i].bufs.data(), MAX_FILE_CHUNK_SIZE,
				POSIX_MADV_SEQUENTIAL | POSIX_MADV_WILLNEED);
	}

	// Get device specific r/w size
	mMaxWrite = android::base::GetIntProperty("sys.usb.ffs.max_write", USB_FFS_MAX_WRITE);
	mMaxRead = android::base::GetIntProperty("sys.usb.ffs.max_read", USB_FFS_MAX_READ);

	size_t attempts = 0;
	while (mMaxWrite >= USB_FFS_MAX_WRITE && mMaxRead >= USB_FFS_MAX_READ &&
			attempts < ENDPOINT_ALLOC_RETRIES) {
		// If larger contiguous chunks of memory aren't available, attempt to try
		// smaller allocations.
		if (ioctl(mBulkIn, FUNCTIONFS_ENDPOINT_ALLOC, static_cast<__u32>(mMaxWrite)) ||
			ioctl(mBulkOut, FUNCTIONFS_ENDPOINT_ALLOC, static_cast<__u32>(mMaxRead))) {
			if (errno == ENODEV) {
				// Driver hasn't enabled endpoints yet.
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				attempts += 1;
				continue;
			}
			mMaxWrite /= 2;
			mMaxRead /=2;
		} else {
			return 0;
		}
	}
	// Try to start MtpServer anyway, with the smallest max r/w values
	mMaxWrite = USB_FFS_MAX_WRITE;
	mMaxRead = USB_FFS_MAX_READ;
	PLOG(ERROR) << "Functionfs could not allocate any memory!";
	return 0;
}

int MtpFfsCompatHandle::read(void* data, size_t len) {
	return readHandle(mBulkOut, data, len);
}

int MtpFfsCompatHandle::write(const void* data, size_t len) {
	return writeHandle(mBulkIn, data, len);
}

int MtpFfsCompatHandle::receiveFile(mtp_file_range mfr, bool zero_packet) {
	// When receiving files, the incoming length is given in 32 bits.
	// A >4G file is given as 0xFFFFFFFF
	uint32_t file_length = mfr.length;
	uint64_t offset = mfr.offset;
	int packet_size = getPacketSize(mBulkOut);

	unsigned char *data = mIobuf[0].bufs.data();
	unsigned char *data2 = mIobuf[1].bufs.data();

	struct aiocb aio;
	aio.aio_fildes = mfr.fd;
	aio.aio_buf = nullptr;
	struct aiocb *aiol[] = {&aio};
	int ret = -1;
	size_t length;
	bool read = false;
	bool write = false;

	posix_fadvise(mfr.fd, 0, 0, POSIX_FADV_SEQUENTIAL | POSIX_FADV_NOREUSE);

	// Break down the file into pieces that fit in buffers
	while (file_length > 0 || write) {
		if (file_length > 0) {
			length = std::min(static_cast<uint32_t>(MAX_FILE_CHUNK_SIZE), file_length);

			// Read data from USB, handle errors after waiting for write thread.
			ret = readHandle(mBulkOut, data, length);

			if (file_length != MAX_MTP_FILE_SIZE && ret < static_cast<int>(length)) {
				ret = -1;
				errno = EIO;
			}
			read = true;
		}

		if (write) {
			// get the return status of the last write request
			aio_suspend(aiol, 1, nullptr);

			int written = aio_return(&aio);
			if (written == -1) {
				errno = aio_error(&aio);
				return -1;
			}
			if (static_cast<size_t>(written) < aio.aio_nbytes) {
				errno = EIO;
				return -1;
			}
			write = false;
		}

		// If there was an error reading above
		if (ret == -1) {
			return -1;
		}

		if (read) {
			if (file_length == MAX_MTP_FILE_SIZE) {
				// For larger files, receive until a short packet is received.
				if (static_cast<size_t>(ret) < length) {
					file_length = 0;
				}
			} else {
				file_length -= ret;
			}
			// Enqueue a new write request
			aio_prepare(&aio, data, length, offset);
			aio_write(&aio);

			offset += ret;
			std::swap(data, data2);

			write = true;
			read = false;
		}
	}
	// Receive an empty packet if size is a multiple of the endpoint size.
	if (ret % packet_size == 0 || zero_packet) {
		if (TEMP_FAILURE_RETRY(::read(mBulkOut, data, packet_size)) != 0) {
			return -1;
		}
	}
	return 0;
}

int MtpFfsCompatHandle::sendFile(mtp_file_range mfr) {
	uint64_t file_length = mfr.length;
	uint32_t given_length = std::min(static_cast<uint64_t>(MAX_MTP_FILE_SIZE),
			file_length + sizeof(mtp_data_header));
	uint64_t offset = mfr.offset;
	int packet_size = getPacketSize(mBulkIn);

	// If file_length is larger than a size_t, truncating would produce the wrong comparison.
	// Instead, promote the left side to 64 bits, then truncate the small result.
	int init_read_len = std::min(
			static_cast<uint64_t>(packet_size - sizeof(mtp_data_header)), file_length);

	unsigned char *data = mIobuf[0].bufs.data();
	unsigned char *data2 = mIobuf[1].bufs.data();

	posix_fadvise(mfr.fd, 0, 0, POSIX_FADV_SEQUENTIAL | POSIX_FADV_NOREUSE);

	struct aiocb aio;
	aio.aio_fildes = mfr.fd;
	struct aiocb *aiol[] = {&aio};
	int ret, length;
	int error = 0;
	bool read = false;
	bool write = false;

	// Send the header data
	mtp_data_header *header = reinterpret_cast<mtp_data_header*>(data);
	header->length = htole32(given_length);
	header->type = htole16(2); /* data packet */
	header->command = htole16(mfr.command);
	header->transaction_id = htole32(mfr.transaction_id);

	// Some hosts don't support header/data separation even though MTP allows it
	// Handle by filling first packet with initial file data
	if (TEMP_FAILURE_RETRY(pread(mfr.fd, reinterpret_cast<char*>(data) +
					sizeof(mtp_data_header), init_read_len, offset))
			!= init_read_len) return -1;
	if (writeHandle(mBulkIn, data, sizeof(mtp_data_header) + init_read_len) == -1) return -1;
	file_length -= init_read_len;
	offset += init_read_len;
	ret = init_read_len + sizeof(mtp_data_header);

	// Break down the file into pieces that fit in buffers
	while (file_length > 0) {
		if (read) {
			// Wait for the previous read to finish
			aio_suspend(aiol, 1, nullptr);
			ret = aio_return(&aio);
			if (ret == -1) {
				errno = aio_error(&aio);
				return -1;
			}
			if (static_cast<size_t>(ret) < aio.aio_nbytes) {
				errno = EIO;
				return -1;
			}

			file_length -= ret;
			offset += ret;
			std::swap(data, data2);
			read = false;
			write = true;
		}

		if (error == -1) {
			return -1;
		}

		if (file_length > 0) {
			length = std::min(static_cast<uint64_t>(MAX_FILE_CHUNK_SIZE), file_length);
			// Queue up another read
			aio_prepare(&aio, data, length, offset);
			aio_read(&aio);
			read = true;
		}

		if (write) {
			if (writeHandle(mBulkIn, data2, ret) == -1) {
				error = -1;
			}
			write = false;
		}
	}

	if (ret % packet_size == 0) {
		// If the last packet wasn't short, send a final empty packet
		if (TEMP_FAILURE_RETRY(::write(mBulkIn, data, 0)) != 0) {
			return -1;
		}
	}

	return 0;
}

