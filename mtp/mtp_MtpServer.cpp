/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *	  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Copyright (C) 2014 TeamWin - bigbiff and Dees_Troy mtp database conversion to C++
 */

#include <utils/Log.h>

#include <stdio.h>
#include <assert.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <vector>
#include <utils/threads.h>
#include <pthread.h>

#include "mtp_MtpServer.hpp"
#include "MtpServer.h"
#include "MtpStorage.h"
#include "MtpDebug.h"
#include "MtpMessage.hpp"

#include <string>

void twmtp_MtpServer::start()
{
	usePtp =  false;
	MyMtpDatabase* mtpdb = new MyMtpDatabase();
	/* Sleep for a bit before we open the MTP USB device because some
	 * devices are not ready due to the kernel not responding to our
	 * sysfs requests right away.
	 */
	usleep(800000);
#ifdef USB_MTP_DEVICE
#define STRINGIFY(x) #x
#define EXPAND(x) STRINGIFY(x)
	const char* mtp_device = EXPAND(USB_MTP_DEVICE);
	MTPI("Using '%s' for MTP device.\n", EXPAND(USB_MTP_DEVICE));
#else
	const char* mtp_device = "/dev/mtp_usb";
#endif
	int fd = open(mtp_device, O_RDWR);
	if (fd < 0) {
		MTPE("could not open MTP driver, errno: %d\n", errno);
		return;
	}
	MTPD("fd: %d\n", fd);
	server = new MtpServer(mtpdb, usePtp, 0, 0664, 0775);
	refserver = server;
	MTPI("created new mtpserver object\n");
	add_storage();
	MTPD("Starting add / remove mtppipe monitor thread\n");
	pthread_t thread;
	ThreadPtr mtpptr = &twmtp_MtpServer::mtppipe_thread;
	PThreadPtr p = *(PThreadPtr*)&mtpptr;
	pthread_create(&thread, NULL, p, this);
	// This loop restarts the MTP process if the device is unplugged and replugged in
	while (true) {
		server->run(fd);
		fd = open(mtp_device, O_RDWR);
		usleep(800000);
	}
}

void twmtp_MtpServer::set_storages(storages* mtpstorages) {
	stores = mtpstorages;
}

void twmtp_MtpServer::cleanup()
{
	android::Mutex sMutex;
	android::Mutex::Autolock autoLock(sMutex);

	if (server) {
		delete server;
	} else {
		MTPD("server is null in cleanup");
	}
}

void twmtp_MtpServer::send_object_added(int handle)
{
	android::Mutex sMutex;
	android::Mutex::Autolock autoLock(sMutex);

	if (server)
		server->sendObjectAdded(handle);
	else
		MTPD("server is null in send_object_added");
}

void twmtp_MtpServer::send_object_removed(int handle)
{
	android::Mutex sMutex;
	android::Mutex::Autolock autoLock(sMutex);

	if (server)
		server->sendObjectRemoved(handle);
	else
		MTPD("server is null in send_object_removed");
}

void twmtp_MtpServer::add_storage()
{
	android::Mutex sMutex;
	android::Mutex::Autolock autoLock(sMutex);

	MTPD("twmtp_MtpServer::add_storage count of storage devices: %i\n", stores->size());
	for (unsigned int i = 0; i < stores->size(); ++i) {
			std::string pathStr = stores->at(i)->mount;

			if (!pathStr.empty()) {
				std::string descriptionStr = stores->at(i)->display;
				int storageID = stores->at(i)->mtpid;
				long reserveSpace = 1;
				bool removable = false;
				uint64_t maxFileSize = stores->at(i)->maxFileSize;
				if (descriptionStr != "") {
					MtpStorage* storage = new MtpStorage(storageID, &pathStr[0], &descriptionStr[0], reserveSpace, removable, maxFileSize, refserver);
					server->addStorage(storage);
				}
		}
	}
}

void twmtp_MtpServer::remove_storage(int storageId)
{
	android::Mutex sMutex;
	android::Mutex::Autolock autoLock(sMutex);

	if (server) {
		MtpStorage* storage = server->getStorage(storageId);
		if (storage) {
			MTPD("twmtp_MtpServer::remove_storage calling removeStorage\n");
			server->removeStorage(storage);
		}
	} else
		MTPD("server is null in remove_storage");
	MTPD("twmtp_MtpServer::remove_storage DONE\n");
}

int twmtp_MtpServer::mtppipe_thread(void)
{
	if (mtp_read_pipe == -1) {
		MTPD("mtppipe_thread exiting because mtp_read_pipe not set\n");
		return 0;
	}
	MTPD("Starting twmtp_MtpServer::mtppipe_thread\n");
	int read_count;
	struct mtpmsg mtp_message;
	while (1) {
		read_count = ::read(mtp_read_pipe, &mtp_message, sizeof(mtp_message));
		MTPD("read %i from mtppipe\n", read_count);
		if (read_count == sizeof(mtp_message)) {
			if (mtp_message.message_type == MTP_MESSAGE_ADD_STORAGE) {
				MTPI("mtppipe add storage %i '%s'\n", mtp_message.storage_id, mtp_message.path);
				if (mtp_message.storage_id) {
					long reserveSpace = 1;
					bool removable = false;
					MtpStorage* storage = new MtpStorage(mtp_message.storage_id, mtp_message.path, mtp_message.display, reserveSpace, removable, mtp_message.maxFileSize, refserver);
					server->addStorage(storage);
					MTPD("mtppipe done adding storage\n");
				} else {
					MTPE("Invalid storage ID %i specified\n", mtp_message.storage_id);
				}
			} else if (mtp_message.message_type == MTP_MESSAGE_REMOVE_STORAGE) {
				MTPI("mtppipe remove storage %i\n", mtp_message.storage_id);
				remove_storage(mtp_message.storage_id);
				MTPD("mtppipe done removing storage\n");
			} else {
				MTPE("Unknown mtppipe message value: %i\n", mtp_message.message_type);
			}
		} else {
			MTPE("twmtp_MtpServer::mtppipe_thread unexpected read_count %i\n", read_count);
			close(mtp_read_pipe);
			break;
		}
	}
	MTPD("twmtp_MtpServer::mtppipe_thread closing\n");
	return 0;
}

void twmtp_MtpServer::set_read_pipe(int pipe)
{
	mtp_read_pipe = pipe;
}
