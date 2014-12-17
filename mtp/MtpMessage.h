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

#ifndef _MTPMESSAGE_H
#define _MTPMESSAGE_H

#define MTP_PIPE "/sbin/mtppipe"

#define MTP_MESSAGE_ADD_STORAGE    1
#define MTP_MESSAGE_REMOVE_STORAGE 2

struct mtpmsg {
	int message_type; // 1 is add, 2 is remove, see above
	unsigned int storage_id;
	const char* display;
	const char* path;
	uint64_t maxFileSize;
};

#endif //_MTPMESSAGE_H
