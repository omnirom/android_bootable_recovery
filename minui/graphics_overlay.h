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
#include <linux/ion.h>

#include "graphics.h"
#include "minui/minui.h"

#ifdef MSM_BSP
typedef struct {
    unsigned char *mem_buf;
    int size;
    int ion_fd;
    int mem_fd;
    struct ion_handle_data handle_data;
} memInfo;
#endif

class MinuiBackendOverlay : public MinuiBackend {
 public:
  GRSurface* Init() override;
  GRSurface* Flip() override;
  void Blank(bool) override;
  ~MinuiBackendOverlay() override;
  MinuiBackendOverlay();

 private:
  void SetDisplayedFramebuffer(unsigned n);
  bool target_has_overlay();

#ifdef MSM_BSP
  int map_mdp_pixel_format();
  void setDisplaySplit(void);
  int getLeftSplit(void);
  int getRightSplit(void);
  int free_ion_mem(void);
  int alloc_ion_mem(unsigned int size);
  bool isDisplaySplit(void);
  int allocate_overlay(int fd, GRSurface gr_fb[]);
  int overlay_display_frame(int fd, void* data, size_t size);
  int free_overlay(int fd);
#endif

  GRSurface gr_framebuffer[2];
  bool double_buffered;
  GRSurface* gr_draw;
  int displayed_buffer;
  fb_var_screeninfo vi;
  int fb_fd;
  bool isMDP5;
  int leftSplit;
  int rightSplit;
  size_t frame_size;
  int overlayL_id;
  int overlayR_id;
};

#endif  // _GRAPHICS_OVERLAY_H_
