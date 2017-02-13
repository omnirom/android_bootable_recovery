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
#include <sys/mman.h>
#include <unistd.h>

#include <adf/adf.h>
#include <sync/sync.h>

#include "minui/minui.h"

MinuiBackendAdf::MinuiBackendAdf() : intf_fd(-1), dev(), n_surfaces(0), surfaces() {}

int MinuiBackendAdf::SurfaceInit(const drm_mode_modeinfo* mode, GRSurfaceAdf* surf) {
  *surf = {};
  surf->fence_fd = -1;
  surf->fd = adf_interface_simple_buffer_alloc(intf_fd, mode->hdisplay, mode->vdisplay, format,
                                               &surf->offset, &surf->pitch);
  if (surf->fd < 0) {
    return surf->fd;
  }

  surf->width = mode->hdisplay;
  surf->height = mode->vdisplay;
  surf->row_bytes = surf->pitch;
  surf->pixel_bytes = (format == DRM_FORMAT_RGB565) ? 2 : 4;

  surf->data = static_cast<uint8_t*>(
      mmap(nullptr, surf->pitch * surf->height, PROT_WRITE, MAP_SHARED, surf->fd, surf->offset));
  if (surf->data == MAP_FAILED) {
    int saved_errno = errno;
    close(surf->fd);
    return -saved_errno;
  }

  return 0;
}

int MinuiBackendAdf::InterfaceInit() {
  adf_interface_data intf_data;
  int err = adf_get_interface_data(intf_fd, &intf_data);
  if (err < 0) return err;

  int ret = 0;
  err = SurfaceInit(&intf_data.current_mode, &surfaces[0]);
  if (err < 0) {
    fprintf(stderr, "allocating surface 0 failed: %s\n", strerror(-err));
    ret = err;
    goto done;
  }

  err = SurfaceInit(&intf_data.current_mode, &surfaces[1]);
  if (err < 0) {
    fprintf(stderr, "allocating surface 1 failed: %s\n", strerror(-err));
    surfaces[1] = {};
    n_surfaces = 1;
  } else {
    n_surfaces = 2;
  }

done:
  adf_free_interface_data(&intf_data);
  return ret;
}

int MinuiBackendAdf::DeviceInit(adf_device* dev) {
  adf_id_t intf_id;
  int err = adf_find_simple_post_configuration(dev, &format, 1, &intf_id, &eng_id);
  if (err < 0) return err;

  err = adf_device_attach(dev, eng_id, intf_id);
  if (err < 0 && err != -EALREADY) return err;

  intf_fd = adf_interface_open(dev, intf_id, O_RDWR);
  if (intf_fd < 0) return intf_fd;

  err = InterfaceInit();
  if (err < 0) {
    close(intf_fd);
    intf_fd = -1;
  }

  return err;
}

GRSurface* MinuiBackendAdf::Init() {
#if defined(RECOVERY_ABGR)
  format = DRM_FORMAT_ABGR8888;
#elif defined(RECOVERY_BGRA)
  format = DRM_FORMAT_BGRA8888;
#elif defined(RECOVERY_RGBX)
  format = DRM_FORMAT_RGBX8888;
#else
  format = DRM_FORMAT_RGB565;
#endif

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
  static constexpr unsigned int warningTimeout = 3000;

  if (surf == nullptr) return;

  if (surf->fence_fd >= 0) {
    int err = sync_wait(surf->fence_fd, warningTimeout);
    if (err < 0) {
      perror("adf sync fence wait error\n");
    }

    close(surf->fence_fd);
    surf->fence_fd = -1;
  }
}

GRSurface* MinuiBackendAdf::Flip() {
  GRSurfaceAdf* surf = &surfaces[current_surface];

  int fence_fd = adf_interface_simple_post(intf_fd, eng_id, surf->width, surf->height, format,
                                           surf->fd, surf->offset, surf->pitch, -1);
  if (fence_fd >= 0) surf->fence_fd = fence_fd;

  current_surface = (current_surface + 1) % n_surfaces;
  Sync(&surfaces[current_surface]);
  return &surfaces[current_surface];
}

void MinuiBackendAdf::Blank(bool blank) {
  adf_interface_blank(intf_fd, blank ? DRM_MODE_DPMS_OFF : DRM_MODE_DPMS_ON);
}

void MinuiBackendAdf::SurfaceDestroy(GRSurfaceAdf* surf) {
  munmap(surf->data, surf->pitch * surf->height);
  close(surf->fence_fd);
  close(surf->fd);
}

MinuiBackendAdf::~MinuiBackendAdf() {
  adf_device_close(&dev);
  for (unsigned int i = 0; i < n_surfaces; i++) {
    SurfaceDestroy(&surfaces[i]);
  }
  if (intf_fd >= 0) close(intf_fd);
}
