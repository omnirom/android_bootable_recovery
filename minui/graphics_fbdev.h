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

#ifndef _GRAPHICS_FBDEV_H_
#define _GRAPHICS_FBDEV_H_

#include <linux/fb.h>

#include "graphics.h"
#include "minui/minui.h"

class MinuiBackendFbdev : public MinuiBackend {
 public:
  GRSurface* Init() override;
  GRSurface* Flip() override;
  void Blank(bool) override;
  ~MinuiBackendFbdev() override;
  MinuiBackendFbdev();

 private:
  void SetDisplayedFramebuffer(unsigned n);

  GRSurface gr_framebuffer[2];
  bool double_buffered;
  GRSurface* gr_draw;
  int displayed_buffer;
  fb_var_screeninfo vi;
  int fb_fd;
};

#endif  // _GRAPHICS_FBDEV_H_
