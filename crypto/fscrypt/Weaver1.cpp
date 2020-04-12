/*
 * Copyright (C) 2017 Team Win Recovery Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* To the best of my knowledge there is no native implementation for
 * Weaver so I made this by looking at the IWeaver.h file that gets
 * compiled by the build system. I took the information from this header
 * file and looked at keymaster source to get an idea of the proper way
 * to write the functions.
 */

#include "Weaver1.h"

//#include <android-base/logging.h>
//#include <keystore/keymaster_tags.h>
//#include <keystore/authorization_set.h>
//#include <keystore/keystore_hidl_support.h>

#include <android/hardware/weaver/1.0/IWeaver.h>

#include <iostream>
#define ERROR 1
#define LOG(x) std::cout

using namespace android::hardware::weaver;
using android::hardware::hidl_string;
using ::android::hardware::weaver::V1_0::IWeaver;
using ::android::hardware::weaver::V1_0::WeaverConfig;
using ::android::hardware::weaver::V1_0::WeaverReadStatus;
using ::android::hardware::weaver::V1_0::WeaverReadResponse;
using ::android::hardware::weaver::V1_0::WeaverStatus;
using ::android::hardware::Return;
using ::android::sp;

namespace android {
namespace vold {

Weaver::Weaver() {
	mDevice = ::android::hardware::weaver::V1_0::IWeaver::getService();
	GottenConfig = false;
}

bool Weaver::GetConfig() {
	if (GottenConfig)
		return true;

	WeaverStatus status;
	WeaverConfig cfg;

	bool callbackCalled = false;
	auto ret = mDevice->getConfig([&](WeaverStatus s, WeaverConfig c) {
		callbackCalled = true;
		status = s;
		cfg = c;
	});
	if (ret.isOk() && callbackCalled && status == WeaverStatus::OK) {
		config = cfg;
		GottenConfig = true;
		return true;
	}
	return false;
}

bool Weaver::GetSlots(uint32_t* slots) {
	if (!GetConfig())
		return false;
	*slots = config.slots;
	return true;
}

bool Weaver::GetKeySize(uint32_t* keySize) {
	if (!GetConfig())
		return false;
	*keySize = config.keySize;
	return true;
}

bool Weaver::GetValueSize(uint32_t* valueSize) {
	if (!GetConfig())
		return false;
	*valueSize = config.valueSize;
	return true;
}

// TODO: we should return more information about the status including time delays before the next retry
bool Weaver::WeaverVerify(const uint32_t slot, const void* weaver_key, std::vector<uint8_t>* payload) {
	bool callbackCalled = false;
	WeaverReadStatus status;
	std::vector<uint8_t> readValue;
	uint32_t timeout;
	uint32_t keySize;
	if (!GetKeySize(&keySize))
		return false;
	std::vector<uint8_t> key;
	key.resize(keySize);
	uint32_t index = 0;
	unsigned char* ptr = (unsigned char*)weaver_key;
	for (index = 0; index < keySize; index++) {
		key[index] = *ptr;
		ptr++;
	}
	const auto readRet = mDevice->read(slot, key, [&](WeaverReadStatus s, WeaverReadResponse r) {
		callbackCalled = true;
		status = s;
		readValue = r.value;
		timeout = r.timeout;
	});
	if (readRet.isOk() && callbackCalled && status == WeaverReadStatus::OK && timeout == 0) {
		*payload = readValue;
		return true;
	}
	return false;
}

}  // namespace vold
}  // namespace android
