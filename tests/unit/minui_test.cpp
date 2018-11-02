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

#include <stdint.h>
#include <stdlib.h>

#include <vector>

#include <gtest/gtest.h>

#include "minui/minui.h"

TEST(GRSurfaceTest, Create_aligned) {
  static constexpr size_t kSurfaceDataAlignment = 8;
  for (size_t data_size = 100; data_size < 128; data_size++) {
    auto surface = GRSurface::Create(10, 1, 10, 1, data_size);
    ASSERT_TRUE(surface);
    ASSERT_EQ(0, reinterpret_cast<uintptr_t>(surface->data()) % kSurfaceDataAlignment);
  }
}

TEST(GRSurfaceTest, Clone) {
  static constexpr size_t kImageSize = 10 * 50;
  auto image = GRSurface::Create(50, 10, 50, 1, kImageSize);
  for (auto i = 0; i < kImageSize; i++) {
    image->data()[i] = rand() % 128;
  }
  auto image_copy = image->Clone();
  ASSERT_EQ(std::vector(image->data(), image->data() + kImageSize),
            std::vector(image_copy->data(), image_copy->data() + kImageSize));
}
