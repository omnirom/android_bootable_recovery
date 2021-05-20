/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include <gtest/gtest.h>

#include "install/spl_check.h"
#include "ota_metadata.pb.h"

using build::tools::releasetools::OtaMetadata;
class SplCheckUnittest : public ::testing::Test {
 public:
  OtaMetadata metadata;
};

TEST_F(SplCheckUnittest, OlderSPL) {
  metadata.set_spl_downgrade(false);
  metadata.mutable_postcondition()->set_security_patch_level("2021-04-25");
  ASSERT_TRUE(ViolatesSPLDowngrade(metadata, "2021-05-01"));
}

TEST_F(SplCheckUnittest, NewerSPL) {
  metadata.set_spl_downgrade(false);
  metadata.mutable_postcondition()->set_security_patch_level("2021-06-01");
  ASSERT_FALSE(ViolatesSPLDowngrade(metadata, "2021-05-05"));
}

TEST_F(SplCheckUnittest, OlderSPLPermit) {
  // If spl_downgrade is set to true, OTA should be permitted
  metadata.set_spl_downgrade(true);
  metadata.mutable_postcondition()->set_security_patch_level("2021-04-11");
  ASSERT_FALSE(ViolatesSPLDowngrade(metadata, "2021-05-11"));
}