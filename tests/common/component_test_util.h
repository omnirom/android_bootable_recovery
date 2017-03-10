/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agree to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _COMPONENT_TEST_UTIL_H
#define _COMPONENT_TEST_UTIL_H

#include <string>

#include <android-base/properties.h>
#include <fs_mgr.h>

// Check if the /misc entry exists in the fstab.
static bool parse_misc() {
  std::unique_ptr<fstab, decltype(&fs_mgr_free_fstab)> fstab(fs_mgr_read_fstab_default(),
                                                             fs_mgr_free_fstab);
  if (!fstab) {
    GTEST_LOG_(INFO) << "Failed to read default fstab";
    return false;
  }

  fstab_rec* record = fs_mgr_get_entry_for_mount_point(fstab.get(), "/misc");
  if (record == nullptr) {
    GTEST_LOG_(INFO) << "Failed to find /misc in fstab.";
    return false;
  }
  return true;
}

#endif //_COMPONENT_TEST_UTIL_H

