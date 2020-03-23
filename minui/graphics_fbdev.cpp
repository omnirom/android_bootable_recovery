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

#include "graphics_fbdev.h"

#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include <memory>

#include <android-base/unique_fd.h>

#include "minui/minui.h"

std::unique_ptr<GRSurfaceFbdev> GRSurfaceFbdev::Create(size_t width, size_t height,
                                                       size_t row_bytes, size_t pixel_bytes) {
  // Cannot use std::make_unique to access non-public ctor.
  return std::unique_ptr<GRSurfaceFbdev>(new GRSurfaceFbdev(width, height, row_bytes, pixel_bytes));
}

void MinuiBackendFbdev::Blank(bool blank) {
#if defined(TW_NO_SCREEN_BLANK) && defined(TW_BRIGHTNESS_PATH) && defined(TW_MAX_BRIGHTNESS)
    int fd;
    char brightness[4];
    snprintf(brightness, 4, "%03d", TW_MAX_BRIGHTNESS/2);

    fd = open(TW_BRIGHTNESS_PATH, O_RDWR);
    if (fd < 0) {
        perror("cannot open LCD backlight");
        return;
    }
    write(fd, blank ? "000" : brightness, 3);
    close(fd);
#else
    int ret;

    ret = ioctl(fb_fd, FBIOBLANK, blank ? FB_BLANK_POWERDOWN : FB_BLANK_UNBLANK);
    if (ret < 0)
        perror("ioctl(): blank");
#endif
}

void MinuiBackendFbdev::SetDisplayedFramebuffer(size_t n) {
  if (n > 1 || !double_buffered) return;

  vi.yres_virtual = gr_framebuffer[0]->height * 2;
  vi.yoffset = n * gr_framebuffer[0]->height;
  vi.bits_per_pixel = gr_framebuffer[0]->pixel_bytes * 8;
  if (ioctl(fb_fd, FBIOPUT_VSCREENINFO, &vi) < 0) {
    perror("active fb swap failed");
  }
  displayed_buffer = n;
}

GRSurface* MinuiBackendFbdev::Init() {
  android::base::unique_fd fd(open("/dev/graphics/fb0", O_RDWR | O_CLOEXEC));
  if (fd == -1) {
    perror("cannot open fb0");
    return nullptr;
  }

  fb_fix_screeninfo fi;
  if (ioctl(fd, FBIOGET_FSCREENINFO, &fi) < 0) {
    perror("failed to get fb0 info");
    return nullptr;
  }

  if (ioctl(fd, FBIOGET_VSCREENINFO, &vi) < 0) {
    perror("failed to get fb0 info");
    return nullptr;
  }

  // We print this out for informational purposes only, but
  // throughout we assume that the framebuffer device uses an RGBX
  // pixel format.  This is the case for every development device I
  // have access to.  For some of those devices (eg, hammerhead aka
  // Nexus 5), FBIOGET_VSCREENINFO *reports* that it wants a
  // different format (XBGR) but actually produces the correct
  // results on the display when you write RGBX.
  //
  // If you have a device that actually *needs* another pixel format
  // (ie, BGRX, or 565), patches welcome...

  printf(
      "fb0 reports (possibly inaccurate):\n"
      "  vi.bits_per_pixel = %d\n"
      "  vi.red.offset   = %3d   .length = %3d\n"
      "  vi.green.offset = %3d   .length = %3d\n"
      "  vi.blue.offset  = %3d   .length = %3d\n",
      vi.bits_per_pixel, vi.red.offset, vi.red.length, vi.green.offset, vi.green.length,
      vi.blue.offset, vi.blue.length);

  void* bits = mmap(0, fi.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (bits == MAP_FAILED) {
    perror("failed to mmap framebuffer");
    return nullptr;
  }

  memset(bits, 0, fi.smem_len);

  gr_framebuffer[0] =
      GRSurfaceFbdev::Create(vi.xres, vi.yres, fi.line_length, vi.bits_per_pixel / 8);
  gr_framebuffer[0]->buffer_ = static_cast<uint8_t*>(bits);
  memset(gr_framebuffer[0]->buffer_, 0, gr_framebuffer[0]->height * gr_framebuffer[0]->row_bytes);

  gr_framebuffer[1] =
      GRSurfaceFbdev::Create(gr_framebuffer[0]->width, gr_framebuffer[0]->height,
                             gr_framebuffer[0]->row_bytes, gr_framebuffer[0]->pixel_bytes);

  /* check if we can use double buffering */
  if (vi.yres * fi.line_length * 2 <= fi.smem_len) {
    double_buffered = true;

    gr_framebuffer[1]->buffer_ =
        gr_framebuffer[0]->buffer_ + gr_framebuffer[0]->height * gr_framebuffer[0]->row_bytes;
  } else {
    double_buffered = false;

    // Without double-buffering, we allocate RAM for a buffer to draw in, and then "flipping" the
    // buffer consists of a memcpy from the buffer we allocated to the framebuffer.
    memory_buffer.resize(gr_framebuffer[1]->height * gr_framebuffer[1]->row_bytes);
    gr_framebuffer[1]->buffer_ = memory_buffer.data();
  }

  gr_draw = gr_framebuffer[1].get();
  memset(gr_draw->buffer_, 0, gr_draw->height * gr_draw->row_bytes);
  fb_fd = std::move(fd);
  SetDisplayedFramebuffer(0);

  printf("framebuffer: %d (%zu x %zu)\n", fb_fd.get(), gr_draw->width, gr_draw->height);

  Blank(true);
  Blank(false);

  return gr_draw;
}

GRSurface* MinuiBackendFbdev::Flip() {
  if (double_buffered) {
    // Change gr_draw to point to the buffer currently displayed, then flip the driver so we're
    // displaying the other buffer instead.
    gr_draw = gr_framebuffer[displayed_buffer].get();
    SetDisplayedFramebuffer(1 - displayed_buffer);
  } else {
    // Copy from the in-memory surface to the framebuffer.
    memcpy(gr_framebuffer[0]->buffer_, gr_draw->buffer_, gr_draw->height * gr_draw->row_bytes);
  }
  return gr_draw;
}
