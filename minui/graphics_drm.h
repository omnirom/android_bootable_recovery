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

#ifndef _GRAPHICS_DRM_H_
#define _GRAPHICS_DRM_H_

#include <stdint.h>

#include <xf86drmMode.h>

#include "graphics.h"
#include "minui/minui.h"

class GRSurfaceDrm : public GRSurface {
 private:
  uint32_t fb_id;
  uint32_t handle;

  friend class MinuiBackendDrm;
};

class MinuiBackendDrm : public MinuiBackend {
 public:
  GRSurface* Init() override;
  GRSurface* Flip() override;
  void Blank(bool) override;
  ~MinuiBackendDrm() override;
  MinuiBackendDrm();

 private:
  void DrmDisableCrtc(int drm_fd, drmModeCrtc* crtc);
  void DrmEnableCrtc(int drm_fd, drmModeCrtc* crtc, GRSurfaceDrm* surface);
  GRSurfaceDrm* DrmCreateSurface(int width, int height);
  void DrmDestroySurface(GRSurfaceDrm* surface);
  void DisableNonMainCrtcs(int fd, drmModeRes* resources, drmModeCrtc* main_crtc);
  drmModeConnector* FindMainMonitor(int fd, drmModeRes* resources, uint32_t* mode_index);

  GRSurfaceDrm* GRSurfaceDrms[2];
  int current_buffer;
  drmModeCrtc* main_monitor_crtc;
  drmModeConnector* main_monitor_connector;
  int drm_fd;
};

#endif  // _GRAPHICS_DRM_H_
