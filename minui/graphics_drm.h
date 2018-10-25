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

#include <stdint.h>

#include <memory>

#include <android-base/macros.h>
#include <xf86drmMode.h>

#include "graphics.h"
#include "minui/minui.h"

class GRSurfaceDrm : public GRSurface {
 public:
  explicit GRSurfaceDrm(int drm_fd) : drm_fd_(drm_fd) {}
  ~GRSurfaceDrm() override;

  // Creates a GRSurfaceDrm instance.
  static std::unique_ptr<GRSurfaceDrm> Create(int drm_fd, int width, int height);

  uint8_t* data() override {
    return mmapped_buffer_;
  }

 private:
  friend class MinuiBackendDrm;

  const int drm_fd_;

  uint32_t fb_id{ 0 };
  uint32_t handle{ 0 };
  uint8_t* mmapped_buffer_{ nullptr };

  DISALLOW_COPY_AND_ASSIGN(GRSurfaceDrm);
};

class MinuiBackendDrm : public MinuiBackend {
 public:
  MinuiBackendDrm() = default;
  ~MinuiBackendDrm() override;

  GRSurface* Init() override;
  GRSurface* Flip() override;
  void Blank(bool) override;

 private:
  void DrmDisableCrtc(int drm_fd, drmModeCrtc* crtc);
  bool DrmEnableCrtc(int drm_fd, drmModeCrtc* crtc, const std::unique_ptr<GRSurfaceDrm>& surface);
  void DisableNonMainCrtcs(int fd, drmModeRes* resources, drmModeCrtc* main_crtc);
  drmModeConnector* FindMainMonitor(int fd, drmModeRes* resources, uint32_t* mode_index);

  std::unique_ptr<GRSurfaceDrm> GRSurfaceDrms[2];
  int current_buffer{ 0 };
  drmModeCrtc* main_monitor_crtc{ nullptr };
  drmModeConnector* main_monitor_connector{ nullptr };
  int drm_fd{ -1 };
};
