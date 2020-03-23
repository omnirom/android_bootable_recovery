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

#pragma once

#include <linux/fb.h>
#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <vector>

#include <android-base/unique_fd.h>

#include "graphics.h"
#include "minui/minui.h"

class GRSurfaceFbdev : public GRSurface {
 public:
  // Creates and returns a GRSurfaceFbdev instance, or nullptr on error.
  static std::unique_ptr<GRSurfaceFbdev> Create(size_t width, size_t height, size_t row_bytes,
                                                size_t pixel_bytes);

  uint8_t* data() override {
    return buffer_;
  }

 protected:
  using GRSurface::GRSurface;

 private:
  friend class MinuiBackendFbdev;

  // Points to the start of the buffer: either the mmap'd framebuffer or one allocated in-memory.
  uint8_t* buffer_{ nullptr };
};

class MinuiBackendFbdev : public MinuiBackend {
 public:
  MinuiBackendFbdev() = default;
  ~MinuiBackendFbdev() override = default;

  GRSurface* Init() override;
  GRSurface* Flip() override;
  void Blank(bool) override;

 private:
  void SetDisplayedFramebuffer(size_t n);

  std::unique_ptr<GRSurfaceFbdev> gr_framebuffer[2];
  // Points to the current surface (i.e. one of the two gr_framebuffer's).
  GRSurfaceFbdev* gr_draw{ nullptr };
  bool double_buffered;
  std::vector<uint8_t> memory_buffer;
  size_t displayed_buffer{ 0 };
  fb_var_screeninfo vi;
  android::base::unique_fd fb_fd;
};
