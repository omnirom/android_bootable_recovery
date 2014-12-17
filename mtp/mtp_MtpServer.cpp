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
	if (setup() == 0) {
		add_storage();
		MTPD("Starting add / remove mtppipe monitor thread\n");
		pthread_t thread;
		ThreadPtr mtpptr = &twmtp_MtpServer::mtppipe_thread;
		PThreadPtr p = *(PThreadPtr*)&mtpptr;
		pthread_create(&thread, NULL, p, this);
		server->run();
	}
}

void twmtp_MtpServer::set_storages(storages* mtpstorages) {
	stores = mtpstorages;
}

int twmtp_MtpServer::setup()
{
	usePtp =  false;
	MyMtpDatabase* mtpdb = new MyMtpDatabase();
#ifdef USB_MTP_DEVICE
#define STRINGIFY(x) #x
#define EXPAND(x) STRINGIFY(x)
	MTPI("Using '%s' for MTP device.\n", EXPAND(USB_MTP_DEVICE));
	int fd = open(EXPAND(USB_MTP_DEVICE), O_RDWR);
#else
	int fd = open("/dev/mtp_usb", O_RDWR);
#endif
	if (fd >= 0) {
		MTPD("fd: %d\n", fd);
		server = new MtpServer(fd, mtpdb, usePtp, 0, 0664, 0775);
		refserver = server;
		MTPI("created new mtpserver object\n");
	} else {
		MTPE("could not open MTP driver, errno: %d\n", errno);
		return -1;
	}
	return 0;
}

void twmtp_MtpServer::run()
{
	MTPD("running in twmtp\n");
	server->run();
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

	MTPI("adding internal storage\n");
	for (unsigned int i = 0; i < stores->size(); ++i) {
			std::string pathStr = stores->at(i)->mount;

			if (!pathStr.empty()) {
				std::string descriptionStr = stores->at(i)->display;
				int storageID = stores->at(i)->mtpid;
				long reserveSpace = 1;
				bool removable = false;
				long maxFileSize = stores->at(i)->maxFileSize;
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
			// This delete seems to freeze / lock, probably because the
			// removeStorage call above has already removed the storage
			// object.
			//delete storage;
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
	int pipe_fd = mtp_read_pipe;
	int read_count = 1;
	struct mtpmsg mtp_message;
	while (1) {
		/*read_count = 1;
		MTPD("twmtp_MtpServer::mtppipe_thread opening mtppipe\n");
		pipe_fd = open(MTP_PIPE, O_RDONLY);
		if (pipe_fd < 0)
			continue;
		MTPD("twmtp_MtpServer::mtppipe_thread opened pipe\n");*/
		//while (read_count) {
			read_count = ::read(pipe_fd, &mtp_message, sizeof(mtp_message));
			MTPD("read %i from mtppipe\n", read_count);
			if (read_count > 0) {
				if (mtp_message.message_type == MTP_MESSAGE_ADD_STORAGE) {
					MTPI("mtppipe add storage %i '%s'\n", mtp_message.storage_id, mtp_message.path);
					long reserveSpace = 1;
					bool removable = false;
					MtpStorage* storage = new MtpStorage(mtp_message.storage_id, mtp_message.path, mtp_message.display, reserveSpace, removable, mtp_message.maxFileSize, refserver);
					server->addStorage(storage);
					MTPD("mtppipe done adding storage\n");
				} else if (mtp_message.message_type == MTP_MESSAGE_REMOVE_STORAGE) {
					MTPI("mtppipe remove storage %i\n", mtp_message.storage_id);
					remove_storage(mtp_message.storage_id);
					MTPD("mtppipe done removing storage\n");
				} else {
					MTPE("Unknown mtppipe message value: %i\n", mtp_message.message_type);
				}
			}
		//}
		//close(pipe_fd);
	}
	MTPD("twmtp_MtpServer::mtppipe_thread closing\n");
	return 0;
}

void twmtp_MtpServer::set_read_pipe(int pipe)
{
	mtp_read_pipe = pipe;
}
