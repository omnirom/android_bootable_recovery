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
    EXPECT_TRUE(matches_locale("zh_CN", "zh_CN_#Hans"));
    EXPECT_TRUE(matches_locale("zh", "zh_CN_#Hans"));
    EXPECT_FALSE(matches_locale("zh_HK", "zh_CN_#Hans"));
    EXPECT_TRUE(matches_locale("en_GB", "en_GB"));
    EXPECT_TRUE(matches_locale("en", "en_GB"));
    EXPECT_FALSE(matches_locale("en_GB", "en"));
    EXPECT_FALSE(matches_locale("en_GB", "en_US"));
}
