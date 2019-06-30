/*
 * Copyright (C) 2007 The Android Open Source Project
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

#include <fcntl.h>
#include <stdio.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>

#include <linux/fb.h>
#include <linux/kd.h>

#include <time.h>

#include <cutils/properties.h>
#include <pixelflinger/pixelflinger.h>
#include "../gui/placement.h"
#include "minui.h"
#include "graphics.h"
// For std::min and std::max
#include <algorithm>

struct GRFont {
    GRSurface* texture;
    int cwidth;
    int cheight;
};

static minui_backend* gr_backend = NULL;

static int overscan_percent = OVERSCAN_PERCENT;
static int overscan_offset_x = 0;
static int overscan_offset_y = 0;

static unsigned char gr_current_r = 255;
static unsigned char gr_current_g = 255;
static unsigned char gr_current_b = 255;

GRSurface* gr_draw = NULL;

static GGLContext *gr_context = 0;
GGLSurface gr_mem_surface;
static int gr_is_curr_clr_opaque = 0;

unsigned int gr_rotation = 0;

int gr_textEx_scaleW(int x, int y, const char *s, void* pFont, int max_width, int placement, int scale)
{
    GGLContext *gl = gr_context;
    void* vfont = pFont;
    GRFont *font = (GRFont*) pFont;
    int y_scale = 0, measured_width, measured_height, new_height;

    if (!s || strlen(s) == 0 || !font)
        return 0;

    measured_height = gr_ttf_getMaxFontHeight(font);

    if (scale) {
        measured_width = gr_ttf_measureEx(s, vfont);
        if (measured_width > max_width) {
            // Adjust font size down until the text fits
            void *new_font = gr_ttf_scaleFont(vfont, max_width, measured_width);
            if (!new_font) {
                printf("gr_textEx_scaleW new_font is NULL\n");
                return 0;
            }
            measured_width = gr_ttf_measureEx(s, new_font);
            // These next 2 lines adjust the y point based on the new font's height
            new_height = gr_ttf_getMaxFontHeight(new_font);
            y_scale = (measured_height - new_height) / 2;
            vfont = new_font;
        }
    } else
        measured_width = gr_ttf_measureEx(s, vfont);

    int x_adj = measured_width;
    if (measured_width > max_width)
        x_adj = max_width;

    if (placement != TOP_LEFT && placement != BOTTOM_LEFT && placement != TEXT_ONLY_RIGHT) {
        if (placement == CENTER || placement == CENTER_X_ONLY)
            x -= (x_adj / 2);
        else
            x -= x_adj;
    }

    if (placement != TOP_LEFT && placement != TOP_RIGHT) {
        if (placement == CENTER || placement == TEXT_ONLY_RIGHT)
            y -= (measured_height / 2);
        else if (placement == BOTTOM_LEFT || placement == BOTTOM_RIGHT)
            y -= measured_height;
    }
    return gr_ttf_textExWH(gl, x, y + y_scale, s, vfont, measured_width + x, -1, gr_draw);
}

void gr_clip(int x, int y, int w, int h)
{
    GGLContext *gl = gr_context;

    switch (gr_rotation) {
        case 90:
            gl->scissor(gl, gr_draw->width - y - h, x, h, w);
            break;
        case 180:
            gl->scissor(gl, gr_draw->width - x - w, gr_draw->height - y - h, w, h);
            break;
        case 270:
            gl->scissor(gl, y, gr_draw->height - x - w, h, w);
            break;
        default:
            gl->scissor(gl, x, y, w, h);
            break;
    }
    gl->enable(gl, GGL_SCISSOR_TEST);
}

void gr_noclip()
{
    GGLContext *gl = gr_context;
    gl->scissor(gl, 0, 0,
                gr_draw->width - 2 * overscan_offset_x,
                gr_draw->height - 2 * overscan_offset_y);
    gl->disable(gl, GGL_SCISSOR_TEST);
}

void gr_line(int x0, int y0, int x1, int y1, int width)
{
    GGLContext *gl = gr_context;
    int x0_disp, y0_disp, x1_disp, y1_disp;

    x0_disp = ROTATION_X_DISP(x0, y0, gr_draw->width);
    y0_disp = ROTATION_Y_DISP(x0, y0, gr_draw->height);
    x1_disp = ROTATION_X_DISP(x1, y1, gr_draw->width);
    y1_disp = ROTATION_Y_DISP(x1, y1, gr_draw->height);

    if(gr_is_curr_clr_opaque)
        gl->disable(gl, GGL_BLEND);

    const int coords0[2] = { x0_disp << 4, y0_disp << 4 };
    const int coords1[2] = { x1_disp << 4, y1_disp << 4 };
    gl->linex(gl, coords0, coords1, width << 4);

    if(gr_is_curr_clr_opaque)
        gl->enable(gl, GGL_BLEND);
}

gr_surface gr_render_circle(int radius, unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
    int rx, ry;
    GGLSurface *surface;
    const int diameter = radius*2 + 1;
    const int radius_check = radius*radius + radius*0.8;
    const uint32_t px = (a << 24) | (b << 16) | (g << 8) | r;
    uint32_t *data;

    surface = (GGLSurface *)malloc(sizeof(GGLSurface));
    memset(surface, 0, sizeof(GGLSurface));

    data = (uint32_t *)malloc(diameter * diameter * 4);
    memset(data, 0, diameter * diameter * 4);

    surface->version = sizeof(surface);
    surface->width = diameter;
    surface->height = diameter;
    surface->stride = diameter;
    surface->data = (GGLubyte*)data;
#if defined(RECOVERY_BGRA)
    surface->format = GGL_PIXEL_FORMAT_BGRA_8888;
#else
    surface->format = GGL_PIXEL_FORMAT_RGBA_8888;
#endif

    for(ry = -radius; ry <= radius; ++ry)
        for(rx = -radius; rx <= radius; ++rx)
            if(rx*rx+ry*ry <= radius_check)
                *(data + diameter*(radius + ry) + (radius+rx)) = px;

    return (gr_surface)surface;
}

void gr_color(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
    GGLContext *gl = gr_context;
    GGLint color[4];
#if defined(RECOVERY_ABGR) || defined(RECOVERY_BGRA)
    color[0] = ((b << 8) | r) + 1;
    color[1] = ((g << 8) | g) + 1;
    color[2] = ((r << 8) | b) + 1;
    color[3] = ((a << 8) | a) + 1;
#else
    color[0] = ((r << 8) | r) + 1;
    color[1] = ((g << 8) | g) + 1;
    color[2] = ((b << 8) | b) + 1;
    color[3] = ((a << 8) | a) + 1;
#endif
    gl->color4xv(gl, color);

    gr_is_curr_clr_opaque = (a == 255);
}

void gr_clear()
{
    if (gr_draw->pixel_bytes == 2) {
        gr_fill(0, 0, gr_fb_width(), gr_fb_height());
        return;
    }

    // This code only works on 32bpp devices
    if (gr_current_r == gr_current_g && gr_current_r == gr_current_b) {
        memset(gr_draw->data, gr_current_r, gr_draw->height * gr_draw->row_bytes);
    } else {
        unsigned char* px = gr_draw->data;
        for (int y = 0; y < gr_draw->height; ++y) {
            for (int x = 0; x < gr_draw->width; ++x) {
                *px++ = gr_current_r;
                *px++ = gr_current_g;
                *px++ = gr_current_b;
                px++;
            }
            px += gr_draw->row_bytes - (gr_draw->width * gr_draw->pixel_bytes);
        }
    }
}

void gr_fill(int x, int y, int w, int h)
{
    GGLContext *gl = gr_context;
    int x0_disp, y0_disp, x1_disp, y1_disp;
    int l_disp, r_disp, t_disp, b_disp;

    if(gr_is_curr_clr_opaque)
        gl->disable(gl, GGL_BLEND);

    x0_disp = ROTATION_X_DISP(x, y, gr_draw->width);
    y0_disp = ROTATION_Y_DISP(x, y, gr_draw->height);
    x1_disp = ROTATION_X_DISP(x + w, y + h, gr_draw->width);
    y1_disp = ROTATION_Y_DISP(x + w, y + h, gr_draw->height);
    l_disp = std::min(x0_disp, x1_disp);
    r_disp = std::max(x0_disp, x1_disp);
    t_disp = std::min(y0_disp, y1_disp);
    b_disp = std::max(y0_disp, y1_disp);

    gl->recti(gl, l_disp, t_disp, r_disp, b_disp);

    if(gr_is_curr_clr_opaque)
        gl->enable(gl, GGL_BLEND);
}

void gr_blit(gr_surface source, int sx, int sy, int w, int h, int dx, int dy)
{
    if (gr_context == NULL) {
        return;
    }

    GGLContext *gl = gr_context;
    GGLSurface *surface = (GGLSurface*)source;

    if(surface->format == GGL_PIXEL_FORMAT_RGBX_8888)
        gl->disable(gl, GGL_BLEND);

    int dx0_disp, dy0_disp, dx1_disp, dy1_disp;
    int l_disp, r_disp, t_disp, b_disp;

    // Figuring out display coordinates works for gr_rotation == 0 too,
    // and isn't as expensive as allocating and rotating another surface,
    // so we do this anyway.
    dx0_disp = ROTATION_X_DISP(dx, dy, gr_draw->width);
    dy0_disp = ROTATION_Y_DISP(dx, dy, gr_draw->height);
    dx1_disp = ROTATION_X_DISP(dx + w, dy + h, gr_draw->width);
    dy1_disp = ROTATION_Y_DISP(dx + w, dy + h, gr_draw->height);
    l_disp = std::min(dx0_disp, dx1_disp);
    r_disp = std::max(dx0_disp, dx1_disp);
    t_disp = std::min(dy0_disp, dy1_disp);
    b_disp = std::max(dy0_disp, dy1_disp);

    GGLSurface surface_rotated;
    if (gr_rotation != 0) {
        // Do not perform relatively expensive operation if not needed
        surface_rotated.version = sizeof(surface_rotated);
        // Skip the **(gr_rotation == 0)** || (gr_rotation == 180) check
        // because we are under a gr_rotation != 0 conditional compilation statement
        surface_rotated.width   = (gr_rotation == 180) ? surface->width  : surface->height;
        surface_rotated.height  = (gr_rotation == 180) ? surface->height : surface->width;
        surface_rotated.stride  = surface_rotated.width;
        surface_rotated.format  = surface->format;
        surface_rotated.data    = (GGLubyte*) malloc(surface_rotated.stride * surface_rotated.height * 4);
        surface_ROTATION_transform((gr_surface) &surface_rotated, (const gr_surface) surface, 4);

        gl->bindTexture(gl, &surface_rotated);
    } else {
        gl->bindTexture(gl, surface);
    }

    gl->texEnvi(gl, GGL_TEXTURE_ENV, GGL_TEXTURE_ENV_MODE, GGL_REPLACE);
    gl->texGeni(gl, GGL_S, GGL_TEXTURE_GEN_MODE, GGL_ONE_TO_ONE);
    gl->texGeni(gl, GGL_T, GGL_TEXTURE_GEN_MODE, GGL_ONE_TO_ONE);
    gl->enable(gl, GGL_TEXTURE_2D);
    gl->texCoord2i(gl, sx - l_disp, sy - t_disp);
    gl->recti(gl, l_disp, t_disp, r_disp, b_disp);
    gl->disable(gl, GGL_TEXTURE_2D);

    if (gr_rotation != 0)
        free(surface_rotated.data);

    if(surface->format == GGL_PIXEL_FORMAT_RGBX_8888)
        gl->enable(gl, GGL_BLEND);
}

unsigned int gr_get_width(gr_surface surface) {
    if (surface == NULL) {
        return 0;
    }
    return ((GGLSurface*) surface)->width;
}

unsigned int gr_get_height(gr_surface surface) {
    if (surface == NULL) {
        return 0;
    }
    return ((GGLSurface*) surface)->height;
}

void gr_flip() {
    gr_draw = gr_backend->flip(gr_backend);
    // On double buffered back ends, when we flip, we need to tell
    // pixel flinger to draw to the other buffer
    gr_mem_surface.data = (GGLubyte*)gr_draw->data;
    gr_context->colorBuffer(gr_context, &gr_mem_surface);
}

static void get_memory_surface(GGLSurface* ms) {
    ms->version = sizeof(*ms);
    ms->width = gr_draw->width;
    ms->height = gr_draw->height;
    ms->stride = gr_draw->row_bytes / gr_draw->pixel_bytes;
    ms->data = (GGLubyte*)gr_draw->data;
    ms->format = gr_draw->format;
}

int gr_init(void)
{
    gr_draw = NULL;

    char gr_rotation_string[PROPERTY_VALUE_MAX];
    char default_rotation[4];
    snprintf(default_rotation, 4, "%d", TW_ROTATION);
    property_get("persist.twrp.rotation", gr_rotation_string, default_rotation);
    gr_rotation = atoi(gr_rotation_string);
    if (!(gr_rotation == 90 || gr_rotation == 180 || gr_rotation == 270))
        gr_rotation = 0;

#ifdef MSM_BSP
    gr_backend = open_overlay();
    if (gr_backend) {
        gr_draw = gr_backend->init(gr_backend);
        if (!gr_draw) {
            gr_backend->exit(gr_backend);
        } else
            printf("Using overlay graphics.\n");
    }
#endif

#ifdef HAS_ADF
    if (!gr_backend || !gr_draw) {
        gr_backend = open_adf();
        if (gr_backend) {
            gr_draw = gr_backend->init(gr_backend);
            if (!gr_draw) {
                gr_backend->exit(gr_backend);
            } else
                printf("Using adf graphics.\n");
        }
    }
#else
#ifdef MSM_BSP
	printf("Skipping adf graphics because TW_TARGET_USES_QCOM_BSP := true\n");
#else
    printf("Skipping adf graphics -- not present in build tree\n");
#endif
#endif

#ifdef HAS_DRM
    if (!gr_backend || !gr_draw) {
        gr_backend = open_drm();
        gr_draw = gr_backend->init(gr_backend);
        if (gr_draw)
            printf("Using drm graphics.\n");
    }
#else
    printf("Skipping drm graphics -- not present in build tree\n");
#endif

    if (!gr_backend || !gr_draw) {
        gr_backend = open_fbdev();
        gr_draw = gr_backend->init(gr_backend);
        if (gr_draw == NULL) {
            return -1;
        } else
            printf("Using fbdev graphics.\n");
    }

    overscan_offset_x = gr_draw->width * overscan_percent / 100;
    overscan_offset_y = gr_draw->height * overscan_percent / 100;

    // Set up pixelflinger
    get_memory_surface(&gr_mem_surface);
    gglInit(&gr_context);
    GGLContext *gl = gr_context;
    gl->colorBuffer(gl, &gr_mem_surface);

    gl->activeTexture(gl, 0);
    gl->enable(gl, GGL_BLEND);
    gl->blendFunc(gl, GGL_SRC_ALPHA, GGL_ONE_MINUS_SRC_ALPHA);

    gr_flip();
    gr_flip();

    return 0;
}

void gr_exit(void)
{
    gr_backend->exit(gr_backend);
}

int gr_fb_width(void)
{
    return (gr_rotation == 0 || gr_rotation == 180) ?
            gr_draw->width  - 2 * overscan_offset_x :
            gr_draw->height - 2 * overscan_offset_y;
}

int gr_fb_height(void)
{
    return (gr_rotation == 0 || gr_rotation == 180) ?
            gr_draw->height - 2 * overscan_offset_y :
            gr_draw->width  - 2 * overscan_offset_x;
}

void gr_fb_blank(bool blank)
{
    gr_backend->blank(gr_backend, blank);
}

int gr_get_surface(gr_surface* surface)
{
    GGLSurface* ms = (GGLSurface*)malloc(sizeof(GGLSurface));
    if (!ms)    return -1;

    // Allocate the data
    get_memory_surface(ms);
    ms->data = (GGLubyte*)malloc(ms->stride * ms->height * gr_draw->pixel_bytes);

    // Now, copy the data
    memcpy(ms->data, gr_mem_surface.data, gr_draw->width * gr_draw->height * gr_draw->pixel_bytes / 8);

    *surface = (gr_surface*) ms;
    return 0;
}

int gr_free_surface(gr_surface surface)
{
    if (!surface)
        return -1;

    GGLSurface* ms = (GGLSurface*) surface;
    free(ms->data);
    free(ms);
    return 0;
}

void gr_write_frame_to_file(int fd)
{
    write(fd, gr_mem_surface.data, gr_draw->width * gr_draw->height * gr_draw->pixel_bytes / 8);
}
