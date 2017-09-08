/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include "minui/minui.h"

TEST(LocaleTest, Misc) {
  EXPECT_TRUE(matches_locale("zh-CN", "zh-Hans-CN"));
  EXPECT_TRUE(matches_locale("zh", "zh-Hans-CN"));
  EXPECT_FALSE(matches_locale("zh-HK", "zh-Hans-CN"));
  EXPECT_TRUE(matches_locale("en-GB", "en-GB"));
  EXPECT_TRUE(matches_locale("en", "en-GB"));
  EXPECT_FALSE(matches_locale("en-GB", "en"));
  EXPECT_FALSE(matches_locale("en-GB", "en-US"));
  EXPECT_FALSE(matches_locale("en-US", ""));
  // Empty locale prefix in the PNG file will match the input locale.
  EXPECT_TRUE(matches_locale("", "en-US"));
  EXPECT_TRUE(matches_locale("sr-Latn", "sr-Latn-BA"));
}
