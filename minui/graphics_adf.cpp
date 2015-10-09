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

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/cdefs.h>
#include <sys/mman.h>

#include <adf/adf.h>

#include "graphics.h"

struct adf_surface_pdata {
    GRSurface base;
    int fd;
    __u32 offset;
    __u32 pitch;
};

struct adf_pdata {
    minui_backend base;
    int intf_fd;
    adf_id_t eng_id;
    __u32 format;

    unsigned int current_surface;
    unsigned int n_surfaces;
    adf_surface_pdata surfaces[2];
};

static GRSurface* adf_flip(minui_backend *backend);
static void adf_blank(minui_backend *backend, bool blank);

static int adf_surface_init(adf_pdata *pdata, drm_mode_modeinfo *mode, adf_surface_pdata *surf) {
    memset(surf, 0, sizeof(*surf));

    surf->fd = adf_interface_simple_buffer_alloc(pdata->intf_fd, mode->hdisplay,
            mode->vdisplay, pdata->format, &surf->offset, &surf->pitch);
    if (surf->fd < 0)
        return surf->fd;

    surf->base.width = mode->hdisplay;
    surf->base.height = mode->vdisplay;
    surf->base.row_bytes = surf->pitch;
    surf->base.pixel_bytes = (pdata->format == DRM_FORMAT_RGB565) ? 2 : 4;

    surf->base.data = reinterpret_cast<uint8_t*>(mmap(NULL,
                                                      surf->pitch * surf->base.height, PROT_WRITE,
                                                      MAP_SHARED, surf->fd, surf->offset));
    if (surf->base.data == MAP_FAILED) {
        close(surf->fd);
        return -errno;
    }

    return 0;
}

static int adf_interface_init(adf_pdata *pdata)
{
    adf_interface_data intf_data;
    int ret = 0;
    int err;

    err = adf_get_interface_data(pdata->intf_fd, &intf_data);
    if (err < 0)
        return err;

    err = adf_surface_init(pdata, &intf_data.current_mode, &pdata->surfaces[0]);
    if (err < 0) {
        fprintf(stderr, "allocating surface 0 failed: %s\n", strerror(-err));
        ret = err;
        goto done;
    }

    err = adf_surface_init(pdata, &intf_data.current_mode,
            &pdata->surfaces[1]);
    if (err < 0) {
        fprintf(stderr, "allocating surface 1 failed: %s\n", strerror(-err));
        memset(&pdata->surfaces[1], 0, sizeof(pdata->surfaces[1]));
        pdata->n_surfaces = 1;
    } else {
        pdata->n_surfaces = 2;
    }

done:
    adf_free_interface_data(&intf_data);
    return ret;
}

static int adf_device_init(adf_pdata *pdata, adf_device *dev)
{
    adf_id_t intf_id;
    int intf_fd;
    int err;

    err = adf_find_simple_post_configuration(dev, &pdata->format, 1, &intf_id,
            &pdata->eng_id);
    if (err < 0)
        return err;

    err = adf_device_attach(dev, pdata->eng_id, intf_id);
    if (err < 0 && err != -EALREADY)
        return err;

    pdata->intf_fd = adf_interface_open(dev, intf_id, O_RDWR);
    if (pdata->intf_fd < 0)
        return pdata->intf_fd;

    err = adf_interface_init(pdata);
    if (err < 0) {
        close(pdata->intf_fd);
        pdata->intf_fd = -1;
    }

    return err;
}

static GRSurface* adf_init(minui_backend *backend)
{
    adf_pdata *pdata = (adf_pdata *)backend;
    adf_id_t *dev_ids = NULL;
    ssize_t n_dev_ids, i;
    GRSurface* ret;

#if defined(RECOVERY_ABGR)
    pdata->format = DRM_FORMAT_ABGR8888;
#elif defined(RECOVERY_BGRA)
    pdata->format = DRM_FORMAT_BGRA8888;
#elif defined(RECOVERY_RGBX)
    pdata->format = DRM_FORMAT_RGBX8888;
#else
    pdata->format = DRM_FORMAT_RGB565;
#endif

    n_dev_ids = adf_devices(&dev_ids);
    if (n_dev_ids == 0) {
        return NULL;
    } else if (n_dev_ids < 0) {
        fprintf(stderr, "enumerating adf devices failed: %s\n",
                strerror(-n_dev_ids));
        return NULL;
    }

    pdata->intf_fd = -1;

    for (i = 0; i < n_dev_ids && pdata->intf_fd < 0; i++) {
        adf_device dev;

        int err = adf_device_open(dev_ids[i], O_RDWR, &dev);
        if (err < 0) {
            fprintf(stderr, "opening adf device %u failed: %s\n", dev_ids[i],
                    strerror(-err));
            continue;
        }

        err = adf_device_init(pdata, &dev);
        if (err < 0)
            fprintf(stderr, "initializing adf device %u failed: %s\n",
                    dev_ids[i], strerror(-err));

        adf_device_close(&dev);
    }

    free(dev_ids);

    if (pdata->intf_fd < 0)
        return NULL;

    ret = adf_flip(backend);

    adf_blank(backend, true);
    adf_blank(backend, false);

    return ret;
}

static GRSurface* adf_flip(minui_backend *backend)
{
    adf_pdata *pdata = (adf_pdata *)backend;
    adf_surface_pdata *surf = &pdata->surfaces[pdata->current_surface];

    int fence_fd = adf_interface_simple_post(pdata->intf_fd, pdata->eng_id,
            surf->base.width, surf->base.height, pdata->format, surf->fd,
            surf->offset, surf->pitch, -1);
    if (fence_fd >= 0)
        close(fence_fd);

    pdata->current_surface = (pdata->current_surface + 1) % pdata->n_surfaces;
    return &pdata->surfaces[pdata->current_surface].base;
}

static void adf_blank(minui_backend *backend, bool blank)
{
    adf_pdata *pdata = (adf_pdata *)backend;
    adf_interface_blank(pdata->intf_fd,
            blank ? DRM_MODE_DPMS_OFF : DRM_MODE_DPMS_ON);
}

static void adf_surface_destroy(adf_surface_pdata *surf)
{
    munmap(surf->base.data, surf->pitch * surf->base.height);
    close(surf->fd);
}

static void adf_exit(minui_backend *backend)
{
    adf_pdata *pdata = (adf_pdata *)backend;
    unsigned int i;

    for (i = 0; i < pdata->n_surfaces; i++)
        adf_surface_destroy(&pdata->surfaces[i]);
    if (pdata->intf_fd >= 0)
        close(pdata->intf_fd);
    free(pdata);
}

minui_backend *open_adf()
{
    adf_pdata* pdata = reinterpret_cast<adf_pdata*>(calloc(1, sizeof(*pdata)));
    if (!pdata) {
        perror("allocating adf backend failed");
        return NULL;
    }

    pdata->base.init = adf_init;
    pdata->base.flip = adf_flip;
    pdata->base.blank = adf_blank;
    pdata->base.exit = adf_exit;
    return &pdata->base;
}
