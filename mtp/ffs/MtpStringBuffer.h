/*
 * Copyright (C) 2010 The Android Open Source Project
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

#ifndef _MTP_STRING_BUFFER_H
#define _MTP_STRING_BUFFER_H

#include <log/log.h>
#include <stdint.h>
#include <string>

// Max Character number of a MTP String
#define MTP_STRING_MAX_CHARACTER_NUMBER				255

class MtpDataPacket;

// Represents a utf8 string, with a maximum of 255 characters
class MtpStringBuffer {

private:
	std::string		mString;

public:
					MtpStringBuffer() {};
					~MtpStringBuffer() {};

	explicit		MtpStringBuffer(const char* src);
	explicit		MtpStringBuffer(const uint16_t* src);
					MtpStringBuffer(const MtpStringBuffer& src);

	void			set(const char* src);
	void			set(const uint16_t* src);

	inline void		append(const char* other);
	inline void		append(MtpStringBuffer &other);

	bool			readFromPacket(MtpDataPacket* packet);
	void			writeToPacket(MtpDataPacket* packet) const;

	inline bool		isEmpty() const { return mString.empty(); }
	inline int		size() const { return mString.length(); }

	inline operator const char*() const { return mString.c_str(); }
};

inline void MtpStringBuffer::append(const char* other) {
	mString += other;
}

inline void MtpStringBuffer::append(MtpStringBuffer &other) {
	mString += other.mString;
}

#endif // _MTP_STRING_BUFFER_H
