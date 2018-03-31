/*
 * Copyright (C) 2016 The Android Open Source Project
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

#ifndef _OTA_TEST_CONSTANTS_H
#define _OTA_TEST_CONSTANTS_H

#include <string>

#include <android-base/file.h>

// Zip entries in ziptest_valid.zip.
static const std::string kATxtContents("abcdefghabcdefgh\n");
static const std::string kBTxtContents("abcdefgh\n");
static const std::string kCTxtContents("abcdefghabcdefgh\n");
static const std::string kDTxtContents("abcdefgh\n");

// echo -n -e "abcdefghabcdefgh\n" | sha1sum
static const std::string kATxtSha1Sum("32c96a03dc8cd20097940f351bca6261ee5a1643");
// echo -n -e "abcdefgh\n" | sha1sum
static const std::string kBTxtSha1Sum("e414af7161c9554089f4106d6f1797ef14a73666");

[[maybe_unused]] static std::string from_testdata_base(const std::string& fname) {
  static std::string exec_dir = android::base::GetExecutableDirectory();
  return exec_dir + "/testdata/" + fname;
}

#endif  // _OTA_TEST_CONSTANTS_H
