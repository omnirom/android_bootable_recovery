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

#include <stdio.h>
#include <png.h>
#include <pixelflinger/pixelflinger.h>
#include <linux/fb.h>

#include "minui.h"

struct fb_var_screeninfo vi;
GGLSurface gr_mem_surface;

int gr_save_screenshot(const char *dest)
{
    uint32_t y, stride_bytes;
    int res = -1;
    GGLContext *gl = NULL;
    GGLSurface surface;
    uint8_t * volatile img_data = NULL;
    uint8_t *ptr;
    FILE *fp = NULL;
    png_structp png_ptr = NULL;
    png_infop info_ptr = NULL;

    fp = fopen(dest, "wb");
    if(!fp)
        goto exit;

    img_data = malloc(vi.xres * vi.yres * 3);
    surface.version = sizeof(surface);
    surface.width = gr_mem_surface.width;
    surface.height = gr_mem_surface.height;
    surface.stride = gr_mem_surface.width;
    surface.data = img_data;
    surface.format = GGL_PIXEL_FORMAT_RGB_888;

    gglInit(&gl);
    gl->colorBuffer(gl, &surface);
    gl->activeTexture(gl, 0);

    gl->bindTexture(gl, &gr_mem_surface);
    gl->texEnvi(gl, GGL_TEXTURE_ENV, GGL_TEXTURE_ENV_MODE, GGL_REPLACE);
    gl->texGeni(gl, GGL_S, GGL_TEXTURE_GEN_MODE, GGL_ONE_TO_ONE);
    gl->texGeni(gl, GGL_T, GGL_TEXTURE_GEN_MODE, GGL_ONE_TO_ONE);
    gl->enable(gl, GGL_TEXTURE_2D);
    gl->texCoord2i(gl, 0, 0);
    gl->recti(gl, 0, 0, gr_mem_surface.width, gr_mem_surface.height);

    gglUninit(gl);
    gl = NULL;

    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr)
        goto exit;

    info_ptr = png_create_info_struct(png_ptr);
    if (info_ptr == NULL)
        goto exit;

    if (setjmp(png_jmpbuf(png_ptr)))
        goto exit;

    png_init_io(png_ptr, fp);
    png_set_IHDR(png_ptr, info_ptr, surface.width, surface.height,
         8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
         PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
    png_write_info(png_ptr, info_ptr);

    ptr = img_data;
    stride_bytes = surface.width*3;
    for(y = 0; y < surface.height; ++y)
    {
        png_write_row(png_ptr, ptr);
        ptr += stride_bytes;
    }

    png_write_end(png_ptr, NULL);

    res = 0;
exit:
    if(info_ptr)
        png_free_data(png_ptr, info_ptr, PNG_FREE_ALL, -1);
    if(png_ptr)
        png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
    if(gl)
        gglUninit(gl);
    if(img_data)
        free(img_data);
    if(fp)
        fclose(fp);
    return res;
}

// Scaling functions
int create_memory_surface(GGLSurface* ms, int w, int h, int format) {
    ms->version = sizeof(*ms);
    ms->width = w;
    ms->height = h;
    ms->stride = w;
    ms->format = format;
    size_t size = w * h * 4;
    ms->data = malloc(size);
    if (ms->data == NULL) {
        printf("create_memory_surface failed to malloc\n");
        return -1;
    }
    return 0;
}

int gr_scale_surface(gr_surface source, gr_surface* destination, float scale_w, float scale_h)
{
    GGLContext *sc_context = 0;
    gglInit(&sc_context);
    GGLContext *gl = sc_context;
    GGLSurface* sc_mem_surface = malloc(sizeof(GGLSurface));
    *destination = NULL;
    if (!sc_mem_surface) {
        printf("scale_surface failed to malloc sc_mem_surface\n");
        return -1;
    }
    GGLSurface *surface = (GGLSurface*)source;
    int w = gr_get_width(source), h = gr_get_height(source);
    int sx = 0, sy = 0, dx = 0, dy = 0;
    float dw = (float)w * scale_w;
    float dh = (float)h * scale_h;

    // Create a new surface that is the appropriate size
    if (create_memory_surface(sc_mem_surface, (int)dw, (int)dh, surface->format)) {
        free(sc_mem_surface);
        return -1;
    }

    // Finish initializing the context
    gl->colorBuffer(gl, sc_mem_surface);
    gl->activeTexture(gl, 0);

    // Enable or disable blending based on source surface format
    if (surface->format == GGL_PIXEL_FORMAT_RGBX_8888) {
        gl->disable(gl, GGL_BLEND);
    } else {
        gl->enable(gl, GGL_BLEND);
        gl->blendFunc(gl, GGL_ONE, GGL_ZERO);
    }

    // Bind our source surface to the context
    gl->bindTexture(gl, surface);

    // Deal with the scaling
    gl->texParameteri(gl, GGL_TEXTURE_2D, GGL_TEXTURE_MIN_FILTER, GGL_LINEAR);
    gl->texParameteri(gl, GGL_TEXTURE_2D, GGL_TEXTURE_MAG_FILTER, GGL_LINEAR);
    gl->texParameteri(gl, GGL_TEXTURE_2D, GGL_TEXTURE_WRAP_S, GGL_CLAMP);
    gl->texParameteri(gl, GGL_TEXTURE_2D, GGL_TEXTURE_WRAP_T, GGL_CLAMP);
    gl->texEnvi(gl, GGL_TEXTURE_ENV, GGL_TEXTURE_ENV_MODE, GGL_REPLACE);
    gl->texGeni(gl, GGL_S, GGL_TEXTURE_GEN_MODE, GGL_AUTOMATIC);
    gl->texGeni(gl, GGL_T, GGL_TEXTURE_GEN_MODE, GGL_AUTOMATIC);
    gl->enable(gl, GGL_TEXTURE_2D);

    int32_t grad[8];
    memset(grad, 0, sizeof(grad));
    // s, dsdx, dsdy, scale, t, dtdx, dtdy, tscale   <- this is wrong!
    // This api uses block floating-point for S and T texture coordinates.
    // All values are given in 16.16, scaled by 'scale'. In other words,
    // set scale to 0, for 16.16 values.

    // s, dsdx, dsdy, t, dtdx, dtdy, sscale, tscale
    float dsdx = (float)w / dw;
    float dtdy = (float)h / dh;
    grad[0] = ((float)sx - (dsdx * dx)) * 65536;
    grad[1] = dsdx * 65536;
    grad[3] = ((float)sy - (dtdy * dy)) * 65536;
    grad[5] = dtdy * 65536;
//    printf("blit: w=%d h=%d dx=%d dy=%d dw=%f dh=%f dsdx=%f dtdy=%f s0=%x dsdx=%x t0=%x dtdy=%x\n",
//                    w,   h,    dx,   dy,   dw,   dh,   dsdx,   dtdy, grad[0], grad[1], grad[3], grad[5]);
    gl->texCoordGradScale8xv(gl, 0 /*tmu*/, grad);

    // draw / scale the source surface to our target context
    gl->recti(gl, dx, dy, dx + dw, dy + dh);
    // put the scaled surface in our destination
    *destination = (gr_surface*) sc_mem_surface;
    return 0;
}
