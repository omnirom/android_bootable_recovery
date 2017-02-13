/*
 * Copyright (C) 2015 The Android Open Source Project
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

#include "graphics_drm.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include <drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "minui/minui.h"

#define ARRAY_SIZE(A) (sizeof(A)/sizeof(*(A)))

MinuiBackendDrm::MinuiBackendDrm()
    : GRSurfaceDrms(), main_monitor_crtc(nullptr), main_monitor_connector(nullptr), drm_fd(-1) {}

void MinuiBackendDrm::DrmDisableCrtc(int drm_fd, drmModeCrtc* crtc) {
  if (crtc) {
    drmModeSetCrtc(drm_fd, crtc->crtc_id,
                   0,         // fb_id
                   0, 0,      // x,y
                   nullptr,   // connectors
                   0,         // connector_count
                   nullptr);  // mode
  }
}

void MinuiBackendDrm::DrmEnableCrtc(int drm_fd, drmModeCrtc* crtc, GRSurfaceDrm* surface) {
  int32_t ret = drmModeSetCrtc(drm_fd, crtc->crtc_id, surface->fb_id, 0, 0,  // x,y
                               &main_monitor_connector->connector_id,
                               1,  // connector_count
                               &main_monitor_crtc->mode);

  if (ret) {
    printf("drmModeSetCrtc failed ret=%d\n", ret);
  }
}

void MinuiBackendDrm::Blank(bool blank) {
  if (blank) {
    DrmDisableCrtc(drm_fd, main_monitor_crtc);
  } else {
    DrmEnableCrtc(drm_fd, main_monitor_crtc, GRSurfaceDrms[current_buffer]);
  }
}

void MinuiBackendDrm::DrmDestroySurface(GRSurfaceDrm* surface) {
  if (!surface) return;

  if (surface->data) {
    munmap(surface->data, surface->row_bytes * surface->height);
  }

  if (surface->fb_id) {
    int ret = drmModeRmFB(drm_fd, surface->fb_id);
    if (ret) {
      printf("drmModeRmFB failed ret=%d\n", ret);
    }
  }

  if (surface->handle) {
    drm_gem_close gem_close = {};
    gem_close.handle = surface->handle;

    int ret = drmIoctl(drm_fd, DRM_IOCTL_GEM_CLOSE, &gem_close);
    if (ret) {
      printf("DRM_IOCTL_GEM_CLOSE failed ret=%d\n", ret);
    }
  }

  delete surface;
}

static int drm_format_to_bpp(uint32_t format) {
  switch (format) {
    case DRM_FORMAT_ABGR8888:
    case DRM_FORMAT_BGRA8888:
    case DRM_FORMAT_RGBX8888:
    case DRM_FORMAT_BGRX8888:
    case DRM_FORMAT_XBGR8888:
    case DRM_FORMAT_XRGB8888:
      return 32;
    case DRM_FORMAT_RGB565:
      return 16;
    default:
      printf("Unknown format %d\n", format);
      return 32;
  }
}

GRSurfaceDrm* MinuiBackendDrm::DrmCreateSurface(int width, int height) {
  GRSurfaceDrm* surface = new GRSurfaceDrm;
  *surface = {};

  uint32_t format;
#if defined(RECOVERY_ABGR)
  format = DRM_FORMAT_RGBA8888;
#elif defined(RECOVERY_BGRA)
  format = DRM_FORMAT_ARGB8888;
#elif defined(RECOVERY_RGBX)
  format = DRM_FORMAT_XBGR8888;
#else
  format = DRM_FORMAT_RGB565;
#endif

  drm_mode_create_dumb create_dumb = {};
  create_dumb.height = height;
  create_dumb.width = width;
  create_dumb.bpp = drm_format_to_bpp(format);
  create_dumb.flags = 0;

  int ret = drmIoctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_dumb);
  if (ret) {
    printf("DRM_IOCTL_MODE_CREATE_DUMB failed ret=%d\n", ret);
    DrmDestroySurface(surface);
    return nullptr;
  }
  surface->handle = create_dumb.handle;

  uint32_t handles[4], pitches[4], offsets[4];

  handles[0] = surface->handle;
  pitches[0] = create_dumb.pitch;
  offsets[0] = 0;

  ret =
      drmModeAddFB2(drm_fd, width, height, format, handles, pitches, offsets, &(surface->fb_id), 0);
  if (ret) {
    printf("drmModeAddFB2 failed ret=%d\n", ret);
    DrmDestroySurface(surface);
    return nullptr;
  }

  drm_mode_map_dumb map_dumb = {};
  map_dumb.handle = create_dumb.handle;
  ret = drmIoctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &map_dumb);
  if (ret) {
    printf("DRM_IOCTL_MODE_MAP_DUMB failed ret=%d\n", ret);
    DrmDestroySurface(surface);
    return nullptr;
  }

  surface->height = height;
  surface->width = width;
  surface->row_bytes = create_dumb.pitch;
  surface->pixel_bytes = create_dumb.bpp / 8;
  surface->data = static_cast<unsigned char*>(mmap(nullptr, surface->height * surface->row_bytes,
                                                   PROT_READ | PROT_WRITE, MAP_SHARED, drm_fd,
                                                   map_dumb.offset));
  if (surface->data == MAP_FAILED) {
    perror("mmap() failed");
    DrmDestroySurface(surface);
    return nullptr;
  }

  return surface;
}

static drmModeCrtc* find_crtc_for_connector(int fd, drmModeRes* resources,
                                            drmModeConnector* connector) {
  // Find the encoder. If we already have one, just use it.
  drmModeEncoder* encoder;
  if (connector->encoder_id) {
    encoder = drmModeGetEncoder(fd, connector->encoder_id);
  } else {
    encoder = nullptr;
  }

  int32_t crtc;
  if (encoder && encoder->crtc_id) {
    crtc = encoder->crtc_id;
    drmModeFreeEncoder(encoder);
    return drmModeGetCrtc(fd, crtc);
  }

  // Didn't find anything, try to find a crtc and encoder combo.
  crtc = -1;
  for (int i = 0; i < connector->count_encoders; i++) {
    encoder = drmModeGetEncoder(fd, connector->encoders[i]);

    if (encoder) {
      for (int j = 0; j < resources->count_crtcs; j++) {
        if (!(encoder->possible_crtcs & (1 << j))) continue;
        crtc = resources->crtcs[j];
        break;
      }
      if (crtc >= 0) {
        drmModeFreeEncoder(encoder);
        return drmModeGetCrtc(fd, crtc);
      }
    }
  }

  return nullptr;
}

static drmModeConnector* find_used_connector_by_type(int fd, drmModeRes* resources, unsigned type) {
  for (int i = 0; i < resources->count_connectors; i++) {
    drmModeConnector* connector = drmModeGetConnector(fd, resources->connectors[i]);
    if (connector) {
      if ((connector->connector_type == type) && (connector->connection == DRM_MODE_CONNECTED) &&
          (connector->count_modes > 0)) {
        return connector;
      }
      drmModeFreeConnector(connector);
    }
  }
  return nullptr;
}

static drmModeConnector* find_first_connected_connector(int fd, drmModeRes* resources) {
  for (int i = 0; i < resources->count_connectors; i++) {
    drmModeConnector* connector;

    connector = drmModeGetConnector(fd, resources->connectors[i]);
    if (connector) {
      if ((connector->count_modes > 0) && (connector->connection == DRM_MODE_CONNECTED))
        return connector;

      drmModeFreeConnector(connector);
    }
  }
  return nullptr;
}

drmModeConnector* MinuiBackendDrm::FindMainMonitor(int fd, drmModeRes* resources,
                                                   uint32_t* mode_index) {
  /* Look for LVDS/eDP/DSI connectors. Those are the main screens. */
  static constexpr unsigned kConnectorPriority[] = {
    DRM_MODE_CONNECTOR_LVDS,
    DRM_MODE_CONNECTOR_eDP,
    DRM_MODE_CONNECTOR_DSI,
  };

  drmModeConnector* main_monitor_connector = nullptr;
  unsigned i = 0;
  do {
    main_monitor_connector = find_used_connector_by_type(fd, resources, kConnectorPriority[i]);
    i++;
  } while (!main_monitor_connector && i < ARRAY_SIZE(kConnectorPriority));

  /* If we didn't find a connector, grab the first one that is connected. */
  if (!main_monitor_connector) {
    main_monitor_connector = find_first_connected_connector(fd, resources);
  }

  /* If we still didn't find a connector, give up and return. */
  if (!main_monitor_connector) return nullptr;

  *mode_index = 0;
  for (int modes = 0; modes < main_monitor_connector->count_modes; modes++) {
    if (main_monitor_connector->modes[modes].type & DRM_MODE_TYPE_PREFERRED) {
      *mode_index = modes;
      break;
    }
  }

  return main_monitor_connector;
}

void MinuiBackendDrm::DisableNonMainCrtcs(int fd, drmModeRes* resources, drmModeCrtc* main_crtc) {
  for (int i = 0; i < resources->count_connectors; i++) {
    drmModeConnector* connector = drmModeGetConnector(fd, resources->connectors[i]);
    drmModeCrtc* crtc = find_crtc_for_connector(fd, resources, connector);
    if (crtc->crtc_id != main_crtc->crtc_id) {
      DrmDisableCrtc(fd, crtc);
    }
    drmModeFreeCrtc(crtc);
  }
}

GRSurface* MinuiBackendDrm::Init() {
  drmModeRes* res = nullptr;

  /* Consider DRM devices in order. */
  for (int i = 0; i < DRM_MAX_MINOR; i++) {
    char* dev_name;
    int ret = asprintf(&dev_name, DRM_DEV_NAME, DRM_DIR_NAME, i);
    if (ret < 0) continue;

    drm_fd = open(dev_name, O_RDWR, 0);
    free(dev_name);
    if (drm_fd < 0) continue;

    uint64_t cap = 0;
    /* We need dumb buffers. */
    ret = drmGetCap(drm_fd, DRM_CAP_DUMB_BUFFER, &cap);
    if (ret || cap == 0) {
      close(drm_fd);
      continue;
    }

    res = drmModeGetResources(drm_fd);
    if (!res) {
      close(drm_fd);
      continue;
    }

    /* Use this device if it has at least one connected monitor. */
    if (res->count_crtcs > 0 && res->count_connectors > 0) {
      if (find_first_connected_connector(drm_fd, res)) break;
    }

    drmModeFreeResources(res);
    close(drm_fd);
    res = nullptr;
  }

  if (drm_fd < 0 || res == nullptr) {
    perror("cannot find/open a drm device");
    return nullptr;
  }

  uint32_t selected_mode;
  main_monitor_connector = FindMainMonitor(drm_fd, res, &selected_mode);

  if (!main_monitor_connector) {
    printf("main_monitor_connector not found\n");
    drmModeFreeResources(res);
    close(drm_fd);
    return nullptr;
  }

  main_monitor_crtc = find_crtc_for_connector(drm_fd, res, main_monitor_connector);

  if (!main_monitor_crtc) {
    printf("main_monitor_crtc not found\n");
    drmModeFreeResources(res);
    close(drm_fd);
    return nullptr;
  }

  DisableNonMainCrtcs(drm_fd, res, main_monitor_crtc);

  main_monitor_crtc->mode = main_monitor_connector->modes[selected_mode];

  int width = main_monitor_crtc->mode.hdisplay;
  int height = main_monitor_crtc->mode.vdisplay;

  drmModeFreeResources(res);

  GRSurfaceDrms[0] = DrmCreateSurface(width, height);
  GRSurfaceDrms[1] = DrmCreateSurface(width, height);
  if (!GRSurfaceDrms[0] || !GRSurfaceDrms[1]) {
    // GRSurfaceDrms and drm_fd should be freed in d'tor.
    return nullptr;
  }

  current_buffer = 0;

  DrmEnableCrtc(drm_fd, main_monitor_crtc, GRSurfaceDrms[1]);

  return GRSurfaceDrms[0];
}

GRSurface* MinuiBackendDrm::Flip() {
  int ret = drmModePageFlip(drm_fd, main_monitor_crtc->crtc_id,
                            GRSurfaceDrms[current_buffer]->fb_id, 0, nullptr);
  if (ret < 0) {
    printf("drmModePageFlip failed ret=%d\n", ret);
    return nullptr;
  }
  current_buffer = 1 - current_buffer;
  return GRSurfaceDrms[current_buffer];
}

MinuiBackendDrm::~MinuiBackendDrm() {
  DrmDisableCrtc(drm_fd, main_monitor_crtc);
  DrmDestroySurface(GRSurfaceDrms[0]);
  DrmDestroySurface(GRSurfaceDrms[1]);
  drmModeFreeCrtc(main_monitor_crtc);
  drmModeFreeConnector(main_monitor_connector);
  close(drm_fd);
  drm_fd = -1;
}
