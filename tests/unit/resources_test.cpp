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

#include <string>

#include <gtest/gtest.h>

#include "common/test_constants.h"
#include "minui/minui.h"

TEST(ResourcesTest, res_create_multi_display_surface) {
  GRSurface** frames;
  int frame_count;
  int fps;
  ASSERT_EQ(0, res_create_multi_display_surface(from_testdata_base("battery_scale.png").c_str(),
                                                &frame_count, &fps, &frames));
  ASSERT_EQ(6, frame_count);
  ASSERT_EQ(20, fps);

  for (auto i = 0; i < frame_count; i++) {
    free(frames[i]);
  }
  free(frames);
}
