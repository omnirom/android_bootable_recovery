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
#include <string.h>
#include <unistd.h>

#include <fcntl.h>
#include <stdio.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>

#include <linux/fb.h>
#include <linux/kd.h>

#include <png.h>

#include <pixelflinger/pixelflinger.h>
#ifdef TW_INCLUDE_JPEG
extern "C" {
#include "jpeglib.h"
}
#endif
#include "minui.h"

#define SURFACE_DATA_ALIGNMENT 8

static GGLSurface* malloc_surface(size_t data_size) {
    size_t size = sizeof(GGLSurface) + data_size + SURFACE_DATA_ALIGNMENT;
    unsigned char* temp = reinterpret_cast<unsigned char*>(malloc(size));
    if (temp == NULL) return NULL;
    GGLSurface* surface = reinterpret_cast<GGLSurface*>(temp);
    surface->data = temp + sizeof(GGLSurface) +
        (SURFACE_DATA_ALIGNMENT - (sizeof(GGLSurface) % SURFACE_DATA_ALIGNMENT));
    return surface;
}

static int open_png(const char* name, png_structp* png_ptr, png_infop* info_ptr,
                    png_uint_32* width, png_uint_32* height, png_byte* channels, FILE** fpp) {
    char resPath[256];
    unsigned char header[8];
    int result = 0;
    int color_type, bit_depth;
    size_t bytesRead;

    snprintf(resPath, sizeof(resPath)-1, TWRES "images/%s.png", name);
    resPath[sizeof(resPath)-1] = '\0';
    FILE* fp = fopen(resPath, "rb");
    if (fp == NULL) {
        fp = fopen(name, "rb");
        if (fp == NULL) {
            result = -1;
            goto exit;
        }
    }

    bytesRead = fread(header, 1, sizeof(header), fp);
    if (bytesRead != sizeof(header)) {
        result = -2;
        goto exit;
    }

    if (png_sig_cmp(header, 0, sizeof(header))) {
        result = -3;
        goto exit;
    }

    *png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!*png_ptr) {
        result = -4;
        goto exit;
    }

    *info_ptr = png_create_info_struct(*png_ptr);
    if (!*info_ptr) {
        result = -5;
        goto exit;
    }

    if (setjmp(png_jmpbuf(*png_ptr))) {
        result = -6;
        goto exit;
    }

    png_init_io(*png_ptr, fp);
    png_set_sig_bytes(*png_ptr, sizeof(header));
    png_read_info(*png_ptr, *info_ptr);

    png_get_IHDR(*png_ptr, *info_ptr, width, height, &bit_depth,
            &color_type, NULL, NULL, NULL);

    *channels = png_get_channels(*png_ptr, *info_ptr);

    if (bit_depth == 8 && *channels == 3 && color_type == PNG_COLOR_TYPE_RGB) {
        // 8-bit RGB images: great, nothing to do.
    } else if (bit_depth <= 8 && *channels == 1 && color_type == PNG_COLOR_TYPE_GRAY) {
        // 1-, 2-, 4-, or 8-bit gray images: expand to 8-bit gray.
        png_set_expand_gray_1_2_4_to_8(*png_ptr);
    } else if (bit_depth <= 8 && *channels == 1 && color_type == PNG_COLOR_TYPE_PALETTE) {
        // paletted images: expand to 8-bit RGB.  Note that we DON'T
        // currently expand the tRNS chunk (if any) to an alpha
        // channel, because minui doesn't support alpha channels in
        // general.
        png_set_palette_to_rgb(*png_ptr);
        *channels = 3;
    } else if (color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_palette_to_rgb(*png_ptr);
    }

    *fpp = fp;
    return result;

  exit:
    if (result < 0) {
        png_destroy_read_struct(png_ptr, info_ptr, NULL);
    }
    if (fp != NULL) {
        fclose(fp);
    }

    return result;
}

// "display" surfaces are transformed into the framebuffer's required
// pixel format (currently only RGBX is supported) at load time, so
// gr_blit() can be nothing more than a memcpy() for each row.  The
// next two functions are the only ones that know anything about the
// framebuffer pixel format; they need to be modified if the
// framebuffer format changes (but nothing else should).

// Allocate and return a GRSurface* sufficient for storing an image of
// the indicated size in the framebuffer pixel format.
static GGLSurface* init_display_surface(png_uint_32 width, png_uint_32 height) {
    GGLSurface* surface = malloc_surface(width * height * 4);
    if (surface == NULL) return NULL;

    surface->version = sizeof(GGLSurface);
    surface->width = width;
    surface->height = height;
    surface->stride = width;

    return surface;
}

// Copy 'input_row' to 'output_row', transforming it to the
// framebuffer pixel format.  The input format depends on the value of
// 'channels':
//
//   1 - input is 8-bit grayscale
//   3 - input is 24-bit RGB
//   4 - input is 32-bit RGBA/RGBX
//
// 'width' is the number of pixels in the row.
static void transform_rgb_to_draw(unsigned char* input_row,
                                  unsigned char* output_row,
                                  int channels, int width) {
    int x;
    unsigned char* ip = input_row;
    unsigned char* op = output_row;

    switch (channels) {
        case 1:
            // expand gray level to RGBX
            for (x = 0; x < width; ++x) {
                *op++ = *ip;
                *op++ = *ip;
                *op++ = *ip;
                *op++ = 0xff;
                ip++;
            }
            break;

        case 3:
            // expand RGBA to RGBX
            for (x = 0; x < width; ++x) {
                *op++ = *ip++;
                *op++ = *ip++;
                *op++ = *ip++;
                *op++ = 0xff;
            }
            break;

        case 4:
            // copy RGBA to RGBX
            memcpy(output_row, input_row, width*4);
            break;
    }
}

int res_create_surface_png(const char* name, gr_surface* pSurface) {
    GGLSurface* surface = NULL;
    int result = 0;
    png_structp png_ptr = NULL;
    png_infop info_ptr = NULL;
    png_uint_32 width, height;
    png_byte channels;
    FILE* fp;
    unsigned char* p_row;
    unsigned int y;

    *pSurface = NULL;

    result = open_png(name, &png_ptr, &info_ptr, &width, &height, &channels, &fp);
    if (result < 0) return result;

    surface = init_display_surface(width, height);
    if (surface == NULL) {
        result = -8;
        goto exit;
    }

#if defined(RECOVERY_ABGR) || defined(RECOVERY_BGRA)
    png_set_bgr(png_ptr);
#endif

    p_row = reinterpret_cast<unsigned char*>(malloc(width * 4));
    if (p_row == NULL) {
        result = -9;
        goto exit;
    }
    for (y = 0; y < height; ++y) {
        png_read_row(png_ptr, p_row, NULL);
        transform_rgb_to_draw(p_row, surface->data + y * width * 4, channels, width);
    }
    free(p_row);

    if (channels == 3)
        surface->format = GGL_PIXEL_FORMAT_RGBX_8888;
    else
        surface->format = GGL_PIXEL_FORMAT_RGBA_8888;

    *pSurface = (gr_surface) surface;

  exit:
    fclose(fp);
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    if (result < 0 && surface != NULL) free(surface);
    return result;
}

#ifdef TW_INCLUDE_JPEG
int res_create_surface_jpg(const char* name, gr_surface* pSurface) {
    GGLSurface* surface = NULL;
    int result = 0, y;
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    unsigned char* pData;
    size_t width, height, stride, pixelSize;

    FILE* fp = fopen(name, "rb");
    if (fp == NULL) {
        char resPath[256];

        snprintf(resPath, sizeof(resPath)-1, TWRES "images/%s", name);
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

    width = cinfo.image_width;
    height = cinfo.image_height;
    stride = 4 * width;
    pixelSize = stride * height;

    surface = reinterpret_cast<GGLSurface*>(malloc(sizeof(GGLSurface) + pixelSize));
    //p_row = reinterpret_cast<unsigned char*>(malloc(width * 4));
    if (surface == NULL) {
        result = -8;
        goto exit;
    }

    pData = (unsigned char*) (surface + 1);
    surface->version = sizeof(GGLSurface);
    surface->width = width;
    surface->height = height;
    surface->stride = width; /* Yes, pixels, not bytes */
    surface->data = pData;
    surface->format = GGL_PIXEL_FORMAT_RGBX_8888;

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
#if defined(RECOVERY_ABGR) || defined(RECOVERY_BGRA)
            pRow[dx    ] = b; // r
            pRow[dx + 1] = g; // g
            pRow[dx + 2] = r; // b
            pRow[dx + 3] = a;
#else
            pRow[dx    ] = r; // r
            pRow[dx + 1] = g; // g
            pRow[dx + 2] = b; // b
            pRow[dx + 3] = a;
#endif
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
#endif

int res_create_surface(const char* name, gr_surface* pSurface) {
    int ret;

    if (!name)      return -1;

#ifdef TW_INCLUDE_JPEG
    if (strlen(name) > 4 && strcmp(name + strlen(name) - 4, ".jpg") == 0)
        return res_create_surface_jpg(name,pSurface);
#endif

    ret = res_create_surface_png(name,pSurface);
#ifdef TW_INCLUDE_JPEG
    if (ret < 0)
        ret = res_create_surface_jpg(name,pSurface);
#endif

    return ret;
}

void res_free_surface(gr_surface surface) {
    GGLSurface* pSurface = (GGLSurface*) surface;
    if (pSurface) {
        free(pSurface);
    }
}

// Scale image function
int res_scale_surface(gr_surface source, gr_surface* destination, float scale_w, float scale_h) {
    GGLContext *gl = NULL;
    GGLSurface* sc_mem_surface = NULL;
    *destination = NULL;
    GGLSurface *surface = (GGLSurface*)source;
    int w = gr_get_width(source), h = gr_get_height(source);
    int sx = 0, sy = 0, dx = 0, dy = 0;
    float dw = (float)w * scale_w;
    float dh = (float)h * scale_h;

    // Create a new surface that is the appropriate size
    sc_mem_surface = init_display_surface((int)dw, (int)dh);
    if (!sc_mem_surface) {
        printf("gr_scale_surface failed to init_display_surface\n");
        return -1;
    }
    sc_mem_surface->format = surface->format;

    // Initialize the context
    gglInit(&gl);
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
    gglUninit(gl);
    gl = NULL;
    // put the scaled surface in our destination
    *destination = (gr_surface*) sc_mem_surface;
    // free memory used in the source
    res_free_surface(source);
    source = NULL;
    return 0;
}
