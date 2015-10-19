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
#include <stdlib.h>
#include <png.h>
#include <pixelflinger/pixelflinger.h>
#include <linux/fb.h>

#include "minui.h"

struct fb_var_screeninfo vi;
extern GGLSurface gr_mem_surface;
extern GRSurface* gr_draw;

int gr_save_screenshot(const char *dest)
{
    uint32_t y, stride_bytes;
    volatile int res = -1;
    GGLContext *gl = NULL;
    GGLSurface surface;
    uint8_t * volatile img_data = NULL;
    uint8_t *ptr;
    FILE * volatile fp = NULL;
    png_structp png_ptr = NULL;
    png_infop info_ptr = NULL;

    fp = fopen(dest, "wb");
    if(!fp)
        goto exit;

    img_data = (uint8_t *)malloc(gr_mem_surface.stride * gr_mem_surface.height * gr_draw->pixel_bytes);
    if (!img_data) {
        printf("gr_save_screenshot failed to malloc img_data\n");
        goto exit;
    }
    surface.version = sizeof(surface);
    surface.width = gr_mem_surface.width;
    surface.height = gr_mem_surface.height;
    surface.stride = gr_mem_surface.stride;
    surface.data = img_data;
    surface.format = GGL_PIXEL_FORMAT_RGBA_8888;

    gglInit(&gl);
    gl->colorBuffer(gl, &surface);
    gl->activeTexture(gl, 0);

    if(gr_mem_surface.format == GGL_PIXEL_FORMAT_RGBX_8888)
        gl->disable(gl, GGL_BLEND);

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

    // To remove the alpha channel for PNG_COLOR_TYPE_RGB format,
    png_set_filler(png_ptr, 0, PNG_FILLER_AFTER);

    ptr = img_data;
    stride_bytes = surface.stride*4;
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
