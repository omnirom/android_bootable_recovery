/*
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>

#include <linux/fb.h>
#include <linux/kd.h>

#ifdef MSM_BSP
#include <linux/msm_mdp.h>
#include <linux/msm_ion.h>
#endif

#include <pixelflinger/pixelflinger.h>

#include "minui.h"

#define MDP_V4_0 400

#ifdef MSM_BSP
#define ALIGN(x, align) (((x) + ((align)-1)) & ~((align)-1))

typedef struct {
    unsigned char *mem_buf;
    int size;
    int ion_fd;
    int mem_fd;
    struct ion_handle_data handle_data;
} memInfo;

static int overlay_id = MSMFB_NEW_REQUEST;
static memInfo mem_info;

static int map_mdp_pixel_format()
{
    int format = MDP_RGB_565;
#if defined(RECOVERY_BGRA)
    format = MDP_BGRA_8888;
#elif defined(RECOVERY_RGBX)
    format = MDP_RGBA_8888;
#endif
    return format;
}

static bool overlay_supported = false;

bool target_has_overlay(char *version)
{
    int ret;
    int mdp_version;

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
        }
    }

    return overlay_supported;
}

int free_ion_mem(void) {
    if (!overlay_supported)
        return -EINVAL;

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
    if (!overlay_supported)
        return -EINVAL;
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

int allocate_overlay(int fd, GGLSurface gr_fb[])
{
    if (!overlay_supported)
        return -EINVAL;

    struct mdp_overlay overlay;
    int ret = 0;

    memset(&overlay, 0 , sizeof (struct mdp_overlay));

    /* Fill Overlay Data */

    overlay.src.width  = ALIGN(gr_fb[0].width, 32);
    overlay.src.height = gr_fb[0].height;
    overlay.src.format = map_mdp_pixel_format();
    overlay.src_rect.w = gr_fb[0].width;
    overlay.src_rect.h = gr_fb[0].height;
    overlay.dst_rect.w = gr_fb[0].width;
    overlay.dst_rect.h = gr_fb[0].height;
    overlay.alpha = 0xFF;
    overlay.transp_mask = MDP_TRANSP_NOP;
    overlay.id = MSMFB_NEW_REQUEST;
    ret = ioctl(fd, MSMFB_OVERLAY_SET, &overlay);
    if (ret < 0) {
        perror("Overlay Set Failed");
        return ret;
    }

    overlay_id = overlay.id;
    return 0;
}

int free_overlay(int fd)
{
    if (!overlay_supported)
        return -EINVAL;

    int ret = 0;
    struct mdp_display_commit ext_commit;

    if (overlay_id != MSMFB_NEW_REQUEST) {
        ret = ioctl(fd, MSMFB_OVERLAY_UNSET, &overlay_id);
        if (ret) {
            perror("Overlay Unset Failed");
            overlay_id = MSMFB_NEW_REQUEST;
            return ret;
        }

        memset(&ext_commit, 0, sizeof(struct mdp_display_commit));
        ext_commit.flags = MDP_DISPLAY_COMMIT_OVERLAY;
        ext_commit.wait_for_finish = 1;
        ret = ioctl(fd, MSMFB_DISPLAY_COMMIT, &ext_commit);
        if (ret < 0) {
            perror("ERROR: Clear MSMFB_DISPLAY_COMMIT failed!");
            overlay_id = MSMFB_NEW_REQUEST;
            return ret;
        }

        overlay_id = MSMFB_NEW_REQUEST;
    }
    return 0;
}

int overlay_display_frame(int fd, GGLubyte* data, size_t size)
{
    if (!overlay_supported)
        return -EINVAL;
    int ret = 0;
    struct msmfb_overlay_data ovdata;
    struct mdp_display_commit ext_commit;

    if (overlay_id == MSMFB_NEW_REQUEST) {
        perror("display_frame failed, no overlay\n");
        return 0;
    }

    memcpy(mem_info.mem_buf, data, size);

    memset(&ovdata, 0, sizeof(struct msmfb_overlay_data));

    ovdata.id = overlay_id;
    ovdata.data.flags = 0;
    ovdata.data.offset = 0;
    ovdata.data.memory_id = mem_info.mem_fd;
    ret = ioctl(fd, MSMFB_OVERLAY_PLAY, &ovdata);
    if (ret < 0) {
        perror("overlay_display_frame failed, overlay play Failed\n");
        return 0;
    }

    memset(&ext_commit, 0, sizeof(struct mdp_display_commit));
    ext_commit.flags = MDP_DISPLAY_COMMIT_OVERLAY;
    ext_commit.wait_for_finish = 1;
    ret = ioctl(fd, MSMFB_DISPLAY_COMMIT, &ext_commit);
    if (ret < 0) {
        perror("overlay_display_frame failed, overlay commit Failed\n!");
        return 0;
    }

    return 0;
}

#else

bool target_has_overlay(char *version) {
    return false;
}

int free_ion_mem(void) {
    return -EINVAL;
}

int alloc_ion_mem(unsigned int size)
{
    return -EINVAL;
}

int allocate_overlay(int fd, GGLSurface gr_fb[])
{
    return -EINVAL;
}

int free_overlay(int fd)
{
    return -EINVAL;
}

int overlay_display_frame(int fd, GGLubyte* data, size_t size)
{
    return -EINVAL;
}

#endif //#ifdef MSM_BSP
