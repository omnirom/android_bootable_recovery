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
#include <stdint.h>

#include "graphics.h"
#include "minui/minui.h"

class GRSurfaceFbdev : public GRSurface {
 public:
  uint8_t* data() override {
    return buffer_;
  }

 private:
  friend class MinuiBackendFbdev;

  // Points to the start of the buffer: either the mmap'd framebuffer or one allocated in-memory.
  uint8_t* buffer_;
};

class MinuiBackendFbdev : public MinuiBackend {
 public:
  GRSurface* Init() override;
  GRSurface* Flip() override;
  void Blank(bool) override;
  ~MinuiBackendFbdev() override;
  MinuiBackendFbdev();

 private:
  void SetDisplayedFramebuffer(unsigned n);

  GRSurfaceFbdev gr_framebuffer[2];
  bool double_buffered;
  GRSurfaceFbdev* gr_draw;
  int displayed_buffer;
  fb_var_screeninfo vi;
  int fb_fd;
};
