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

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <fcntl.h>
#include <stdio.h>

#include <sys/cdefs.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>

#include <linux/fb.h>
#include <linux/kd.h>

#ifdef MSM_BSP
#include <linux/msm_mdp.h>
#include <linux/msm_ion.h>
#endif

#include "minui.h"
#include "graphics.h"
#include <pixelflinger/pixelflinger.h>

#define MDP_V4_0 400
#define MAX_DISPLAY_DIM  2048

static GRSurface* overlay_init(minui_backend*);
static GRSurface* overlay_flip(minui_backend*);
static void overlay_blank(minui_backend*, bool);
static void overlay_exit(minui_backend*);

static GRSurface gr_framebuffer;
static GRSurface* gr_draw = NULL;
static int displayed_buffer;

static fb_var_screeninfo vi;
static int fb_fd = -1;
static bool isMDP5 = false;
static int leftSplit = 0;
static int rightSplit = 0;
#define ALIGN(x, align) (((x) + ((align)-1)) & ~((align)-1))

static size_t frame_size = 0;

#ifdef MSM_BSP
typedef struct {
    unsigned char *mem_buf;
    int size;
    int ion_fd;
    int mem_fd;
    struct ion_handle_data handle_data;
} memInfo;

//Left and right overlay id
static int overlayL_id = MSMFB_NEW_REQUEST;
static int overlayR_id = MSMFB_NEW_REQUEST;

static memInfo mem_info;

static int map_mdp_pixel_format()
{
    if (gr_framebuffer.format == GGL_PIXEL_FORMAT_RGB_565)
        return MDP_RGB_565;
    else if (gr_framebuffer.format == GGL_PIXEL_FORMAT_BGRA_8888)
        return MDP_BGRA_8888;
    else if (gr_framebuffer.format == GGL_PIXEL_FORMAT_RGBA_8888)
        return MDP_RGBA_8888;
    else if (gr_framebuffer.format == GGL_PIXEL_FORMAT_RGBX_8888)
        return MDP_RGBA_8888;
    printf("No known pixel format for map_mdp_pixel_format, defaulting to MDP_RGB_565.\n");
    return MDP_RGB_565;
}
#endif // MSM_BSP

static minui_backend my_backend = {
    .init = overlay_init,
    .flip = overlay_flip,
    .blank = overlay_blank,
    .exit = overlay_exit,
};

bool target_has_overlay(char *version)
{
    int ret;
    int mdp_version;
    bool overlay_supported = false;

    if (strlen(version) >= 8) {
        if(!strncmp(version, "msmfb", strlen("msmfb"))) {
            char str_ver[4];
            memcpy(str_ver, version + strlen("msmfb"), 3);
            str_ver[3] = '\0';
            mdp_version = atoi(str_ver);
            if (mdp_version >= MDP_V4_0) {
                overlay_supported = true;
            }
        } else if (!strncmp(version, "mdssfb", strlen("mdssfb"))) {
            overlay_supported = true;
            isMDP5 = true;
        }
    }

    return overlay_supported;
}

minui_backend* open_overlay() {
    fb_fix_screeninfo fi;
    int fd;

    fd = open("/dev/graphics/fb0", O_RDWR);
    if (fd < 0) {
        perror("open_overlay cannot open fb0");
        return NULL;
    }

    if (ioctl(fd, FBIOGET_FSCREENINFO, &fi) < 0) {
        perror("failed to get fb0 info");
        close(fd);
        return NULL;
    }

    if (target_has_overlay(fi.id)) {
#ifdef MSM_BSP
        close(fd);
        return &my_backend;
#else
        printf("Overlay graphics may work (%s), but not enabled. Use TW_TARGET_USES_QCOM_BSP := true to enable.\n", fi.id);
#endif
    }
    close(fd);
    return NULL;
}

static void overlay_blank(minui_backend* backend __unused, bool blank)
{
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

#ifdef MSM_BSP
void setDisplaySplit(void) {
    char split[64] = {0};
    if (!isMDP5)
        return;
    FILE* fp = fopen("/sys/class/graphics/fb0/msm_fb_split", "r");
    if (fp) {
        //Format "left right" space as delimiter
        if(fread(split, sizeof(char), 64, fp)) {
            leftSplit = atoi(split);
            printf("Left Split=%d\n",leftSplit);
            char *rght = strpbrk(split, " ");
            if (rght)
                rightSplit = atoi(rght + 1);
            printf("Right Split=%d\n", rightSplit);
        }
    } else {
        printf("Failed to open mdss_fb_split node\n");
    }
    if (fp)
        fclose(fp);
}

int getLeftSplit(void) {
   //Default even split for all displays with high res
   int lSplit = vi.xres / 2;

   //Override if split published by driver
   if (leftSplit)
       lSplit = leftSplit;

   return lSplit;
}

int getRightSplit(void) {
   return rightSplit;
}

int free_ion_mem(void) {
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

int alloc_ion_mem(unsigned int size)
{
    int result;
    struct ion_fd_data fd_data;
    struct ion_allocation_data ionAllocData;

    mem_info.ion_fd = open("/dev/ion", O_RDWR|O_DSYNC);
    if (mem_info.ion_fd < 0) {
        perror("ERROR: Can't open ion ");
        return -errno;
    }

    ionAllocData.flags = 0;
    ionAllocData.len = size;
    ionAllocData.align = sysconf(_SC_PAGESIZE);
#ifdef NEW_ION_HEAP
    ionAllocData.heap_id_mask =
#else
    ionAllocData.heap_mask =
#endif
            ION_HEAP(ION_IOMMU_HEAP_ID) |
            ION_HEAP(ION_SYSTEM_CONTIG_HEAP_ID);

    result = ioctl(mem_info.ion_fd, ION_IOC_ALLOC,  &ionAllocData);
    if(result){
        perror("ION_IOC_ALLOC Failed ");
        close(mem_info.ion_fd);
        return result;
    }

    fd_data.handle = ionAllocData.handle;
    mem_info.handle_data.handle = ionAllocData.handle;
    result = ioctl(mem_info.ion_fd, ION_IOC_MAP, &fd_data);
    if (result) {
        perror("ION_IOC_MAP Failed ");
        free_ion_mem();
        return result;
    }
    mem_info.mem_buf = (unsigned char *)mmap(NULL, size, PROT_READ |
                PROT_WRITE, MAP_SHARED, fd_data.fd, 0);
    mem_info.mem_fd = fd_data.fd;

    if (!mem_info.mem_buf) {
        perror("ERROR: mem_buf MAP_FAILED ");
        free_ion_mem();
        return -ENOMEM;
    }

    return 0;
}

bool isDisplaySplit(void) {
    if (vi.xres > MAX_DISPLAY_DIM)
        return true;
    //check if right split is set by driver
    if (getRightSplit())
        return true;

    return false;
}

int allocate_overlay(int fd, GRSurface gr_fb)
{
    int ret = 0;

    if (!isDisplaySplit()) {
        // Check if overlay is already allocated
        if (MSMFB_NEW_REQUEST == overlayL_id) {
            struct mdp_overlay overlayL;

            memset(&overlayL, 0 , sizeof (struct mdp_overlay));

            /* Fill Overlay Data */
            overlayL.src.width  = ALIGN(gr_fb.width, 32);
            overlayL.src.height = gr_fb.height;
            overlayL.src.format = map_mdp_pixel_format();
            overlayL.src_rect.w = gr_fb.width;
            overlayL.src_rect.h = gr_fb.height;
            overlayL.dst_rect.w = gr_fb.width;
            overlayL.dst_rect.h = gr_fb.height;
            overlayL.alpha = 0xFF;
#ifdef BOARD_HAS_FLIPPED_SCREEN
            overlayL.flags = MDP_ROT_180;
#endif
            overlayL.transp_mask = MDP_TRANSP_NOP;
            overlayL.id = MSMFB_NEW_REQUEST;
            ret = ioctl(fd, MSMFB_OVERLAY_SET, &overlayL);
            if (ret < 0) {
                perror("Overlay Set Failed");
                return ret;
            }
            overlayL_id = overlayL.id;
        }
    } else {
        float xres = vi.xres;
        int lSplit = getLeftSplit();
        float lSplitRatio = lSplit / xres;
        float lCropWidth = gr_fb.width * lSplitRatio;
        int lWidth = lSplit;
        int rWidth = gr_fb.width - lSplit;
        int height = gr_fb.height;

        if (MSMFB_NEW_REQUEST == overlayL_id) {

            struct mdp_overlay overlayL;

            memset(&overlayL, 0 , sizeof (struct mdp_overlay));

            /* Fill OverlayL Data */
            overlayL.src.width  = ALIGN(gr_fb.width, 32);
            overlayL.src.height = gr_fb.height;
            overlayL.src.format = map_mdp_pixel_format();
            overlayL.src_rect.x = 0;
            overlayL.src_rect.y = 0;
            overlayL.src_rect.w = lCropWidth;
            overlayL.src_rect.h = gr_fb.height;
            overlayL.dst_rect.x = 0;
            overlayL.dst_rect.y = 0;
            overlayL.dst_rect.w = lWidth;
            overlayL.dst_rect.h = height;
            overlayL.alpha = 0xFF;
#ifdef BOARD_HAS_FLIPPED_SCREEN
            overlayL.flags = MDP_ROT_180;
#endif
            overlayL.transp_mask = MDP_TRANSP_NOP;
            overlayL.id = MSMFB_NEW_REQUEST;
            ret = ioctl(fd, MSMFB_OVERLAY_SET, &overlayL);
            if (ret < 0) {
                perror("OverlayL Set Failed");
                return ret;
            }
            overlayL_id = overlayL.id;
        }
        if (MSMFB_NEW_REQUEST == overlayR_id) {
            struct mdp_overlay overlayR;

            memset(&overlayR, 0 , sizeof (struct mdp_overlay));

            /* Fill OverlayR Data */
            overlayR.src.width  = ALIGN(gr_fb.width, 32);
            overlayR.src.height = gr_fb.height;
            overlayR.src.format = map_mdp_pixel_format();
            overlayR.src_rect.x = lCropWidth;
            overlayR.src_rect.y = 0;
            overlayR.src_rect.w = gr_fb.width - lCropWidth;
            overlayR.src_rect.h = gr_fb.height;
            overlayR.dst_rect.x = 0;
            overlayR.dst_rect.y = 0;
            overlayR.dst_rect.w = rWidth;
            overlayR.dst_rect.h = height;
            overlayR.alpha = 0xFF;
#ifdef BOARD_HAS_FLIPPED_SCREEN
            overlayR.flags = MDSS_MDP_RIGHT_MIXER | MDP_ROT_180;
#else
            overlayR.flags = MDSS_MDP_RIGHT_MIXER;
#endif
            overlayR.transp_mask = MDP_TRANSP_NOP;
            overlayR.id = MSMFB_NEW_REQUEST;
            ret = ioctl(fd, MSMFB_OVERLAY_SET, &overlayR);
            if (ret < 0) {
                perror("OverlayR Set Failed");
                return ret;
            }
            overlayR_id = overlayR.id;
        }

    }
    return 0;
}

int overlay_display_frame(int fd, void* data, size_t size)
{
    int ret = 0;
    struct msmfb_overlay_data ovdataL, ovdataR;
    struct mdp_display_commit ext_commit;

    if (!isDisplaySplit()) {
        if (overlayL_id == MSMFB_NEW_REQUEST) {
            perror("display_frame failed, no overlay\n");
            return -EINVAL;
        }

        memcpy(mem_info.mem_buf, data, size);

        memset(&ovdataL, 0, sizeof(struct msmfb_overlay_data));

        ovdataL.id = overlayL_id;
        ovdataL.data.flags = 0;
        ovdataL.data.offset = 0;
        ovdataL.data.memory_id = mem_info.mem_fd;
        ret = ioctl(fd, MSMFB_OVERLAY_PLAY, &ovdataL);
        if (ret < 0) {
            perror("overlay_display_frame failed, overlay play Failed\n");
            printf("%i, %i, %i, %i\n", ret, fb_fd, fd, errno);
            return ret;
        }
    } else {

        if (overlayL_id == MSMFB_NEW_REQUEST) {
            perror("display_frame failed, no overlayL \n");
            return -EINVAL;
        }

        memcpy(mem_info.mem_buf, data, size);

        memset(&ovdataL, 0, sizeof(struct msmfb_overlay_data));

        ovdataL.id = overlayL_id;
        ovdataL.data.flags = 0;
        ovdataL.data.offset = 0;
        ovdataL.data.memory_id = mem_info.mem_fd;
        ret = ioctl(fd, MSMFB_OVERLAY_PLAY, &ovdataL);
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
        ret = ioctl(fd, MSMFB_OVERLAY_PLAY, &ovdataR);
        if (ret < 0) {
            perror("overlay_display_frame failed, overlayR play Failed\n");
            return ret;
        }
    }
    memset(&ext_commit, 0, sizeof(struct mdp_display_commit));
    ext_commit.flags = MDP_DISPLAY_COMMIT_OVERLAY;
    ext_commit.wait_for_finish = 1;
    ret = ioctl(fd, MSMFB_DISPLAY_COMMIT, &ext_commit);
    if (ret < 0) {
        perror("overlay_display_frame failed, overlay commit Failed\n!");
    }

    return ret;
}

static GRSurface* overlay_flip(minui_backend* backend __unused) {
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
    // Copy from the in-memory surface to the framebuffer.
    overlay_display_frame(fb_fd, gr_draw->data, frame_size);
    return gr_draw;
}

int free_overlay(int fd)
{
    int ret = 0;
    struct mdp_display_commit ext_commit;

    if (!isDisplaySplit()) {
        if (overlayL_id != MSMFB_NEW_REQUEST) {
            ret = ioctl(fd, MSMFB_OVERLAY_UNSET, &overlayL_id);
            if (ret) {
                perror("Overlay Unset Failed");
                overlayL_id = MSMFB_NEW_REQUEST;
                return ret;
            }
        }
    } else {

        if (overlayL_id != MSMFB_NEW_REQUEST) {
            ret = ioctl(fd, MSMFB_OVERLAY_UNSET, &overlayL_id);
            if (ret) {
                perror("OverlayL Unset Failed");
            }
        }

        if (overlayR_id != MSMFB_NEW_REQUEST) {
            ret = ioctl(fd, MSMFB_OVERLAY_UNSET, &overlayR_id);
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
    ret = ioctl(fd, MSMFB_DISPLAY_COMMIT, &ext_commit);
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

static GRSurface* overlay_init(minui_backend* backend) {
    int fd = open("/dev/graphics/fb0", O_RDWR);
    if (fd == -1) {
        perror("cannot open fb0");
        return NULL;
    }

    fb_fix_screeninfo fi;
    if (ioctl(fd, FBIOGET_FSCREENINFO, &fi) < 0) {
        perror("failed to get fb0 info");
        close(fd);
        return NULL;
    }

    if (ioctl(fd, FBIOGET_VSCREENINFO, &vi) < 0) {
        perror("failed to get fb0 info");
        close(fd);
        return NULL;
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

    printf("fb0 reports (possibly inaccurate):\n"
           "  vi.bits_per_pixel = %d\n"
           "  vi.red.offset   = %3d   .length = %3d\n"
           "  vi.green.offset = %3d   .length = %3d\n"
           "  vi.blue.offset  = %3d   .length = %3d\n",
           vi.bits_per_pixel,
           vi.red.offset, vi.red.length,
           vi.green.offset, vi.green.length,
           vi.blue.offset, vi.blue.length);

    gr_framebuffer.width = vi.xres;
    gr_framebuffer.height = vi.yres;
    gr_framebuffer.row_bytes = fi.line_length;
    gr_framebuffer.pixel_bytes = vi.bits_per_pixel / 8;
    //gr_framebuffer.data = reinterpret_cast<uint8_t*>(bits);
    if (vi.bits_per_pixel == 16) {
        printf("setting GGL_PIXEL_FORMAT_RGB_565\n");
        gr_framebuffer.format = GGL_PIXEL_FORMAT_RGB_565;
    } else if (vi.red.offset == 8 || vi.red.offset == 16) {
        printf("setting GGL_PIXEL_FORMAT_BGRA_8888\n");
        gr_framebuffer.format = GGL_PIXEL_FORMAT_BGRA_8888;
    } else if (vi.red.offset == 0) {
        printf("setting GGL_PIXEL_FORMAT_RGBA_8888\n");
        gr_framebuffer.format = GGL_PIXEL_FORMAT_RGBA_8888;
    } else if (vi.red.offset == 24) {
        printf("setting GGL_PIXEL_FORMAT_RGBX_8888\n");
        gr_framebuffer.format = GGL_PIXEL_FORMAT_RGBX_8888;
    } else {
        if (vi.red.length == 8) {
            printf("No valid pixel format detected, trying GGL_PIXEL_FORMAT_RGBX_8888\n");
            gr_framebuffer.format = GGL_PIXEL_FORMAT_RGBX_8888;
        } else {
            printf("No valid pixel format detected, trying GGL_PIXEL_FORMAT_RGB_565\n");
            gr_framebuffer.format = GGL_PIXEL_FORMAT_RGB_565;
        }
    }

    frame_size = fi.line_length * vi.yres;

    gr_framebuffer.data = reinterpret_cast<uint8_t*>(calloc(frame_size, 1));
    if (gr_framebuffer.data == NULL) {
        perror("failed to calloc framebuffer");
        close(fd);
        return NULL;
    }

    gr_draw = &gr_framebuffer;
    fb_fd = fd;

    printf("framebuffer: %d (%d x %d)\n", fb_fd, gr_draw->width, gr_draw->height);

    overlay_blank(backend, true);
    overlay_blank(backend, false);

    if (!alloc_ion_mem(frame_size))
        allocate_overlay(fb_fd, gr_framebuffer);

    return gr_draw;
}

static void overlay_exit(minui_backend* backend __unused) {
    free_overlay(fb_fd);
    free_ion_mem();

    close(fb_fd);
    fb_fd = -1;

    if (gr_draw) {
        free(gr_draw->data);
        free(gr_draw);
    }
    gr_draw = NULL;
}
#else // MSM_BSP
static GRSurface* overlay_flip(minui_backend* backend __unused) {
    return NULL;
}

static GRSurface* overlay_init(minui_backend* backend __unused) {
    return NULL;
}

static void overlay_exit(minui_backend* backend __unused) {
    return;
}
#endif // MSM_BSP
