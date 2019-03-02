/*
 * Copyright (C) 2014 The Android Open Source Project
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

#include "graphics_adf.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <adf/adf.h>
#include <sync/sync.h>

#include "minui/minui.h"

GRSurfaceAdf::~GRSurfaceAdf() {
  if (mmapped_buffer_) {
    munmap(mmapped_buffer_, pitch * height);
  }
  if (fence_fd != -1) {
    close(fence_fd);
  }
  if (fd != -1) {
    close(fd);
  }
}

std::unique_ptr<GRSurfaceAdf> GRSurfaceAdf::Create(int intf_fd, const drm_mode_modeinfo* mode,
                                                   __u32 format, int* err) {
  __u32 offset;
  __u32 pitch;
  auto fd = adf_interface_simple_buffer_alloc(intf_fd, mode->hdisplay, mode->vdisplay, format,
                                              &offset, &pitch);

  if (fd < 0) {
    *err = fd;
    return nullptr;
  }

  std::unique_ptr<GRSurfaceAdf> surf = std::unique_ptr<GRSurfaceAdf>(
      new GRSurfaceAdf(mode->hdisplay, mode->vdisplay, pitch, (format == DRM_FORMAT_RGB565 ? 2 : 4),
                       offset, pitch, fd));

  auto mmapped =
      mmap(nullptr, surf->pitch * surf->height, PROT_WRITE, MAP_SHARED, surf->fd, surf->offset);
  if (mmapped == MAP_FAILED) {
    *err = -errno;
    return nullptr;
  }
  surf->mmapped_buffer_ = static_cast<uint8_t*>(mmapped);
  return surf;
}

MinuiBackendAdf::MinuiBackendAdf() : intf_fd(-1), dev(), current_surface(0), n_surfaces(0) {}

int MinuiBackendAdf::InterfaceInit() {
  adf_interface_data intf_data;
  if (int err = adf_get_interface_data(intf_fd, &intf_data); err < 0) return err;

  int result = 0;
  surfaces[0] = GRSurfaceAdf::Create(intf_fd, &intf_data.current_mode, format, &result);
  if (!surfaces[0]) {
    fprintf(stderr, "Failed to allocate surface 0: %s\n", strerror(-result));
    goto done;
  }

  surfaces[1] = GRSurfaceAdf::Create(intf_fd, &intf_data.current_mode, format, &result);
  if (!surfaces[1]) {
    fprintf(stderr, "Failed to allocate surface 1: %s\n", strerror(-result));
    n_surfaces = 1;
  } else {
    n_surfaces = 2;
  }

done:
  adf_free_interface_data(&intf_data);
  return result;
}

int MinuiBackendAdf::DeviceInit(adf_device* dev) {
  adf_id_t intf_id;
  int err = adf_find_simple_post_configuration(dev, &format, 1, &intf_id, &eng_id);
  if (err < 0) return err;

  err = adf_device_attach(dev, eng_id, intf_id);
  if (err < 0 && err != -EALREADY) return err;

  intf_fd = adf_interface_open(dev, intf_id, O_RDWR | O_CLOEXEC);
  if (intf_fd < 0) return intf_fd;

  err = InterfaceInit();
  if (err < 0) {
    close(intf_fd);
    intf_fd = -1;
  }

  return err;
}

GRSurface* MinuiBackendAdf::Init() {
  PixelFormat pixel_format = gr_pixel_format();
  if (pixel_format == PixelFormat::ABGR) {
    format = DRM_FORMAT_ABGR8888;
  } else if (pixel_format == PixelFormat::BGRA) {
    format = DRM_FORMAT_BGRA8888;
  } else if (pixel_format == PixelFormat::RGBX) {
    format = DRM_FORMAT_RGBX8888;
  } else {
    format = DRM_FORMAT_RGB565;
  }

  adf_id_t* dev_ids = nullptr;
  ssize_t n_dev_ids = adf_devices(&dev_ids);
  if (n_dev_ids == 0) {
    return nullptr;
  } else if (n_dev_ids < 0) {
    fprintf(stderr, "enumerating adf devices failed: %s\n", strerror(-n_dev_ids));
    return nullptr;
  }

  intf_fd = -1;

  for (ssize_t i = 0; i < n_dev_ids && intf_fd < 0; i++) {
    int err = adf_device_open(dev_ids[i], O_RDWR, &dev);
    if (err < 0) {
      fprintf(stderr, "opening adf device %u failed: %s\n", dev_ids[i], strerror(-err));
      continue;
    }

    err = DeviceInit(&dev);
    if (err < 0) {
      fprintf(stderr, "initializing adf device %u failed: %s\n", dev_ids[i], strerror(-err));
      adf_device_close(&dev);
    }
  }

  free(dev_ids);

  if (intf_fd < 0) return nullptr;

  GRSurface* ret = Flip();

  Blank(true);
  Blank(false);

  return ret;
}

void MinuiBackendAdf::Sync(GRSurfaceAdf* surf) {
  static constexpr unsigned int kWarningTimeout = 3000;

  if (surf == nullptr) return;

  if (surf->fence_fd >= 0) {
    int err = sync_wait(surf->fence_fd, kWarningTimeout);
    if (err < 0) {
      perror("adf sync fence wait error\n");
    }

    close(surf->fence_fd);
    surf->fence_fd = -1;
  }
}

GRSurface* MinuiBackendAdf::Flip() {
  const auto& surf = surfaces[current_surface];

  int fence_fd = adf_interface_simple_post(intf_fd, eng_id, surf->width, surf->height, format,
                                           surf->fd, surf->offset, surf->pitch, -1);
  if (fence_fd >= 0) surf->fence_fd = fence_fd;

  current_surface = (current_surface + 1) % n_surfaces;
  Sync(surfaces[current_surface].get());
  return surfaces[current_surface].get();
}

void MinuiBackendAdf::Blank(bool blank) {
  adf_interface_blank(intf_fd, blank ? DRM_MODE_DPMS_OFF : DRM_MODE_DPMS_ON);
}

MinuiBackendAdf::~MinuiBackendAdf() {
  adf_device_close(&dev);
  if (intf_fd >= 0) close(intf_fd);
}
