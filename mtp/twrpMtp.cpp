/*
 * Copyright (C) 2014 TeamWin - bigbiff and Dees_Troy mtp database conversion to C++
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
 */


#include <string>
#include <fcntl.h>
#include <sys/wait.h>
#include <pthread.h>
#include <utils/Errors.h>
#include <utils/threads.h>
#include "MtpTypes.h"
#include "MtpPacket.h"
#include "MtpDataPacket.h"
#include "MtpDatabase.h"
#include "MtpRequestPacket.h"
#include "MtpResponsePacket.h"
#include "mtp_MtpDatabase.hpp"
#include "mtp_MtpServer.hpp"
#include "twrpMtp.hpp"
#include "MtpDebug.h"

#ifdef TWRPMTP
static void usage(std::string prg) {
	printf("Usage: %s <OPTIONS>\n", prg.c_str());
	printf("Options:\n");
	printf("\t-h, --help\t\tShow Usage\n");
	printf("\t-s1, --storage1 /path/to/dir\t\tDestination to first storage directory\n");
	printf("\t-s2, --storage2 /path/to/dir\t\tDestination to first storage directory\n");
	printf("\t-sN, --storageN /path/to/dir\t\tDestination to first storage directory\n");
}

int main(int argc, char* argv[]) {
	printf("argc: %d\n", argc);
	if (argc < 2) {
		usage(argv[0]);
		return 1;
	}

	std::vector <std::string> storages;

	for (int i = 1; i < argc; ++i) {
		std::string arg = argv[i];
		if ((arg == "-h") || (arg == "--help")) {
			usage(argv[0]);
		}
		else {
			storages.push_back(arg);
		}
	}
	printf("starting\n");
	twmtp_MtpServer* mtp = new twmtp_MtpServer();
	mtp->set_storages(storages);
	mtp->start();
	return 0;
}
#endif //def TWRPMTP

twrpMtp::twrpMtp(int debug_enabled) {
	if (debug_enabled)
		MtpDebug::enableDebug();
	mtpstorages = new storages;
	mtp_read_pipe = -1;
}

int twrpMtp::start(void) {
	MTPI("Starting MTP\n");
	twmtp_MtpServer *mtp = new twmtp_MtpServer();
	mtp->set_storages(mtpstorages);
	mtp->set_read_pipe(mtp_read_pipe);
	mtp->start();
	return 0;
}

pthread_t twrpMtp::threadserver(void) {
	pthread_t thread;
	ThreadPtr mtpptr = &twrpMtp::start;
	PThreadPtr p = *(PThreadPtr*)&mtpptr;
	pthread_create(&thread, NULL, p, this);
	return thread;
}

pid_t twrpMtp::forkserver(int mtppipe[2]) {
	pid_t pid;
	if ((pid = fork()) == -1) {
		MTPE("MTP fork failed.\n");
		return 0;
	}
	if (pid == 0) {
		// Child process
		close(mtppipe[1]); // Child closes write side
		mtp_read_pipe = mtppipe[0];
		start();
		MTPD("MTP child process exited.\n");
		close(mtppipe[0]);
		_exit(0);
	} else {
		return pid;
	}
	return 0;
}

void twrpMtp::addStorage(std::string display, std::string path, int mtpid, uint64_t maxFileSize) {
	s = new storage;
	s->display = display;
	s->mount = path;
	s->mtpid = mtpid;
	s->maxFileSize = maxFileSize;
	MTPD("twrpMtp mtpid: %d\n", s->mtpid);
	mtpstorages->push_back(s);
}
