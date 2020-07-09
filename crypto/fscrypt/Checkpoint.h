/*
 * Copyright (C) 2018 The Android Open Source Project
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

#ifndef _CHECKPOINT_H
#define _CHECKPOINT_H

#include <binder/Status.h>
#include <string>

namespace android {
namespace vold {

android::binder::Status cp_supportsCheckpoint(bool& result);

android::binder::Status cp_supportsBlockCheckpoint(bool& result);

android::binder::Status cp_supportsFileCheckpoint(bool& result);

android::binder::Status cp_startCheckpoint(int retry);

android::binder::Status cp_commitChanges();

void cp_abortChanges(const std::string& message, bool retry);

bool cp_needsRollback();

bool cp_needsCheckpoint();

android::binder::Status cp_prepareCheckpoint();

android::binder::Status cp_restoreCheckpoint(const std::string& mountPoint, int count = 0);

android::binder::Status cp_markBootAttempt();

}  // namespace vold
}  // namespace android

#endif
