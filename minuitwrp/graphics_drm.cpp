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

#include <drm_fourcc.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/cdefs.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "minui.h"
#include "graphics.h"
#include <pixelflinger/pixelflinger.h>

#define ARRAY_SIZE(A) (sizeof(A)/sizeof(*(A)))

struct drm_surface {
    GRSurface base;
    uint32_t fb_id;
    uint32_t handle;
};

static drm_surface *drm_surfaces[2];
static int current_buffer;

static drmModeCrtc *main_monitor_crtc;
static drmModeConnector *main_monitor_connector;

static int drm_fd = -1;

static void drm_disable_crtc(int drm_fd, drmModeCrtc *crtc) {
    if (crtc) {
        drmModeSetCrtc(drm_fd, crtc->crtc_id,
                       0, // fb_id
                       0, 0,  // x,y
                       NULL,  // connectors
                       0,     // connector_count
                       NULL); // mode
    }
}

static void drm_enable_crtc(int drm_fd, drmModeCrtc *crtc,
                            struct drm_surface *surface) {
    int32_t ret;

    ret = drmModeSetCrtc(drm_fd, crtc->crtc_id,
                         surface->fb_id,
                         0, 0,  // x,y
                         &main_monitor_connector->connector_id,
                         1,  // connector_count
                         &main_monitor_crtc->mode);

    if (ret)
        printf("drmModeSetCrtc failed ret=%d\n", ret);
}

static void drm_blank(minui_backend* backend __unused, bool blank) {
    if (blank)
        drm_disable_crtc(drm_fd, main_monitor_crtc);
    else
        drm_enable_crtc(drm_fd, main_monitor_crtc,
                        drm_surfaces[current_buffer]);
}

static void drm_destroy_surface(struct drm_surface *surface) {
    struct drm_gem_close gem_close;
    int ret;

    if(!surface)
        return;

    if (surface->base.data)
        munmap(surface->base.data,
               surface->base.row_bytes * surface->base.height);

    if (surface->fb_id) {
        ret = drmModeRmFB(drm_fd, surface->fb_id);
        if (ret)
            printf("drmModeRmFB failed ret=%d\n", ret);
    }

    if (surface->handle) {
        memset(&gem_close, 0, sizeof(gem_close));
        gem_close.handle = surface->handle;

        ret = drmIoctl(drm_fd, DRM_IOCTL_GEM_CLOSE, &gem_close);
        if (ret)
            printf("DRM_IOCTL_GEM_CLOSE failed ret=%d\n", ret);
    }

    free(surface);
}

static int drm_format_to_bpp(uint32_t format) {
    switch(format) {
        case DRM_FORMAT_ABGR8888:
        case DRM_FORMAT_BGRA8888:
        case DRM_FORMAT_RGBX8888:
        case DRM_FORMAT_BGRX8888:
        case DRM_FORMAT_XBGR8888:
        case DRM_FORMAT_ARGB8888:
        case DRM_FORMAT_XRGB8888:
            return 32;
        case DRM_FORMAT_RGB565:
            return 16;
        default:
            printf("Unknown format %d\n", format);
            return 32;
    }
}

static drm_surface *drm_create_surface(int width, int height) {
    struct drm_surface *surface;
    struct drm_mode_create_dumb create_dumb;
    uint32_t format;
    __u32 base_format;
    int ret;

    surface = (struct drm_surface*)calloc(1, sizeof(*surface));
    if (!surface) {
        printf("Can't allocate memory\n");
        return NULL;
    }

#if defined(RECOVERY_ABGR)
    format = DRM_FORMAT_RGBA8888;
    base_format = GGL_PIXEL_FORMAT_RGBA_8888;
    printf("setting DRM_FORMAT_RGBA8888 and GGL_PIXEL_FORMAT_RGBA_8888\n");
#elif defined(RECOVERY_BGRA)
    format = DRM_FORMAT_ARGB8888;
    base_format = GGL_PIXEL_FORMAT_BGRA_8888;
    printf("setting DRM_FORMAT_ARGB8888 and GGL_PIXEL_FORMAT_BGRA_8888, GGL_PIXEL_FORMAT may not match!\n");
#elif defined(RECOVERY_RGBA)
    format = DRM_FORMAT_ABGR8888;
    base_format = GGL_PIXEL_FORMAT_BGRA_8888;
    printf("setting DRM_FORMAT_ABGR8888 and GGL_PIXEL_FORMAT_BGRA_8888, GGL_PIXEL_FORMAT may not match!\n");
#elif defined(RECOVERY_RGBX)
    format = DRM_FORMAT_XBGR8888;
    base_format = GGL_PIXEL_FORMAT_BGRA_8888;
    printf("setting DRM_FORMAT_XBGR8888 and GGL_PIXEL_FORMAT_BGRA_8888, GGL_PIXEL_FORMAT may not match!\n");
#else
    format = DRM_FORMAT_RGB565;
    base_format = GGL_PIXEL_FORMAT_BGRA_8888;
    printf("setting DRM_FORMAT_RGB565 and GGL_PIXEL_FORMAT_RGB_565\n");
#endif

    memset(&create_dumb, 0, sizeof(create_dumb));
    create_dumb.height = height;
    create_dumb.width = width;
    create_dumb.bpp = drm_format_to_bpp(format);
    create_dumb.flags = 0;

    ret = drmIoctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_dumb);
    if (ret) {
        printf("DRM_IOCTL_MODE_CREATE_DUMB failed ret=%d\n",ret);
        drm_destroy_surface(surface);
        return NULL;
    }
    surface->handle = create_dumb.handle;

    uint32_t handles[4], pitches[4], offsets[4];

    handles[0] = surface->handle;
    pitches[0] = create_dumb.pitch;
    offsets[0] = 0;

    ret = drmModeAddFB2(drm_fd, width, height,
            format, handles, pitches, offsets,
            &(surface->fb_id), 0);
    if (ret) {
        printf("drmModeAddFB2 failed ret=%d\n", ret);
        drm_destroy_surface(surface);
        return NULL;
    }

    struct drm_mode_map_dumb map_dumb;
    memset(&map_dumb, 0, sizeof(map_dumb));
    map_dumb.handle = create_dumb.handle;
    ret = drmIoctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &map_dumb);
    if (ret) {
        printf("DRM_IOCTL_MODE_MAP_DUMB failed ret=%d\n",ret);
        drm_destroy_surface(surface);
        return NULL;;
    }

    surface->base.height = height;
    surface->base.width = width;
    surface->base.row_bytes = create_dumb.pitch;
    surface->base.pixel_bytes = create_dumb.bpp / 8;
    surface->base.format = base_format;
    surface->base.data = (unsigned char*)
                         mmap(NULL,
                              surface->base.height * surface->base.row_bytes,
                              PROT_READ | PROT_WRITE, MAP_SHARED,
                              drm_fd, map_dumb.offset);
    if (surface->base.data == MAP_FAILED) {
        perror("mmap() failed");
        drm_destroy_surface(surface);
        return NULL;
    }

    return surface;
}

static drmModeCrtc *find_crtc_for_connector(int fd,
                            drmModeRes *resources,
                            drmModeConnector *connector) {
    int i, j;
    drmModeEncoder *encoder;
    int32_t crtc;

    /*
     * Find the encoder. If we already have one, just use it.
     */
    if (connector->encoder_id)
        encoder = drmModeGetEncoder(fd, connector->encoder_id);
    else
        encoder = NULL;

    if (encoder && encoder->crtc_id) {
        crtc = encoder->crtc_id;
        drmModeFreeEncoder(encoder);
        return drmModeGetCrtc(fd, crtc);
    }

    /*
     * Didn't find anything, try to find a crtc and encoder combo.
     */
    crtc = -1;
    for (i = 0; i < connector->count_encoders; i++) {
        encoder = drmModeGetEncoder(fd, connector->encoders[i]);

        if (encoder) {
            for (j = 0; j < resources->count_crtcs; j++) {
                if (!(encoder->possible_crtcs & (1 << j)))
                    continue;
                crtc = resources->crtcs[j];
                break;
            }
            if (crtc >= 0) {
                drmModeFreeEncoder(encoder);
                return drmModeGetCrtc(fd, crtc);
            }
        }
    }

    return NULL;
}

static drmModeConnector *find_used_connector_by_type(int fd,
                                 drmModeRes *resources,
                                 unsigned type) {
    int i;
    for (i = 0; i < resources->count_connectors; i++) {
        drmModeConnector *connector;

        connector = drmModeGetConnector(fd, resources->connectors[i]);
        if (connector) {
            if ((connector->connector_type == type) &&
                    (connector->connection == DRM_MODE_CONNECTED) &&
                    (connector->count_modes > 0))
                return connector;

            drmModeFreeConnector(connector);
        }
    }
    return NULL;
}

static drmModeConnector *find_first_connected_connector(int fd,
                             drmModeRes *resources) {
    int i;
    for (i = 0; i < resources->count_connectors; i++) {
        drmModeConnector *connector;

        connector = drmModeGetConnector(fd, resources->connectors[i]);
        if (connector) {
            if ((connector->count_modes > 0) &&
                    (connector->connection == DRM_MODE_CONNECTED))
                return connector;

            drmModeFreeConnector(connector);
        }
    }
    return NULL;
}

static drmModeConnector *find_main_monitor(int fd, drmModeRes *resources,
        uint32_t *mode_index) {
    unsigned i = 0;
    int modes;
    /* Look for LVDS/eDP/DSI connectors. Those are the main screens. */
    unsigned kConnectorPriority[] = {
        DRM_MODE_CONNECTOR_LVDS,
        DRM_MODE_CONNECTOR_eDP,
        DRM_MODE_CONNECTOR_DSI,
    };

    drmModeConnector *main_monitor_connector = NULL;
    do {
        main_monitor_connector = find_used_connector_by_type(fd,
                                         resources,
                                         kConnectorPriority[i]);
        i++;
    } while (!main_monitor_connector && i < ARRAY_SIZE(kConnectorPriority));

    /* If we didn't find a connector, grab the first one that is connected. */
    if (!main_monitor_connector)
        main_monitor_connector =
                find_first_connected_connector(fd, resources);

    /* If we still didn't find a connector, give up and return. */
    if (!main_monitor_connector)
        return NULL;

    *mode_index = 0;
    for (modes = 0; modes < main_monitor_connector->count_modes; modes++) {
        if (main_monitor_connector->modes[modes].type &
                DRM_MODE_TYPE_PREFERRED) {
            *mode_index = modes;
            break;
        }
    }

    return main_monitor_connector;
}

static void disable_non_main_crtcs(int fd,
                    drmModeRes *resources,
                    drmModeCrtc* main_crtc) {
    int i;
    drmModeCrtc* crtc;

    for (i = 0; i < resources->count_connectors; i++) {
        drmModeConnector *connector;

        connector = drmModeGetConnector(fd, resources->connectors[i]);
        crtc = find_crtc_for_connector(fd, resources, connector);
        if (crtc->crtc_id != main_crtc->crtc_id)
            drm_disable_crtc(fd, crtc);
        drmModeFreeCrtc(crtc);
    }
}

static GRSurface* drm_init(minui_backend* backend __unused) {
    drmModeRes *res = NULL;
    uint32_t selected_mode;
    char *dev_name;
    int width, height;
    int ret, i;

    /* Consider DRM devices in order. */
    for (i = 0; i < DRM_MAX_MINOR; i++) {
        uint64_t cap = 0;

        ret = asprintf(&dev_name, DRM_DEV_NAME, DRM_DIR_NAME, i);
        if (ret < 0)
            continue;

        drm_fd = open(dev_name, O_RDWR, 0);
        free(dev_name);
        if (drm_fd < 0)
            continue;

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
        if (res->count_crtcs > 0 && res->count_connectors > 0)
            if (find_first_connected_connector(drm_fd, res))
                break;

        drmModeFreeResources(res);
        close(drm_fd);
        res = NULL;
    }

    if (drm_fd < 0 || res == NULL) {
        perror("cannot find/open a drm device");
        return NULL;
    }

    main_monitor_connector = find_main_monitor(drm_fd,
            res, &selected_mode);

    if (!main_monitor_connector) {
        printf("main_monitor_connector not found\n");
        drmModeFreeResources(res);
        close(drm_fd);
        return NULL;
    }

    main_monitor_crtc = find_crtc_for_connector(drm_fd, res,
                                                main_monitor_connector);

    if (!main_monitor_crtc) {
        printf("main_monitor_crtc not found\n");
        drmModeFreeResources(res);
        close(drm_fd);
        return NULL;
    }

    disable_non_main_crtcs(drm_fd,
                           res, main_monitor_crtc);

    main_monitor_crtc->mode = main_monitor_connector->modes[selected_mode];

    width = main_monitor_crtc->mode.hdisplay;
    height = main_monitor_crtc->mode.vdisplay;

    drmModeFreeResources(res);

    drm_surfaces[0] = drm_create_surface(width, height);
    drm_surfaces[1] = drm_create_surface(width, height);
    if (!drm_surfaces[0] || !drm_surfaces[1]) {
        drm_destroy_surface(drm_surfaces[0]);
        drm_destroy_surface(drm_surfaces[1]);
        drmModeFreeResources(res);
        close(drm_fd);
        return NULL;
    }

    current_buffer = 0;

    drm_enable_crtc(drm_fd, main_monitor_crtc, drm_surfaces[1]);

    return &(drm_surfaces[0]->base);
}

static GRSurface* drm_flip(minui_backend* backend __unused) {
    int ret;

    ret = drmModePageFlip(drm_fd, main_monitor_crtc->crtc_id,
                          drm_surfaces[current_buffer]->fb_id, 0, NULL);
    if (ret < 0) {
        printf("drmModePageFlip failed ret=%d\n", ret);
        return NULL;
    }
    current_buffer = 1 - current_buffer;
    return &(drm_surfaces[current_buffer]->base);
}

static void drm_exit(minui_backend* backend __unused) {
    drm_disable_crtc(drm_fd, main_monitor_crtc);
    drm_destroy_surface(drm_surfaces[0]);
    drm_destroy_surface(drm_surfaces[1]);
    drmModeFreeCrtc(main_monitor_crtc);
    drmModeFreeConnector(main_monitor_connector);
    close(drm_fd);
    drm_fd = -1;
}

static minui_backend drm_backend = {
    .init = drm_init,
    .flip = drm_flip,
    .blank = drm_blank,
    .exit = drm_exit,
};

minui_backend* open_drm() {
    return &drm_backend;
}
