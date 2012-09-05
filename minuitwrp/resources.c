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

#include <stdlib.h>
#include <unistd.h>

#include <fcntl.h>
#include <stdio.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>

#include <linux/fb.h>
#include <linux/kd.h>

#include <pixelflinger/pixelflinger.h>

#include <png.h>
#include <jpeglib.h>

#include "minui.h"

// libpng gives "undefined reference to 'pow'" errors, and I have no
// idea how to convince the build system to link with -lm.  We don't
// need this functionality (it's used for gamma adjustment) so provide
// a dummy implementation to satisfy the linker.
double pow(double x, double y) {
    return x;
}

int res_create_surface_png(const char* name, gr_surface* pSurface) {
    GGLSurface* surface = NULL;
    int result = 0;
    unsigned char header[8];
    png_structp png_ptr = NULL;
    png_infop info_ptr = NULL;

    FILE* fp = fopen(name, "rb");
    if (fp == NULL) {
        char resPath[256];

        snprintf(resPath, sizeof(resPath)-1, "/res/images/%s.png", name);
        resPath[sizeof(resPath)-1] = '\0';
        fp = fopen(resPath, "rb");
        if (fp == NULL)
        {
            result = -1;
            goto exit;
        }
    }

    size_t bytesRead = fread(header, 1, sizeof(header), fp);
    if (bytesRead != sizeof(header)) {
        result = -2;
        goto exit;
    }

    if (png_sig_cmp(header, 0, sizeof(header))) {
        result = -3;
        goto exit;
    }

    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) {
        result = -4;
        goto exit;
    }

    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        result = -5;
        goto exit;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        result = -6;
        goto exit;
    }

    png_set_packing(png_ptr);

    png_init_io(png_ptr, fp);
    png_set_sig_bytes(png_ptr, sizeof(header));
    png_read_info(png_ptr, info_ptr);

    size_t width = info_ptr->width;
    size_t height = info_ptr->height;
    size_t stride = 4 * width;
    size_t pixelSize = stride * height;

    int color_type = info_ptr->color_type;
    int bit_depth = info_ptr->bit_depth;
    int channels = info_ptr->channels;
    if (!(bit_depth == 8 &&
          ((channels == 3 && color_type == PNG_COLOR_TYPE_RGB) ||
           (channels == 4 && color_type == PNG_COLOR_TYPE_RGBA) ||
           (channels == 1 && color_type == PNG_COLOR_TYPE_PALETTE)))) {
        return -7;
        goto exit;
    }

    surface = malloc(sizeof(GGLSurface) + pixelSize);
    if (surface == NULL) {
        result = -8;
        goto exit;
    }
    unsigned char* pData = (unsigned char*) (surface + 1);
    surface->version = sizeof(GGLSurface);
    surface->width = width;
    surface->height = height;
    surface->stride = width; /* Yes, pixels, not bytes */
    surface->data = pData;
    surface->format = (channels == 3) ?
            GGL_PIXEL_FORMAT_RGBX_8888 : GGL_PIXEL_FORMAT_RGBA_8888;

    if (color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_palette_to_rgb(png_ptr);
    }

    int y;
    if (channels < 4) {
        for (y = 0; y < (int) height; ++y) {
            unsigned char* pRow = pData + y * stride;
            png_read_row(png_ptr, pRow, NULL);

            int x;
            for(x = width - 1; x >= 0; x--) {
                int sx = x * 3;
                int dx = x * 4;
                unsigned char r = pRow[sx];
                unsigned char g = pRow[sx + 1];
                unsigned char b = pRow[sx + 2];
                unsigned char a = 0xff;
                pRow[dx    ] = r; // r
                pRow[dx + 1] = g; // g
                pRow[dx + 2] = b; // b
                pRow[dx + 3] = a;
            }
        }
    } else {
        for (y = 0; y < (int) height; ++y) {
            unsigned char* pRow = pData + y * stride;
            png_read_row(png_ptr, pRow, NULL);
        }
    }

    *pSurface = (gr_surface) surface;

exit:
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

    if (fp != NULL) {
        fclose(fp);
    }
    if (result < 0) {
        if (surface) {
            free(surface);
        }
    }
    return result;
}

int res_create_surface_jpg(const char* name, gr_surface* pSurface) {
    GGLSurface* surface = NULL;
    int result = 0;
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;

    FILE* fp = fopen(name, "rb");
    if (fp == NULL) {
        char resPath[256];

        snprintf(resPath, sizeof(resPath)-1, "/res/images/%s", name);
        resPath[sizeof(resPath)-1] = '\0';
        fp = fopen(resPath, "rb");
        if (fp == NULL) {
            result = -1;
            goto exit;
        }
    }

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);

    /* Specify data source for decompression */
    jpeg_stdio_src(&cinfo, fp);

    /* Read file header, set default decompression parameters */
    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK)
        goto exit;

    /* Start decompressor */
    (void) jpeg_start_decompress(&cinfo);

    size_t width = cinfo.image_width;
    size_t height = cinfo.image_height;
    size_t stride = 4 * width;
    size_t pixelSize = stride * height;

    surface = malloc(sizeof(GGLSurface) + pixelSize);
    if (surface == NULL) {
        result = -8;
        goto exit;
    }

    unsigned char* pData = (unsigned char*) (surface + 1);
    surface->version = sizeof(GGLSurface);
    surface->width = width;
    surface->height = height;
    surface->stride = width; /* Yes, pixels, not bytes */
    surface->data = pData;
    surface->format = GGL_PIXEL_FORMAT_RGBX_8888;

    int y;
    for (y = 0; y < (int) height; ++y) {
        unsigned char* pRow = pData + y * stride;
        jpeg_read_scanlines(&cinfo, &pRow, 1);

        int x;
        for(x = width - 1; x >= 0; x--) {
            int sx = x * 3;
            int dx = x * 4;
            unsigned char r = pRow[sx];
            unsigned char g = pRow[sx + 1];
            unsigned char b = pRow[sx + 2];
            unsigned char a = 0xff;
            pRow[dx    ] = r; // r
            pRow[dx + 1] = g; // g
            pRow[dx + 2] = b; // b
            pRow[dx + 3] = a;
        }
    }
    *pSurface = (gr_surface) surface;

exit:
    if (fp != NULL)
    {
        if (surface)
        {
            (void) jpeg_finish_decompress(&cinfo);
            if (result < 0)
            {
                free(surface);
            }
        }
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);
    }
    return result;
}

int res_create_surface(const char* name, gr_surface* pSurface) {
    int ret;

    if (!name)      return -1;

    if (strlen(name) > 4 && strcmp(name + strlen(name) - 4, ".jpg") == 0)
		return res_create_surface_jpg(name,pSurface);

    ret = res_create_surface_png(name,pSurface);
    if (ret < 0)
        ret = res_create_surface_jpg(name,pSurface);

    return ret;
}

void res_free_surface(gr_surface surface) {
    GGLSurface* pSurface = (GGLSurface*) surface;
    if (pSurface) {
        free(pSurface);
    }
}
