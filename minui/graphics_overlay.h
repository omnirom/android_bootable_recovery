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

#ifndef _GRAPHICS_OVERLAY_H_
#define _GRAPHICS_OVERLAY_H_

#include <linux/fb.h>
#include <linux/msm_mdp.h>
#include <linux/msm_ion.h>

#include "graphics.h"
#include "minui/minui.h"

typedef struct {
    unsigned char *mem_buf;
    int size;
    int ion_fd;
    int mem_fd;
    struct ion_handle_data handle_data;
} MemInfo;

class MinuiBackendOverlay : public MinuiBackend {
 public:
  GRSurface* Init() override;
  GRSurface* Flip() override;
  void Blank(bool) override;
  ~MinuiBackendOverlay() override;
  MinuiBackendOverlay();

 private:
  void SetDisplayedFramebuffer(unsigned n);
  bool TargetHasOverlay(char *version);
  int GetLeftSplit();
  int GetRightSplit();
  int MapMdpPixelFormat();
  int FreeIonMem();
  int AllocateIonMem(unsigned int size);
  bool IsDisplaySplit();
  void SetDisplaySplit();
  int AllocateOverlay();
  int DisplayFrame();
  int FreeOverlay();

  GRSurface gr_framebuffer[2];
  bool double_buffered;
  GRSurface* gr_draw;
  int displayed_buffer;
  fb_var_screeninfo vi;
  int fb_fd;

  int left_split;
  int right_split;
  bool is_MDP5;
  int overlayL_id;
  int overlayR_id;
  MemInfo mem_info;
  size_t frame_size;
};

#endif  // _GRAPHICS_OVERLAY_H_
