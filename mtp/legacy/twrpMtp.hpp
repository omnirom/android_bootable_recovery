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

#ifndef TWRPMTP_HPP
#define TWRPMTP_HPP

#include <fcntl.h>
#include <utils/Errors.h>
#include <utils/threads.h>
#include <string>
#include <vector>
#include <pthread.h>
#include "MtpTypes.h"
#include "MtpPacket.h"
#include "MtpDataPacket.h"
#include "MtpDatabase.h"
#include "MtpRequestPacket.h"
#include "MtpResponsePacket.h"
#include "mtp_MtpDatabase.hpp"

class twrpMtp {
	public:
		twrpMtp(int debug_enabled = 0);
		pthread_t threadserver(void);
		pid_t forkserver(int mtppipe[2]);
		void addStorage(std::string display, std::string path, int mtpid, uint64_t maxFileSize);
	private:
		int start(void);
		typedef int (twrpMtp::*ThreadPtr)(void);
		typedef void* (*PThreadPtr)(void *);
		storages *mtpstorages;
		storage *s;
		int mtp_read_pipe;
};
#endif
