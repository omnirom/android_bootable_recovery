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

#ifndef _MINUI_H_
#define _MINUI_H_

#include "../gui/placement.h"
#include <stdbool.h>

struct GRSurface {
    int width;
    int height;
    int row_bytes;
    int pixel_bytes;
    unsigned char* data;
    __u32 format;
};

typedef void* gr_surface;
typedef unsigned short gr_pixel;

#define FONT_TYPE_TWRP 0
#define FONT_TYPE_TTF  1

int gr_init(void);
void gr_exit(void);

int gr_fb_width(void);
int gr_fb_height(void);
gr_pixel *gr_fb_data(void);
void gr_flip(void);
void gr_fb_blank(bool blank);

void gr_color(unsigned char r, unsigned char g, unsigned char b, unsigned char a);
void gr_clip(int x, int y, int w, int h);
void gr_noclip();
void gr_fill(int x, int y, int w, int h);
void gr_line(int x0, int y0, int x1, int y1, int width);
gr_surface gr_render_circle(int radius, unsigned char r, unsigned char g, unsigned char b, unsigned char a);

int gr_textEx_scaleW(int x, int y, const char *s, void* pFont, int max_width, int placement, int scale);

int gr_getMaxFontHeight(void *font);

void *gr_ttf_loadFont(const char *filename, int size, int dpi);
void *gr_ttf_scaleFont(void *font, int max_width, int measured_width);
void gr_ttf_freeFont(void *font);
int gr_ttf_textExWH(void *context, int x, int y, const char *s, void *pFont, int max_width, int max_height);
int gr_ttf_measureEx(const char *s, void *font);
int gr_ttf_maxExW(const char *s, void *font, int max_width);
int gr_ttf_getMaxFontHeight(void *font);
void gr_ttf_dump_stats(void);

void gr_blit(gr_surface source, int sx, int sy, int w, int h, int dx, int dy);
unsigned int gr_get_width(gr_surface surface);
unsigned int gr_get_height(gr_surface surface);
int gr_get_surface(gr_surface* surface);
int gr_free_surface(gr_surface surface);

// Functions in graphics_utils.c
int gr_save_screenshot(const char *dest);

// input event structure, include <linux/input.h> for the definition.
// see http://www.mjmwired.net/kernel/Documentation/input/ for info.
struct input_event;

int ev_init(void);
void ev_exit(void);
int ev_get(struct input_event *ev, int timeout_ms);
int ev_has_mouse(void);

// Resources

// Returns 0 if no error, else negative.
int res_create_surface(const char* name, gr_surface* pSurface);
void res_free_surface(gr_surface surface);
int res_scale_surface(gr_surface source, gr_surface* destination, float scale_w, float scale_h);

int vibrate(int timeout_ms);

#endif
