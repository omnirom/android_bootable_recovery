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

#include "graphics.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <memory>

#include "font_10x18.h"
#include "graphics_adf.h"
#include "graphics_drm.h"
#include "graphics_fbdev.h"
#include "minui/minui.h"

static GRFont* gr_font = NULL;
static MinuiBackend* gr_backend = nullptr;

static int overscan_percent = OVERSCAN_PERCENT;
static int overscan_offset_x = 0;
static int overscan_offset_y = 0;

static unsigned char gr_current_r = 255;
static unsigned char gr_current_g = 255;
static unsigned char gr_current_b = 255;
static unsigned char gr_current_a = 255;

static GRSurface* gr_draw = NULL;

static bool outside(int x, int y)
{
    return x < 0 || x >= gr_draw->width || y < 0 || y >= gr_draw->height;
}

const GRFont* gr_sys_font()
{
    return gr_font;
}

int gr_measure(const GRFont* font, const char *s)
{
    return font->char_width * strlen(s);
}

void gr_font_size(const GRFont* font, int *x, int *y)
{
    *x = font->char_width;
    *y = font->char_height;
}

static void text_blend(unsigned char* src_p, int src_row_bytes,
                       unsigned char* dst_p, int dst_row_bytes,
                       int width, int height)
{
    for (int j = 0; j < height; ++j) {
        unsigned char* sx = src_p;
        unsigned char* px = dst_p;
        for (int i = 0; i < width; ++i) {
            unsigned char a = *sx++;
            if (gr_current_a < 255) a = ((int)a * gr_current_a) / 255;
            if (a == 255) {
                *px++ = gr_current_r;
                *px++ = gr_current_g;
                *px++ = gr_current_b;
                px++;
            } else if (a > 0) {
                *px = (*px * (255-a) + gr_current_r * a) / 255;
                ++px;
                *px = (*px * (255-a) + gr_current_g * a) / 255;
                ++px;
                *px = (*px * (255-a) + gr_current_b * a) / 255;
                ++px;
                ++px;
            } else {
                px += 4;
            }
        }
        src_p += src_row_bytes;
        dst_p += dst_row_bytes;
    }
}

void gr_text(const GRFont* font, int x, int y, const char *s, bool bold)
{
    if (!font->texture || gr_current_a == 0) return;

    bold = bold && (font->texture->height != font->char_height);

    x += overscan_offset_x;
    y += overscan_offset_y;

    unsigned char ch;
    while ((ch = *s++)) {
        if (outside(x, y) || outside(x+font->char_width-1, y+font->char_height-1)) break;

        if (ch < ' ' || ch > '~') {
            ch = '?';
        }

        unsigned char* src_p = font->texture->data + ((ch - ' ') * font->char_width) +
                               (bold ? font->char_height * font->texture->row_bytes : 0);
        unsigned char* dst_p = gr_draw->data + y*gr_draw->row_bytes + x*gr_draw->pixel_bytes;

        text_blend(src_p, font->texture->row_bytes,
                   dst_p, gr_draw->row_bytes,
                   font->char_width, font->char_height);

        x += font->char_width;
    }
}

void gr_texticon(int x, int y, GRSurface* icon) {
    if (icon == NULL) return;

    if (icon->pixel_bytes != 1) {
        printf("gr_texticon: source has wrong format\n");
        return;
    }

    x += overscan_offset_x;
    y += overscan_offset_y;

    if (outside(x, y) || outside(x+icon->width-1, y+icon->height-1)) return;

    unsigned char* src_p = icon->data;
    unsigned char* dst_p = gr_draw->data + y*gr_draw->row_bytes + x*gr_draw->pixel_bytes;

    text_blend(src_p, icon->row_bytes,
               dst_p, gr_draw->row_bytes,
               icon->width, icon->height);
}

void gr_color(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
#if defined(RECOVERY_ABGR) || defined(RECOVERY_BGRA)
    gr_current_r = b;
    gr_current_g = g;
    gr_current_b = r;
    gr_current_a = a;
#else
    gr_current_r = r;
    gr_current_g = g;
    gr_current_b = b;
    gr_current_a = a;
#endif
}

void gr_clear()
{
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

void gr_fill(int x1, int y1, int x2, int y2)
{
    x1 += overscan_offset_x;
    y1 += overscan_offset_y;

    x2 += overscan_offset_x;
    y2 += overscan_offset_y;

    if (outside(x1, y1) || outside(x2-1, y2-1)) return;

    unsigned char* p = gr_draw->data + y1 * gr_draw->row_bytes + x1 * gr_draw->pixel_bytes;
    if (gr_current_a == 255) {
        int x, y;
        for (y = y1; y < y2; ++y) {
            unsigned char* px = p;
            for (x = x1; x < x2; ++x) {
                *px++ = gr_current_r;
                *px++ = gr_current_g;
                *px++ = gr_current_b;
                px++;
            }
            p += gr_draw->row_bytes;
        }
    } else if (gr_current_a > 0) {
        int x, y;
        for (y = y1; y < y2; ++y) {
            unsigned char* px = p;
            for (x = x1; x < x2; ++x) {
                *px = (*px * (255-gr_current_a) + gr_current_r * gr_current_a) / 255;
                ++px;
                *px = (*px * (255-gr_current_a) + gr_current_g * gr_current_a) / 255;
                ++px;
                *px = (*px * (255-gr_current_a) + gr_current_b * gr_current_a) / 255;
                ++px;
                ++px;
            }
            p += gr_draw->row_bytes;
        }
    }
}

void gr_blit(GRSurface* source, int sx, int sy, int w, int h, int dx, int dy) {
    if (source == NULL) return;

    if (gr_draw->pixel_bytes != source->pixel_bytes) {
        printf("gr_blit: source has wrong format\n");
        return;
    }

    dx += overscan_offset_x;
    dy += overscan_offset_y;

    if (outside(dx, dy) || outside(dx+w-1, dy+h-1)) return;

    unsigned char* src_p = source->data + sy*source->row_bytes + sx*source->pixel_bytes;
    unsigned char* dst_p = gr_draw->data + dy*gr_draw->row_bytes + dx*gr_draw->pixel_bytes;

    int i;
    for (i = 0; i < h; ++i) {
        memcpy(dst_p, src_p, w * source->pixel_bytes);
        src_p += source->row_bytes;
        dst_p += gr_draw->row_bytes;
    }
}

unsigned int gr_get_width(GRSurface* surface) {
    if (surface == NULL) {
        return 0;
    }
    return surface->width;
}

unsigned int gr_get_height(GRSurface* surface) {
    if (surface == NULL) {
        return 0;
    }
    return surface->height;
}

int gr_init_font(const char* name, GRFont** dest) {
    GRFont* font = static_cast<GRFont*>(calloc(1, sizeof(*gr_font)));
    if (font == nullptr) {
        return -1;
    }

    int res = res_create_alpha_surface(name, &(font->texture));
    if (res < 0) {
        free(font);
        return res;
    }

    // The font image should be a 96x2 array of character images.  The
    // columns are the printable ASCII characters 0x20 - 0x7f.  The
    // top row is regular text; the bottom row is bold.
    font->char_width = font->texture->width / 96;
    font->char_height = font->texture->height / 2;

    *dest = font;

    return 0;
}

static void gr_init_font(void)
{
    int res = gr_init_font("font", &gr_font);
    if (res == 0) {
        return;
    }

    printf("failed to read font: res=%d\n", res);


    // fall back to the compiled-in font.
    gr_font = static_cast<GRFont*>(calloc(1, sizeof(*gr_font)));
    gr_font->texture = static_cast<GRSurface*>(malloc(sizeof(*gr_font->texture)));
    gr_font->texture->width = font.width;
    gr_font->texture->height = font.height;
    gr_font->texture->row_bytes = font.width;
    gr_font->texture->pixel_bytes = 1;

    unsigned char* bits = static_cast<unsigned char*>(malloc(font.width * font.height));
    gr_font->texture->data = bits;

    unsigned char data;
    unsigned char* in = font.rundata;
    while((data = *in++)) {
        memset(bits, (data & 0x80) ? 255 : 0, data & 0x7f);
        bits += (data & 0x7f);
    }

    gr_font->char_width = font.char_width;
    gr_font->char_height = font.char_height;
}

void gr_flip() {
  gr_draw = gr_backend->Flip();
}

int gr_init() {
  gr_init_font();

  auto backend = std::unique_ptr<MinuiBackend>{ std::make_unique<MinuiBackendAdf>() };
  gr_draw = backend->Init();

  if (!gr_draw) {
    backend = std::make_unique<MinuiBackendDrm>();
    gr_draw = backend->Init();
  }

  if (!gr_draw) {
    backend = std::make_unique<MinuiBackendFbdev>();
    gr_draw = backend->Init();
  }

  if (!gr_draw) {
    return -1;
  }

  gr_backend = backend.release();

  overscan_offset_x = gr_draw->width * overscan_percent / 100;
  overscan_offset_y = gr_draw->height * overscan_percent / 100;

  gr_flip();
  gr_flip();

  return 0;
}

void gr_exit() {
  delete gr_backend;
}

int gr_fb_width() {
  return gr_draw->width - 2 * overscan_offset_x;
}

int gr_fb_height() {
  return gr_draw->height - 2 * overscan_offset_y;
}

void gr_fb_blank(bool blank) {
  gr_backend->Blank(blank);
}
