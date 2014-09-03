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

#ifndef MTP_MTPSERVER_HPP
#define MTP_MTPSERVER_HPP
#include <utils/Log.h>

#include <string>
#include <stdio.h>
#include <assert.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <utils/threads.h>

#include "MtpServer.h"
#include "MtpStorage.h"
#include "mtp_MtpDatabase.hpp"

typedef struct Storage {
	std::string display;
	std::string mount;
	int mtpid;
} storage;

typedef std::vector<storage*> storages;

class twmtp_MtpServer {
	public:
		void start();
		int setup();
		void run();
		void cleanup();
		void send_object_added(int handle);
		void send_object_removed(int handle);
		void add_storage();
		void remove_storage(int storageId);
		void set_storages(storages* mtpstorages);
		storages *stores;
	private:
		bool usePtp;
		MtpServer* server;
		MtpServer* refserver;

};
#endif
