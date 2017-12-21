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

#include <sys/types.h>

#include <functional>
#include <string>

//
// Graphics.
//

struct GRSurface {
  int width;
  int height;
  int row_bytes;
  int pixel_bytes;
  unsigned char* data;
};

struct GRFont {
  GRSurface* texture;
  int char_width;
  int char_height;
};

enum GRRotation {
  ROTATION_NONE = 0,
  ROTATION_RIGHT = 1,
  ROTATION_DOWN = 2,
  ROTATION_LEFT = 3,
};

int gr_init();
void gr_exit();

int gr_fb_width();
int gr_fb_height();

void gr_flip();
void gr_fb_blank(bool blank);

void gr_clear();  // clear entire surface to current color
void gr_color(unsigned char r, unsigned char g, unsigned char b, unsigned char a);
void gr_fill(int x1, int y1, int x2, int y2);

void gr_texticon(int x, int y, GRSurface* icon);

const GRFont* gr_sys_font();
int gr_init_font(const char* name, GRFont** dest);
void gr_text(const GRFont* font, int x, int y, const char* s, bool bold);
int gr_measure(const GRFont* font, const char* s);
void gr_font_size(const GRFont* font, int* x, int* y);

void gr_blit(GRSurface* source, int sx, int sy, int w, int h, int dx, int dy);
unsigned int gr_get_width(GRSurface* surface);
unsigned int gr_get_height(GRSurface* surface);

// Set rotation, flips gr_fb_width/height if 90 degree rotation difference
void gr_rotate(GRRotation rotation);

//
// Input events.
//

struct input_event;

using ev_callback = std::function<int(int fd, uint32_t epevents)>;
using ev_set_key_callback = std::function<int(int code, int value)>;

int ev_init(ev_callback input_cb, bool allow_touch_inputs = false);
void ev_exit();
int ev_add_fd(int fd, ev_callback cb);
void ev_iterate_available_keys(const std::function<void(int)>& f);
void ev_iterate_touch_inputs(const std::function<void(int)>& action);
int ev_sync_key_state(const ev_set_key_callback& set_key_cb);

// 'timeout' has the same semantics as poll(2).
//    0 : don't block
//  < 0 : block forever
//  > 0 : block for 'timeout' milliseconds
int ev_wait(int timeout);

int ev_get_input(int fd, uint32_t epevents, input_event* ev);
void ev_dispatch();
int ev_get_epollfd();

//
// Resources
//

bool matches_locale(const std::string& prefix, const std::string& locale);

// res_create_*_surface() functions return 0 if no error, else
// negative.
//
// A "display" surface is one that is intended to be drawn to the
// screen with gr_blit().  An "alpha" surface is a grayscale image
// interpreted as an alpha mask used to render text in the current
// color (with gr_text() or gr_texticon()).
//
// All these functions load PNG images from "/res/images/${name}.png".

// Load a single display surface from a PNG image.
int res_create_display_surface(const char* name, GRSurface** pSurface);

// Load an array of display surfaces from a single PNG image.  The PNG
// should have a 'Frames' text chunk whose value is the number of
// frames this image represents.  The pixel data itself is interlaced
// by row.
int res_create_multi_display_surface(const char* name, int* frames, int* fps,
                                     GRSurface*** pSurface);

// Load a single alpha surface from a grayscale PNG image.
int res_create_alpha_surface(const char* name, GRSurface** pSurface);

// Load part of a grayscale PNG image that is the first match for the
// given locale.  The image is expected to be a composite of multiple
// translations of the same text, with special added rows that encode
// the subimages' size and intended locale in the pixel data.  See
// bootable/recovery/tools/recovery_l10n for an app that will generate
// these specialized images from Android resources.
int res_create_localized_alpha_surface(const char* name, const char* locale,
                                       GRSurface** pSurface);

// Free a surface allocated by any of the res_create_*_surface()
// functions.
void res_free_surface(GRSurface* surface);

#endif
