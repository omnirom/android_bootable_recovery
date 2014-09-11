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

#include "mtp_MtpServer.hpp"
#include "MtpServer.h"
#include "MtpStorage.h"
#include "MtpDebug.h"

#include <string>

void twmtp_MtpServer::start()
{
	if (setup() == 0) {
		add_storage();
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
				long maxFileSize = 1000000000L;
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
			server->removeStorage(storage);
			delete storage;
		}
	} else
		MTPD("server is null in remove_storage");
}
