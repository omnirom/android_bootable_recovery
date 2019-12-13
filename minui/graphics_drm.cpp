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
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include <memory>

#include <android-base/macros.h>
#include <android-base/stringprintf.h>
#include <android-base/unique_fd.h>
#include <drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "minui/minui.h"

GRSurfaceDrm::~GRSurfaceDrm() {
  if (mmapped_buffer_) {
    munmap(mmapped_buffer_, row_bytes * height);
  }

  if (fb_id) {
    if (drmModeRmFB(drm_fd_, fb_id) != 0) {
      perror("Failed to drmModeRmFB");
      // Falling through to free other resources.
    }
  }

  if (handle) {
    drm_gem_close gem_close = {};
    gem_close.handle = handle;

    if (drmIoctl(drm_fd_, DRM_IOCTL_GEM_CLOSE, &gem_close) != 0) {
      perror("Failed to DRM_IOCTL_GEM_CLOSE");
    }
  }
}

static int drm_format_to_bpp(uint32_t format) {
  switch (format) {
    case DRM_FORMAT_ABGR8888:
    case DRM_FORMAT_BGRA8888:
    case DRM_FORMAT_RGBX8888:
    case DRM_FORMAT_RGBA8888:
    case DRM_FORMAT_ARGB8888:
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

std::unique_ptr<GRSurfaceDrm> GRSurfaceDrm::Create(int drm_fd, int width, int height) {
  uint32_t format;
  PixelFormat pixel_format = gr_pixel_format();
  // PixelFormat comes in byte order, whereas DRM_FORMAT_* uses little-endian
  // (external/libdrm/include/drm/drm_fourcc.h). Note that although drm_fourcc.h also defines a
  // macro of DRM_FORMAT_BIG_ENDIAN, it doesn't seem to be actually supported (see the discussion
  // in https://lists.freedesktop.org/archives/amd-gfx/2017-May/008560.html).
  if (pixel_format == PixelFormat::ABGR) {
    format = DRM_FORMAT_RGBA8888;
  } else if (pixel_format == PixelFormat::BGRA) {
    format = DRM_FORMAT_ARGB8888;
  } else if (pixel_format == PixelFormat::RGBX) {
    format = DRM_FORMAT_XBGR8888;
  } else if (pixel_format == PixelFormat::ARGB) {
    format = DRM_FORMAT_BGRA8888;
  } else {
    format = DRM_FORMAT_RGB565;
  }

  drm_mode_create_dumb create_dumb = {};
  create_dumb.height = height;
  create_dumb.width = width;
  create_dumb.bpp = drm_format_to_bpp(format);
  create_dumb.flags = 0;

  if (drmIoctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_dumb) != 0) {
    perror("Failed to DRM_IOCTL_MODE_CREATE_DUMB");
    return nullptr;
  }

  // Cannot use std::make_unique to access non-public ctor.
  auto surface = std::unique_ptr<GRSurfaceDrm>(new GRSurfaceDrm(
      width, height, create_dumb.pitch, create_dumb.bpp / 8, drm_fd, create_dumb.handle));

  uint32_t handles[4], pitches[4], offsets[4];

  handles[0] = surface->handle;
  pitches[0] = create_dumb.pitch;
  offsets[0] = 0;
  if (drmModeAddFB2(drm_fd, width, height, format, handles, pitches, offsets, &surface->fb_id, 0) !=
      0) {
    perror("Failed to drmModeAddFB2");
    return nullptr;
  }

  drm_mode_map_dumb map_dumb = {};
  map_dumb.handle = create_dumb.handle;
  if (drmIoctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &map_dumb) != 0) {
    perror("Failed to DRM_IOCTL_MODE_MAP_DUMB");
    return nullptr;
  }

  auto mmapped = mmap(nullptr, surface->height * surface->row_bytes, PROT_READ | PROT_WRITE,
                      MAP_SHARED, drm_fd, map_dumb.offset);
  if (mmapped == MAP_FAILED) {
    perror("Failed to mmap()");
    return nullptr;
  }
  surface->mmapped_buffer_ = static_cast<uint8_t*>(mmapped);
  return surface;
}

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

bool MinuiBackendDrm::DrmEnableCrtc(int drm_fd, drmModeCrtc* crtc,
                                    const std::unique_ptr<GRSurfaceDrm>& surface) {
  if (drmModeSetCrtc(drm_fd, crtc->crtc_id, surface->fb_id, 0, 0,  // x,y
                     &main_monitor_connector->connector_id,
                     1,  // connector_count
                     &main_monitor_crtc->mode) != 0) {
    perror("Failed to drmModeSetCrtc");
    return false;
  }
  return true;
}

void MinuiBackendDrm::Blank(bool blank) {
  if (blank) {
    DrmDisableCrtc(drm_fd, main_monitor_crtc);
  } else {
    DrmEnableCrtc(drm_fd, main_monitor_crtc, GRSurfaceDrms[current_buffer]);
  }
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
  } while (!main_monitor_connector && i < arraysize(kConnectorPriority));

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
  drm_fd = -1;

  /* Consider DRM devices in order. */
  for (int i = 0; i < DRM_MAX_MINOR; i++) {
    auto dev_name = android::base::StringPrintf(DRM_DEV_NAME, DRM_DIR_NAME, i);
    android::base::unique_fd fd(open(dev_name.c_str(), O_RDWR | O_CLOEXEC));
    if (fd == -1) continue;

    /* We need dumb buffers. */
    if (uint64_t cap = 0; drmGetCap(fd.get(), DRM_CAP_DUMB_BUFFER, &cap) != 0 || cap == 0) {
      continue;
    }

    res = drmModeGetResources(fd.get());
    if (!res) {
      continue;
    }

    /* Use this device if it has at least one connected monitor. */
    if (res->count_crtcs > 0 && res->count_connectors > 0) {
      if (find_first_connected_connector(fd.get(), res)) {
        drm_fd = fd.release();
        break;
      }
    }

    drmModeFreeResources(res);
    res = nullptr;
  }

  if (drm_fd == -1 || res == nullptr) {
    perror("Failed to find/open a drm device");
    return nullptr;
  }

  uint32_t selected_mode;
  main_monitor_connector = FindMainMonitor(drm_fd, res, &selected_mode);
  if (!main_monitor_connector) {
    fprintf(stderr, "Failed to find main_monitor_connector\n");
    drmModeFreeResources(res);
    close(drm_fd);
    return nullptr;
  }

  main_monitor_crtc = find_crtc_for_connector(drm_fd, res, main_monitor_connector);
  if (!main_monitor_crtc) {
    fprintf(stderr, "Failed to find main_monitor_crtc\n");
    drmModeFreeResources(res);
    close(drm_fd);
    return nullptr;
  }

  DisableNonMainCrtcs(drm_fd, res, main_monitor_crtc);

  main_monitor_crtc->mode = main_monitor_connector->modes[selected_mode];

  int width = main_monitor_crtc->mode.hdisplay;
  int height = main_monitor_crtc->mode.vdisplay;

  drmModeFreeResources(res);

  GRSurfaceDrms[0] = GRSurfaceDrm::Create(drm_fd, width, height);
  GRSurfaceDrms[1] = GRSurfaceDrm::Create(drm_fd, width, height);
  if (!GRSurfaceDrms[0] || !GRSurfaceDrms[1]) {
    return nullptr;
  }

  current_buffer = 0;

  // We will likely encounter errors in the backend functions (i.e. Flip) if EnableCrtc fails.
  if (!DrmEnableCrtc(drm_fd, main_monitor_crtc, GRSurfaceDrms[1])) {
    return nullptr;
  }

  return GRSurfaceDrms[0].get();
}

static void page_flip_complete(__unused int fd,
                               __unused unsigned int sequence,
                               __unused unsigned int tv_sec,
                               __unused unsigned int tv_usec,
                               void *user_data) {
  *static_cast<bool*>(user_data) = false;
}

GRSurface* MinuiBackendDrm::Flip() {
  bool ongoing_flip = true;
  if (drmModePageFlip(drm_fd, main_monitor_crtc->crtc_id, GRSurfaceDrms[current_buffer]->fb_id,
                      DRM_MODE_PAGE_FLIP_EVENT, &ongoing_flip) != 0) {
    perror("Failed to drmModePageFlip");
    return nullptr;
  }

  while (ongoing_flip) {
    struct pollfd fds = {
      .fd = drm_fd,
      .events = POLLIN
    };

    if (poll(&fds, 1, -1) == -1 || !(fds.revents & POLLIN)) {
      perror("Failed to poll() on drm fd");
      break;
    }

    drmEventContext evctx = {
      .version = DRM_EVENT_CONTEXT_VERSION,
      .page_flip_handler = page_flip_complete
    };

    if (drmHandleEvent(drm_fd, &evctx) != 0) {
      perror("Failed to drmHandleEvent");
      break;
    }
  }

  current_buffer = 1 - current_buffer;
  return GRSurfaceDrms[current_buffer].get();
}

MinuiBackendDrm::~MinuiBackendDrm() {
  DrmDisableCrtc(drm_fd, main_monitor_crtc);
  drmModeFreeCrtc(main_monitor_crtc);
  drmModeFreeConnector(main_monitor_connector);
  close(drm_fd);
  drm_fd = -1;
}
