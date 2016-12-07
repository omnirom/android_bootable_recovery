/*
 * Copyright (C) 2015 The Android Open Source Project
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

#ifndef TWRP_VOLD_UTILS_H
#define TWRP_VOLD_UTILS_H

#include <utils/Errors.h>
#include <cutils/multiuser.h>
#include <selinux/selinux.h>

#include <vector>
#include <string>

namespace android {
namespace vold {

/* Returns either WEXITSTATUS() status, or a negative errno */
status_t ForkExecvp(const std::vector<std::string>& args);
status_t ForkExecvp(const std::vector<std::string>& args, security_context_t context);

status_t ForkExecvp(const std::vector<std::string>& args,
        std::vector<std::string>& output);
status_t ForkExecvp(const std::vector<std::string>& args,
        std::vector<std::string>& output, security_context_t context);

pid_t ForkExecvpAsync(const std::vector<std::string>& args);

status_t ReadRandomBytes(size_t bytes, std::string& out);

/* Converts hex string to raw bytes, ignoring [ :-] */
status_t HexToStr(const std::string& hex, std::string& str);

std::string BuildKeyPath(const std::string& partGuid);

std::string BuildDataSystemLegacyPath(userid_t userid);
std::string BuildDataSystemCePath(userid_t userid);
std::string BuildDataSystemDePath(userid_t userid);
std::string BuildDataMiscLegacyPath(userid_t userid);
std::string BuildDataMiscCePath(userid_t userid);
std::string BuildDataMiscDePath(userid_t userid);
std::string BuildDataProfilesDePath(userid_t userid);
std::string BuildDataProfilesForeignDexDePath(userid_t userid);

std::string BuildDataPath(const char* volumeUuid);
std::string BuildDataMediaCePath(const char* volumeUuid, userid_t userid);
std::string BuildDataUserCePath(const char* volumeUuid, userid_t userid);
std::string BuildDataUserDePath(const char* volumeUuid, userid_t userid);

}  // namespace vold
}  // namespace android

#endif
