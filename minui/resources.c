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

#include "minui.h"

// File signature for BMP files.
// The letters 'BM' as a little-endian unsigned short.

#define BMP_SIGNATURE 0x4d42

typedef struct {
    // constant, value should equal BMP_SIGNATURE
    unsigned short  bfType;
    // size of the file in bytes.
    unsigned long   bfSize;
    // must always be set to zero.
    unsigned short  bfReserved1;
    // must always be set to zero.
    unsigned short  bfReserved2;
    // offset from the beginning of the file to the bitmap data.
    unsigned long   bfOffBits;

    // The BITMAPINFOHEADER:
    // size of the BITMAPINFOHEADER structure, in bytes.
    unsigned long   biSize;
    // width of the image, in pixels.
    unsigned long   biWidth;
    // height of the image, in pixels.
    unsigned long   biHeight;
    // number of planes of the target device, must be set to 1.
    unsigned short  biPlanes;
    // number of bits per pixel.
    unsigned short  biBitCount;
    // type of compression, zero means no compression.
    unsigned long   biCompression;
    // size of the image data, in bytes. If there is no compression,
    // it is valid to set this member to zero.
    unsigned long   biSizeImage;
    // horizontal pixels per meter on the designated targer device,
    // usually set to zero.
    unsigned long   biXPelsPerMeter;
    // vertical pixels per meter on the designated targer device,
    // usually set to zero.
    unsigned long   biYPelsPerMeter;
    // number of colors used in the bitmap, if set to zero the
    // number of colors is calculated using the biBitCount member.
    unsigned long   biClrUsed;
    // number of color that are 'important' for the bitmap,
    // if set to zero, all colors are important.
    unsigned long   biClrImportant;
} __attribute__((packed)) BitMapFileHeader;

int res_create_surface(const char* name, gr_surface* pSurface) {
    char resPath[256];
    BitMapFileHeader header;
    GGLSurface* surface = NULL;
    int result = 0;
    
    snprintf(resPath, sizeof(resPath)-1, "/res/images/%s.bmp", name);
    resPath[sizeof(resPath)-1] = '\0';
    int fd = open(resPath, O_RDONLY);
    if (fd == -1) {
        result = -1;
        goto exit;
    }
    size_t bytesRead = read(fd, &header, sizeof(header));
    if (bytesRead != sizeof(header)) {
        result = -2;
        goto exit;
    }
    if (header.bfType != BMP_SIGNATURE) {
        result = -3; // Not a legal header
        goto exit;
    }
    if (header.biPlanes != 1) {
        result = -4;
        goto exit;
    }
    if (!(header.biBitCount == 24 || header.biBitCount == 32)) {
        result = -5;
        goto exit;
    }
    if (header.biCompression != 0) {
        result = -6;
        goto exit;
    }
    size_t width = header.biWidth;
    size_t height = header.biHeight;
    size_t stride = 4 * width;
    size_t pixelSize = stride * height;
    
    surface = malloc(sizeof(GGLSurface) + pixelSize);
    if (surface == NULL) {
        result = -7;
        goto exit;
    }
    unsigned char* pData = (unsigned char*) (surface + 1);
    surface->version = sizeof(GGLSurface);
    surface->width = width;
    surface->height = height;
    surface->stride = width; /* Yes, pixels, not bytes */
    surface->data = pData;
    surface->format = (header.biBitCount == 24) ?
            GGL_PIXEL_FORMAT_RGBX_8888 : GGL_PIXEL_FORMAT_RGBA_8888;

    // Source pixel bytes are stored B G R {A}
    
    lseek(fd, header.bfOffBits, SEEK_SET);
    size_t y;
    if (header.biBitCount == 24) { // RGB
        size_t inputStride = (((3 * width + 3) >> 2) << 2);
        for (y = 0; y < height; y++) {
            unsigned char* pRow = pData + (height - (y + 1)) * stride;
            bytesRead = read(fd,  pRow, inputStride);
            if (bytesRead != inputStride) {
                result = -8;
                goto exit;
            }
            int x;
            for(x = width - 1; x >= 0; x--) {
                int sx = x * 3;
                int dx = x * 4;
                unsigned char b = pRow[sx];
                unsigned char g = pRow[sx + 1];
                unsigned char r = pRow[sx + 2];
                unsigned char a = 0xff;
                pRow[dx    ] = r; // r
                pRow[dx + 1] = g; // g
                pRow[dx + 2] = b; // b;
                pRow[dx + 3] = a;
            }
        }
    } else { // RGBA
        for (y = 0; y < height; y++) {
            unsigned char* pRow = pData + (height - (y + 1)) * stride;
            bytesRead = read(fd,  pRow, stride);
            if (bytesRead != stride) {
                result = -9;
                goto exit;
            }
            size_t x;
            for(x = 0; x < width; x++) {
                size_t xx = x * 4;
                unsigned char b = pRow[xx];
                unsigned char g = pRow[xx + 1];
                unsigned char r = pRow[xx + 2];
                unsigned char a = pRow[xx + 3];
                pRow[xx    ] = r;
                pRow[xx + 1] = g;
                pRow[xx + 2] = b;
                pRow[xx + 3] = a;
            }
        }
    }
    *pSurface = (gr_surface) surface;

exit:
    if (fd >= 0) {
        close(fd);
    }
    if (result < 0) {
        if (surface) {
            free(surface);
        }
    }
    return result;
}

void res_free_surface(gr_surface surface) {
    GGLSurface* pSurface = (GGLSurface*) surface;
    if (pSurface) {
        free(pSurface);
    }
}
