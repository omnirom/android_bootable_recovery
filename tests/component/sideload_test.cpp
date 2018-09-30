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

#include <unistd.h>

#include <gtest/gtest.h>

#include "fuse_sideload.h"

TEST(SideloadTest, fuse_device) {
  ASSERT_EQ(0, access("/dev/fuse", R_OK | W_OK));
}

TEST(SideloadTest, run_fuse_sideload_wrong_parameters) {
  provider_vtab vtab;
  vtab.close = [](void*) {};

  ASSERT_EQ(-1, run_fuse_sideload(&vtab, nullptr, 4096, 4095));
  ASSERT_EQ(-1, run_fuse_sideload(&vtab, nullptr, 4096, (1 << 22) + 1));

  // Too many blocks.
  ASSERT_EQ(-1, run_fuse_sideload(&vtab, nullptr, ((1 << 18) + 1) * 4096, 4096));
}
