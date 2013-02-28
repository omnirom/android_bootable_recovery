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

typedef void* gr_surface;
typedef unsigned short gr_pixel;

int gr_init(void);
void gr_exit(void);

int gr_fb_width(void);
int gr_fb_height(void);
gr_pixel *gr_fb_data(void);
void gr_flip(void);
int gr_fb_blank(int blank);

void gr_color(unsigned char r, unsigned char g, unsigned char b, unsigned char a);
void gr_fill(int x, int y, int w, int h);

int gr_textEx(int x, int y, const char *s, void* font);
int gr_textExW(int x, int y, const char *s, void* font, int max_width);
int gr_textExWH(int x, int y, const char *s, void* pFont, int max_width, int max_height);
int twgr_text(int x, int y, const char *s);
static inline int gr_text(int x, int y, const char *s)     { return gr_textEx(x, y, s, NULL); }
int gr_measureEx(const char *s, void* font);
static inline int gr_measure(const char *s)                { return gr_measureEx(s, NULL); }

int gr_getFontDetails(void* font, unsigned* cheight, unsigned* maxwidth);
static inline void gr_font_size(int *x, int *y)            { gr_getFontDetails(NULL, (unsigned*) y, (unsigned*) x); }

void* gr_loadFont(const char* fontName);

void gr_blit(gr_surface source, int sx, int sy, int w, int h, int dx, int dy);
unsigned int gr_get_width(gr_surface surface);
unsigned int gr_get_height(gr_surface surface);
int gr_get_surface(gr_surface* surface);
int gr_free_surface(gr_surface surface);

// input event structure, include <linux/input.h> for the definition.
// see http://www.mjmwired.net/kernel/Documentation/input/ for info.
struct input_event;

int ev_init(void);
void ev_exit(void);
int ev_get(struct input_event *ev, unsigned dont_wait);

// Resources

// Returns 0 if no error, else negative.
int res_create_surface(const char* name, gr_surface* pSurface);
void res_free_surface(gr_surface surface);

// Needed for AOSP:
int ev_wait(int timeout);
void ev_dispatch(void);
int ev_get_input(int fd, short revents, struct input_event *ev);

#endif
