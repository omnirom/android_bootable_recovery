/*
 * Copyright (C) 2017 The Android Open Source Project
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

// See the comments in update_verifier.cpp.

#include <string>

#include <android-base/logging.h>
#include <android-base/properties.h>

#include "update_verifier/update_verifier.h"

int main(int argc, char** argv) {
  std::string s = android::base::GetProperty("ro.boot.slot_suffix", "");

  if (s.empty()) {
    return 0;  // non-A/B update device, so we quit
  }

  // Set up update_verifier logging to be written to kmsg; because we may not have Logd during
  // boot time.
  android::base::InitLogging(argv, &android::base::KernelLogger);

  return update_verifier(argc, argv);
}
