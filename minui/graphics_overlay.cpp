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

#include "graphics_overlay.h"

#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include "minui/minui.h"

#define MDP_V4_0 400
#define MAX_DISPLAY_DIM  2048

#define ALIGN(x, align) (((x) + ((align)-1)) & ~((align)-1))

MinuiBackendOverlay::MinuiBackendOverlay() :
    gr_draw(nullptr), fb_fd(-1),
    left_split(0), right_split(0), is_MDP5(false),
    overlayL_id(MSMFB_NEW_REQUEST), overlayR_id(MSMFB_NEW_REQUEST),
    frame_size(0) {}

void MinuiBackendOverlay::Blank(bool blank) {
  int ret = ioctl(fb_fd, FBIOBLANK, blank ? FB_BLANK_POWERDOWN : FB_BLANK_UNBLANK);
  if (ret < 0) perror("ioctl(): blank");
}

void MinuiBackendOverlay::SetDisplayedFramebuffer(unsigned n) {
  if (n > 1 || !double_buffered) return;

  vi.yres_virtual = gr_framebuffer[0].height * 2;
  vi.yoffset = n * gr_framebuffer[0].height;
  vi.bits_per_pixel = gr_framebuffer[0].pixel_bytes * 8;
  if (ioctl(fb_fd, FBIOPUT_VSCREENINFO, &vi) < 0) {
    perror("active fb swap failed");
  }
  displayed_buffer = n;
}

GRSurface* MinuiBackendOverlay::Init() {
  int fd = open("/dev/graphics/fb0", O_RDWR);
  if (fd == -1) {
    perror("open_overlay cannot open fb0");
    return nullptr;
  }

  fb_fix_screeninfo fi;
  if (ioctl(fd, FBIOGET_FSCREENINFO, &fi) < 0) {
    perror("failed to get fb0 info");
    close(fd);
    return nullptr;
  }

  if (ioctl(fd, FBIOGET_VSCREENINFO, &vi) < 0) {
    perror("failed to get fb0 info");
    close(fd);
    return nullptr;
  }

  if (!TargetHasOverlay(fi.id)) {
    perror("target has no overlay");
    close(fd);
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
    close(fd);
    return nullptr;
  }

  memset(bits, 0, fi.smem_len);

  SetDisplaySplit();

  gr_framebuffer[0].width = vi.xres;
  gr_framebuffer[0].height = vi.yres;
  gr_framebuffer[0].row_bytes = fi.line_length;
  gr_framebuffer[0].pixel_bytes = vi.bits_per_pixel / 8;
  gr_framebuffer[0].data = static_cast<uint8_t*>(bits);
  memset(gr_framebuffer[0].data, 0, gr_framebuffer[0].height * gr_framebuffer[0].row_bytes);

#ifdef OVERLAY_ENABLE_DOUBLE_BUFFERING
  /* check if we can use double buffering */
  if (vi.yres * fi.line_length * 2 <= fi.smem_len) {
    double_buffered = true;

    memcpy(gr_framebuffer + 1, gr_framebuffer, sizeof(GRSurface));
    gr_framebuffer[1].data =
        gr_framebuffer[0].data + gr_framebuffer[0].height * gr_framebuffer[0].row_bytes;

    gr_draw = gr_framebuffer + 1;

  } else
#endif
  {
    double_buffered = false;

    // Without double-buffering, we allocate RAM for a buffer to
    // draw in, and then "flipping" the buffer consists of a
    // memcpy from the buffer we allocated to the framebuffer.

    gr_draw = static_cast<GRSurface*>(malloc(sizeof(GRSurface)));
    memcpy(gr_draw, gr_framebuffer, sizeof(GRSurface));
    gr_draw->data = static_cast<unsigned char*>(malloc(gr_draw->height * gr_draw->row_bytes));
    if (!gr_draw->data) {
      perror("failed to allocate in-memory surface");
      return nullptr;
    }
  }

  memset(gr_draw->data, (unsigned char)0xee, gr_draw->height * gr_draw->row_bytes);
  fb_fd = fd;
  SetDisplayedFramebuffer(0);

  frame_size = fi.line_length * vi.yres;

  printf("framebuffer: %d (%d x %d)\n", fb_fd, gr_draw->width, gr_draw->height);

  Blank(true);
  Blank(false);

  if (!AllocateIonMem(fi.line_length * vi.yres))
    AllocateOverlay();

  return gr_draw;
}

GRSurface* MinuiBackendOverlay::Flip() {
    if (double_buffered) {
#if defined(RECOVERY_BGRA)
        // In case of BGRA, do some byte swapping
        unsigned int idx;
        unsigned char tmp;
        unsigned char* ucfb_vaddr = (unsigned char*)gr_draw->data;
        for (idx = 0 ; idx < (gr_draw->height * gr_draw->row_bytes);
                idx += 4) {
            tmp = ucfb_vaddr[idx];
            ucfb_vaddr[idx    ] = ucfb_vaddr[idx + 2];
            ucfb_vaddr[idx + 2] = tmp;
        }
#endif
        // Change gr_draw to point to the buffer currently displayed,
        // then flip the driver so we're displaying the other buffer
        // instead.
        gr_draw = gr_framebuffer + displayed_buffer;
        SetDisplayedFramebuffer(1 - displayed_buffer);
        DisplayFrame();
    } else {
        // Copy from the in-memory surface to the framebuffer.
        DisplayFrame();
    }
    return gr_draw;
}

MinuiBackendOverlay::~MinuiBackendOverlay() {
  FreeOverlay();
  FreeIonMem();

  close(fb_fd);
  fb_fd = -1;

  if (!double_buffered && gr_draw) {
    free(gr_draw->data);
    free(gr_draw);
  }
  gr_draw = nullptr;
}

///////////////////// Private methods //////////////////////

bool MinuiBackendOverlay::TargetHasOverlay(char *version) {
  int mdp_version;
  bool overlay_supported = false;

  if (strlen(version) >= 8) {
    if (!strncmp(version, "msmfb", strlen("msmfb"))) {
      char str_ver[4];
      memcpy(str_ver, version + strlen("msmfb"), 3);
      str_ver[3] = '\0';
      mdp_version = atoi(str_ver);
      if (mdp_version >= MDP_V4_0) {
        overlay_supported = true;
      }
    } else if (!strncmp(version, "mdssfb", strlen("mdssfb"))) {
      overlay_supported = true;
      is_MDP5 = true;
    }
  }
  return overlay_supported;
}

void MinuiBackendOverlay::SetDisplaySplit() {
  char split[64] = {0};
  if (!is_MDP5)
    return;
  FILE* fp = fopen("/sys/class/graphics/fb0/msm_fb_split", "r");
  if (fp) {
    //Format "left right" space as delimiter
    if(fread(split, sizeof(char), 64, fp)) {
      left_split = atoi(split);
      printf("Left Split=%d\n",left_split);
      char *rght = strpbrk(split, " ");
      if (rght)
        right_split = atoi(rght + 1);
      printf("Right Split=%d\n", right_split);
    }
  } else {
    printf("Failed to open mdss_fb_split node\n");
  }
  if (fp)
    fclose(fp);
}

int MinuiBackendOverlay::GetLeftSplit() {
  // Default even split for all displays with high res
  int lsplit = vi.xres / 2;

  // Override if split published by driver
  if (left_split)
  lsplit = left_split;

  return lsplit;
}

int MinuiBackendOverlay::GetRightSplit() {
  return right_split;
}

int MinuiBackendOverlay::MapMdpPixelFormat() {
  int format = MDP_RGB_565;
#if defined(RECOVERY_BGRA)
  format = MDP_BGRA_8888;
#elif defined(RECOVERY_RGBA)
  format = MDP_RGBA_8888;
#elif defined(RECOVERY_RGBX)
  format = MDP_RGBA_8888;
#endif
  return format;
}

int MinuiBackendOverlay::FreeIonMem(void) {
  int ret = 0;

  if (mem_info.mem_buf)
    munmap(mem_info.mem_buf, mem_info.size);

  if (mem_info.ion_fd >= 0) {
    ret = ioctl(mem_info.ion_fd, ION_IOC_FREE, &mem_info.handle_data);
    if (ret < 0)
      perror("free_mem failed ");
  }

  if (mem_info.mem_fd >= 0)
    close(mem_info.mem_fd);
  if (mem_info.ion_fd >= 0)
    close(mem_info.ion_fd);

  memset(&mem_info, 0, sizeof(mem_info));
  mem_info.mem_fd = -1;
  mem_info.ion_fd = -1;
  return 0;
}

int MinuiBackendOverlay::AllocateIonMem(unsigned int size) {
  int result;
  struct ion_fd_data fd_data;
  struct ion_allocation_data ion_alloc_data;

  mem_info.ion_fd = open("/dev/ion", O_RDWR|O_DSYNC);
  if (mem_info.ion_fd < 0) {
    perror("ERROR: Can't open ion ");
    return -errno;
  }

  ion_alloc_data.flags = 0;
  ion_alloc_data.len = size;
  ion_alloc_data.align = sysconf(_SC_PAGESIZE);
  ion_alloc_data.heap_id_mask =
  ION_HEAP(ION_IOMMU_HEAP_ID) |
  ION_HEAP(ION_SYSTEM_CONTIG_HEAP_ID);

  result = ioctl(mem_info.ion_fd, ION_IOC_ALLOC,  &ion_alloc_data);
  if (result){
    perror("ION_IOC_ALLOC Failed ");
    close(mem_info.ion_fd);
    return result;
  }

  fd_data.handle = ion_alloc_data.handle;
  mem_info.handle_data.handle = ion_alloc_data.handle;
  result = ioctl(mem_info.ion_fd, ION_IOC_MAP, &fd_data);
  if (result) {
    perror("ION_IOC_MAP Failed ");
    FreeIonMem();
    return result;
  }
  mem_info.mem_buf = (unsigned char *)mmap(NULL, size, PROT_READ |
    PROT_WRITE, MAP_SHARED, fd_data.fd, 0);
  mem_info.mem_fd = fd_data.fd;

  if (!mem_info.mem_buf) {
    perror("ERROR: mem_buf MAP_FAILED ");
    FreeIonMem();
    return -ENOMEM;
  }

  return 0;
}

bool MinuiBackendOverlay::IsDisplaySplit() {
    if (vi.xres > MAX_DISPLAY_DIM)
        return true;
    //check if right split is set by driver
    if (GetRightSplit())
        return true;

    return false;
}

int MinuiBackendOverlay::AllocateOverlay() {
  int ret = 0;

  if (!IsDisplaySplit()) {
    // Check if overlay is already allocated
    if (MSMFB_NEW_REQUEST == overlayL_id) {
      struct mdp_overlay overlayL;

      memset(&overlayL, 0 , sizeof (struct mdp_overlay));

            /* Fill Overlay Data */
      overlayL.src.width  = ALIGN(gr_framebuffer[0].width, 32);
      overlayL.src.height = gr_framebuffer[0].height;
      overlayL.src.format = MapMdpPixelFormat();
      overlayL.src_rect.w = gr_framebuffer[0].width;
      overlayL.src_rect.h = gr_framebuffer[0].height;
      overlayL.dst_rect.w = gr_framebuffer[0].width;
      overlayL.dst_rect.h = gr_framebuffer[0].height;
      overlayL.alpha = 0xFF;
      overlayL.transp_mask = MDP_TRANSP_NOP;
      overlayL.id = MSMFB_NEW_REQUEST;
      ret = ioctl(fb_fd, MSMFB_OVERLAY_SET, &overlayL);
      if (ret < 0) {
        perror("Overlay Set Failed");
        return ret;
      }
      overlayL_id = overlayL.id;
    }
  } else {
    float xres = vi.xres;
    int lsplit = GetLeftSplit();
    float lsplit_ratio = lsplit / xres;
    float lcrop_width = gr_framebuffer[0].width * lsplit_ratio;
    int lwidth = lsplit;
    int rwidth = gr_framebuffer[0].width - lsplit;
    int height = gr_framebuffer[0].height;

    if (MSMFB_NEW_REQUEST == overlayL_id) {

      struct mdp_overlay overlayL;

      memset(&overlayL, 0 , sizeof (struct mdp_overlay));

      // Fill OverlayL Data
      overlayL.src.width  = ALIGN(gr_framebuffer[0].width, 32);
      overlayL.src.height = gr_framebuffer[0].height;
      overlayL.src.format = MapMdpPixelFormat();
      overlayL.src_rect.x = 0;
      overlayL.src_rect.y = 0;
      overlayL.src_rect.w = lcrop_width;
      overlayL.src_rect.h = gr_framebuffer[0].height;
      overlayL.dst_rect.x = 0;
      overlayL.dst_rect.y = 0;
      overlayL.dst_rect.w = lwidth;
      overlayL.dst_rect.h = height;
      overlayL.alpha = 0xFF;
      overlayL.transp_mask = MDP_TRANSP_NOP;
      overlayL.id = MSMFB_NEW_REQUEST;
      ret = ioctl(fb_fd, MSMFB_OVERLAY_SET, &overlayL);
      if (ret < 0) {
        perror("OverlayL Set Failed");
        return ret;
      }
      overlayL_id = overlayL.id;
    }
    if (MSMFB_NEW_REQUEST == overlayR_id) {
      struct mdp_overlay overlayR;

      memset(&overlayR, 0 , sizeof (struct mdp_overlay));

      // Fill OverlayR Data
      overlayR.src.width  = ALIGN(gr_framebuffer[0].width, 32);
      overlayR.src.height = gr_framebuffer[0].height;
      overlayR.src.format = MapMdpPixelFormat();
      overlayR.src_rect.x = lcrop_width;
      overlayR.src_rect.y = 0;
      overlayR.src_rect.w = gr_framebuffer[0].width - lcrop_width;
      overlayR.src_rect.h = gr_framebuffer[0].height;
      overlayR.dst_rect.x = 0;
      overlayR.dst_rect.y = 0;
      overlayR.dst_rect.w = rwidth;
      overlayR.dst_rect.h = height;
      overlayR.alpha = 0xFF;
      overlayR.flags = MDSS_MDP_RIGHT_MIXER;
      overlayR.transp_mask = MDP_TRANSP_NOP;
      overlayR.id = MSMFB_NEW_REQUEST;
      ret = ioctl(fb_fd, MSMFB_OVERLAY_SET, &overlayR);
      if (ret < 0) {
        perror("OverlayR Set Failed");
        return ret;
      }
      overlayR_id = overlayR.id;
    }

  }
  return 0;
}

int MinuiBackendOverlay::FreeOverlay() {
  int ret = 0;
  struct mdp_display_commit ext_commit;


  if (!IsDisplaySplit()) {
    if (overlayL_id != MSMFB_NEW_REQUEST) {
      ret = ioctl(fb_fd, MSMFB_OVERLAY_UNSET, &overlayL_id);
      if (ret) {
        perror("Overlay Unset Failed");
        overlayL_id = MSMFB_NEW_REQUEST;
        return ret;
      }
    }
  } else {

    if (overlayL_id != MSMFB_NEW_REQUEST) {
      ret = ioctl(fb_fd, MSMFB_OVERLAY_UNSET, &overlayL_id);
      if (ret) {
        perror("OverlayL Unset Failed");
        overlayL_id = MSMFB_NEW_REQUEST;
        return ret;
      }
    }

    if (overlayR_id != MSMFB_NEW_REQUEST) {
      ret = ioctl(fb_fd, MSMFB_OVERLAY_UNSET, &overlayR_id);
      if (ret) {
        perror("OverlayR Unset Failed");
        overlayR_id = MSMFB_NEW_REQUEST;
        return ret;
      }
    }
  }
  memset(&ext_commit, 0, sizeof(struct mdp_display_commit));
  ext_commit.flags = MDP_DISPLAY_COMMIT_OVERLAY;
  ext_commit.wait_for_finish = 1;
  ret = ioctl(fb_fd, MSMFB_DISPLAY_COMMIT, &ext_commit);
  if (ret < 0) {
    perror("ERROR: Clear MSMFB_DISPLAY_COMMIT failed!");
    overlayL_id = MSMFB_NEW_REQUEST;
    overlayR_id = MSMFB_NEW_REQUEST;
    return ret;
  }
  overlayL_id = MSMFB_NEW_REQUEST;
  overlayR_id = MSMFB_NEW_REQUEST;

  return 0;
}

int MinuiBackendOverlay::DisplayFrame() {
  int ret = 0;
  struct msmfb_overlay_data ovdataL, ovdataR;
  struct mdp_display_commit ext_commit;

  if (!IsDisplaySplit()) {
    if (overlayL_id == MSMFB_NEW_REQUEST) {
      perror("display_frame failed, no overlay\n");
      return -EINVAL;
    }

    memcpy(mem_info.mem_buf, gr_draw->data, frame_size);

    memset(&ovdataL, 0, sizeof(struct msmfb_overlay_data));

    ovdataL.id = overlayL_id;
    ovdataL.data.flags = 0;
    ovdataL.data.offset = 0;
    ovdataL.data.memory_id = mem_info.mem_fd;
    ret = ioctl(fb_fd, MSMFB_OVERLAY_PLAY, &ovdataL);
    if (ret < 0) {
      perror("overlay_display_frame failed, overlay play Failed\n");
      return ret;
    }
  } else {
    if (overlayL_id == MSMFB_NEW_REQUEST) {
      perror("display_frame failed, no overlayL \n");
      return -EINVAL;
    }
    memcpy(mem_info.mem_buf, gr_draw->data, frame_size);

    memset(&ovdataL, 0, sizeof(struct msmfb_overlay_data));

    ovdataL.id = overlayL_id;
    ovdataL.data.flags = 0;
    ovdataL.data.offset = 0;
    ovdataL.data.memory_id = mem_info.mem_fd;
    ret = ioctl(fb_fd, MSMFB_OVERLAY_PLAY, &ovdataL);
    if (ret < 0) {
      perror("overlay_display_frame failed, overlayL play Failed\n");
      return ret;
    }

    if (overlayR_id == MSMFB_NEW_REQUEST) {
      perror("display_frame failed, no overlayR \n");
      return -EINVAL;
    }
    memset(&ovdataR, 0, sizeof(struct msmfb_overlay_data));

    ovdataR.id = overlayR_id;
    ovdataR.data.flags = 0;
    ovdataR.data.offset = 0;
    ovdataR.data.memory_id = mem_info.mem_fd;
    ret = ioctl(fb_fd, MSMFB_OVERLAY_PLAY, &ovdataR);
    if (ret < 0) {
      perror("overlay_display_frame failed, overlayR play Failed\n");
      return ret;
    }
  }
  memset(&ext_commit, 0, sizeof(struct mdp_display_commit));
  ext_commit.flags = MDP_DISPLAY_COMMIT_OVERLAY;
  ext_commit.wait_for_finish = 1;
  ret = ioctl(fb_fd, MSMFB_DISPLAY_COMMIT, &ext_commit);
  if (ret < 0) {
    perror("overlay_display_frame failed, overlay commit Failed\n!");
  }

  return ret;
}
