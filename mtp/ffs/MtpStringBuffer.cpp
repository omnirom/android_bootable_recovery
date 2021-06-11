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

#define LOG_TAG "MtpStringBuffer"

#include <codecvt>
#include <locale>
#include <string>
#include <vector>

#include "MtpDataPacket.h"
#include "MtpStringBuffer.h"

namespace {

std::wstring_convert<std::codecvt_utf8_utf16<char16_t>,char16_t> gConvert;

static std::string utf16ToUtf8(std::u16string input_str) {
	return gConvert.to_bytes(input_str);
}

static std::u16string utf8ToUtf16(std::string input_str) {
	return gConvert.from_bytes(input_str);
}

} // namespace

MtpStringBuffer::MtpStringBuffer(const char* src)
{
	set(src);
}

MtpStringBuffer::MtpStringBuffer(const uint16_t* src)
{
	set(src);
}

MtpStringBuffer::MtpStringBuffer(const MtpStringBuffer& src)
{
	mString = src.mString;
}

void MtpStringBuffer::set(const char* src) {
	mString = std::string(src);
}

void MtpStringBuffer::set(const uint16_t* src) {
	mString = utf16ToUtf8(std::u16string((const char16_t*)src));
}

bool MtpStringBuffer::readFromPacket(MtpDataPacket* packet) {
	uint8_t count;
	if (!packet->getUInt8(count))
		return false;
	if (count == 0)
		return true;

	std::vector<char16_t> buffer(count);
	for (int i = 0; i < count; i++) {
		uint16_t ch;
		if (!packet->getUInt16(ch))
			return false;
		buffer[i] = ch;
	}
	if (buffer[count-1] != '\0') {
		MTPE("Mtp string not null terminated\n");
		return false;
	}
	mString = utf16ToUtf8(std::u16string(buffer.data()));
	return true;
}

void MtpStringBuffer::writeToPacket(MtpDataPacket* packet) const {
	std::u16string src16 = utf8ToUtf16(mString);
	int count = src16.length();

	if (count == 0) {
		packet->putUInt8(0);
		return;
	}
	packet->putUInt8(std::min(count + 1, MTP_STRING_MAX_CHARACTER_NUMBER));

	int i = 0;
	for (char16_t &c : src16) {
		if (i == MTP_STRING_MAX_CHARACTER_NUMBER - 1) {
			// Leave a slot for null termination.
			MTPD("Mtp truncating long string\n");
			break;
		}
		packet->putUInt16(c);
		i++;
	}
	// only terminate with zero if string is not empty
	packet->putUInt16(0);
}
