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

#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "minui/minui.h"

TEST(GRSurfaceTest, Create_aligned) {
  auto surface = GRSurface::Create(9, 11, 9, 1);
  ASSERT_TRUE(surface);
  ASSERT_EQ(0, reinterpret_cast<uintptr_t>(surface->data()) % GRSurface::kSurfaceDataAlignment);
  // data_size will be rounded up to the next multiple of GRSurface::kSurfaceDataAlignment.
  ASSERT_EQ(0, surface->data_size() % GRSurface::kSurfaceDataAlignment);
  ASSERT_GE(surface->data_size(), 11 * 9);
}

TEST(GRSurfaceTest, Create_invalid_inputs) {
  ASSERT_FALSE(GRSurface::Create(9, 11, 0, 1));
  ASSERT_FALSE(GRSurface::Create(9, 0, 9, 1));
  ASSERT_FALSE(GRSurface::Create(0, 11, 9, 1));
  ASSERT_FALSE(GRSurface::Create(9, 11, 9, 0));
  ASSERT_FALSE(GRSurface::Create(9, 101, std::numeric_limits<size_t>::max() / 100, 1));
}

TEST(GRSurfaceTest, Clone) {
  auto image = GRSurface::Create(50, 10, 50, 1);
  ASSERT_GE(image->data_size(), 10 * 50);
  for (auto i = 0; i < image->data_size(); i++) {
    image->data()[i] = rand() % 128;
  }
  auto image_copy = image->Clone();
  ASSERT_EQ(image->data_size(), image_copy->data_size());
  ASSERT_EQ(std::vector(image->data(), image->data() + image->data_size()),
            std::vector(image_copy->data(), image_copy->data() + image->data_size()));
}
