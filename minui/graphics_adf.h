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

#ifndef _GRAPHICS_ADF_H_
#define _GRAPHICS_ADF_H_

#include <adf/adf.h>

#include "graphics.h"

class GRSurfaceAdf : public GRSurface {
 private:
  int fence_fd;
  int fd;
  __u32 offset;
  __u32 pitch;

  friend class MinuiBackendAdf;
};

class MinuiBackendAdf : public MinuiBackend {
 public:
  GRSurface* Init() override;
  GRSurface* Flip() override;
  void Blank(bool) override;
  ~MinuiBackendAdf() override;
  MinuiBackendAdf();

 private:
  int SurfaceInit(const drm_mode_modeinfo* mode, GRSurfaceAdf* surf);
  int InterfaceInit();
  int DeviceInit(adf_device* dev);
  void SurfaceDestroy(GRSurfaceAdf* surf);
  void Sync(GRSurfaceAdf* surf);

  int intf_fd;
  adf_id_t eng_id;
  __u32 format;
  adf_device dev;
  unsigned int current_surface;
  unsigned int n_surfaces;
  GRSurfaceAdf surfaces[2];
};

#endif  // _GRAPHICS_ADF_H_
