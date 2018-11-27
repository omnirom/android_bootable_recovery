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

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include <memory>

#include <adf/adf.h>

#include "graphics.h"
#include "minui/minui.h"

class GRSurfaceAdf : public GRSurface {
 public:
  ~GRSurfaceAdf() override;

  static std::unique_ptr<GRSurfaceAdf> Create(int intf_fd, const drm_mode_modeinfo* mode,
                                              __u32 format, int* err);

  uint8_t* data() override {
    return mmapped_buffer_;
  }

 private:
  friend class MinuiBackendAdf;

  GRSurfaceAdf(size_t width, size_t height, size_t row_bytes, size_t pixel_bytes, __u32 offset,
               __u32 pitch, int fd)
      : GRSurface(width, height, row_bytes, pixel_bytes), offset(offset), pitch(pitch), fd(fd) {}

  const __u32 offset;
  const __u32 pitch;

  int fd;
  int fence_fd{ -1 };
  uint8_t* mmapped_buffer_{ nullptr };
};

class MinuiBackendAdf : public MinuiBackend {
 public:
  MinuiBackendAdf();
  ~MinuiBackendAdf() override;
  GRSurface* Init() override;
  GRSurface* Flip() override;
  void Blank(bool) override;

 private:
  int InterfaceInit();
  int DeviceInit(adf_device* dev);
  void Sync(GRSurfaceAdf* surf);

  int intf_fd;
  adf_id_t eng_id;
  __u32 format;
  adf_device dev;
  size_t current_surface;
  size_t n_surfaces;
  std::unique_ptr<GRSurfaceAdf> surfaces[2];
};
