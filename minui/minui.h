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

void gr_color(unsigned char r, unsigned char g, unsigned char b, unsigned char a);
void gr_fill(int x, int y, int w, int h);
int gr_text(int x, int y, const char *s);
int gr_measure(const char *s);

void gr_blit(gr_surface source, int sx, int sy, int w, int h, int dx, int dy);
unsigned int gr_get_width(gr_surface surface);
unsigned int gr_get_height(gr_surface surface);

// input event structure, include <linux/input.h> for the definition.
// see http://www.mjmwired.net/kernel/Documentation/input/ for info.
struct input_event;

// Dream-specific key codes
#define KEY_DREAM_HOME        102  // = KEY_HOME
#define KEY_DREAM_RED         107  // = KEY_END
#define KEY_DREAM_VOLUMEDOWN  114  // = KEY_VOLUMEDOWN
#define KEY_DREAM_VOLUMEUP    115  // = KEY_VOLUMEUP
#define KEY_DREAM_SYM         127  // = KEY_COMPOSE
#define KEY_DREAM_MENU        139  // = KEY_MENU
#define KEY_DREAM_BACK        158  // = KEY_BACK
#define KEY_DREAM_FOCUS       211  // = KEY_HP (light touch on camera)
#define KEY_DREAM_CAMERA      212  // = KEY_CAMERA
#define KEY_DREAM_AT          215  // = KEY_EMAIL
#define KEY_DREAM_GREEN       231
#define KEY_DREAM_FATTOUCH    258  // = BTN_2 ???
#define KEY_DREAM_BALL        272  // = BTN_MOUSE
#define KEY_DREAM_TOUCH       330  // = BTN_TOUCH

int ev_init(void);
void ev_exit(void);
int ev_get(struct input_event *ev, unsigned dont_wait);

// Resources

// Returns 0 if no error, else negative.
int res_create_surface(const char* name, gr_surface* pSurface);
void res_free_surface(gr_surface surface);

#endif
