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

#ifndef TWRP_WEAVER_H
#define TWRP_WEAVER_H

#include <memory>
#include <string>
#include <utility>

#include <android/hardware/weaver/1.0/IWeaver.h>
#include "Utils.h"

namespace android {
namespace vold {
using ::android::hardware::weaver::V1_0::IWeaver;

// Wrapper for a Weaver device
class Weaver {
	public:
		Weaver();
		// false if we failed to open the weaver device.
		explicit operator bool() { return mDevice.get() != nullptr; }

		bool GetSlots(uint32_t* slots);
		bool GetKeySize(uint32_t* keySize);
		bool GetValueSize(uint32_t* valueSize);
		// TODO: we should return more information about the status including time delays before the next retry
		bool WeaverVerify(const uint32_t slot, const void* weaver_key, std::vector<uint8_t>* payload);

	private:
		sp<hardware::weaver::V1_0::IWeaver> mDevice;
		hardware::weaver::V1_0::WeaverConfig config;
		bool GottenConfig;

		bool GetConfig();

		DISALLOW_COPY_AND_ASSIGN(Weaver);
};

}  // namespace vold
}  // namespace android

#endif
