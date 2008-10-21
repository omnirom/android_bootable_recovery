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

#include "font_10x18.h"
#include "minui.h"

typedef struct {
    GGLSurface texture;
    unsigned cwidth;
    unsigned cheight;
    unsigned ascent;
} GRFont;

static GRFont *gr_font = 0;
static GGLContext *gr_context = 0;
static GGLSurface gr_font_texture;
static GGLSurface gr_framebuffer[2];
static unsigned gr_active_fb = 0;

static int gr_fb_fd = -1;
static int gr_vt_fd = -1;

static struct fb_var_screeninfo vi;

static int get_framebuffer(GGLSurface *fb)
{
    int fd;
    struct fb_fix_screeninfo fi;
    void *bits;
    
    fd = open("/dev/graphics/fb0", O_RDWR);
    if(fd < 0) {
        perror("cannot open fb0");
        return -1;
    }

    if(ioctl(fd, FBIOGET_FSCREENINFO, &fi) < 0) {
        perror("failed to get fb0 info");
        return -1;
    }

    if(ioctl(fd, FBIOGET_VSCREENINFO, &vi) < 0) {
        perror("failed to get fb0 info");
        return -1;
    }

    bits = mmap(0, fi.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(bits == MAP_FAILED) {
        perror("failed to mmap framebuffer");
        return -1;
    }

    fb->version = sizeof(*fb);
    fb->width = vi.xres;
    fb->height = vi.yres;
    fb->stride = vi.xres;
    fb->data = bits;
    fb->format = GGL_PIXEL_FORMAT_RGB_565;
    
    fb++;
    
    fb->version = sizeof(*fb);
    fb->width = vi.xres;
    fb->height = vi.yres;
    fb->stride = vi.xres;
    fb->data = (void*) (((unsigned) bits) + vi.yres * vi.xres * 2);
    fb->format = GGL_PIXEL_FORMAT_RGB_565;

    return fd;
}

static void set_active_framebuffer(unsigned n)
{
    if(n > 1) return;
    vi.yres_virtual = vi.yres * 2;
    vi.yoffset = n * vi.yres;
    if(ioctl(gr_fb_fd, FBIOPUT_VSCREENINFO, &vi) < 0) {
        fprintf(stderr,"active fb swap failed!\n");
    }
}

static void dumpinfo(struct fb_var_screeninfo *vi)
{
    fprintf(stderr,"vi.xres = %d\n", vi->xres);
    fprintf(stderr,"vi.yres = %d\n", vi->yres);
    fprintf(stderr,"vi.xresv = %d\n", vi->xres_virtual);
    fprintf(stderr,"vi.yresv = %d\n", vi->yres_virtual);
    fprintf(stderr,"vi.xoff = %d\n", vi->xoffset);
    fprintf(stderr,"vi.yoff = %d\n", vi->yoffset);
}

void gr_flip(void)
{
    GGLContext *gl = gr_context;
    
        /* currently active buffer becomes the backbuffer */
    gl->colorBuffer(gl, gr_framebuffer + gr_active_fb);

        /* swap front and back buffers */
    gr_active_fb = (gr_active_fb + 1) & 1;

        /* inform the display driver */
    set_active_framebuffer(gr_active_fb);
}

void gr_color(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
    GGLContext *gl = gr_context;
    GGLint color[4];
    color[0] = ((r << 8) | r) + 1;
    color[1] = ((g << 8) | g) + 1;
    color[2] = ((b << 8) | b) + 1;
    color[3] = ((a << 8) | a) + 1;
    gl->color4xv(gl, color);
}

int gr_measure(const char *s)
{
    return gr_font->cwidth * strlen(s);
}

int gr_text(int x, int y, const char *s)
{
    GGLContext *gl = gr_context;
    GRFont *font = gr_font;
    unsigned off;

    y -= font->ascent;
  
    gl->bindTexture(gl, &font->texture);
    gl->texEnvi(gl, GGL_TEXTURE_ENV, GGL_TEXTURE_ENV_MODE, GGL_REPLACE);
    gl->texGeni(gl, GGL_S, GGL_TEXTURE_GEN_MODE, GGL_ONE_TO_ONE);
    gl->texGeni(gl, GGL_T, GGL_TEXTURE_GEN_MODE, GGL_ONE_TO_ONE);
    gl->enable(gl, GGL_TEXTURE_2D);
    
    while((off = *s++)) {
        off -= 32;
        if(off < 96) {
            gl->texCoord2i(gl, (off * font->cwidth) - x, 0 - y);
            gl->recti(gl, x, y, x + font->cwidth, y + font->cheight);
        }
        x += font->cwidth;
    }

    return x;
}

void gr_fill(int x, int y, int w, int h)
{
    GGLContext *gl = gr_context;
    gl->disable(gl, GGL_TEXTURE_2D);
    gl->recti(gl, x, y, w, h);
}

void gr_blit(gr_surface source, int sx, int sy, int w, int h, int dx, int dy) {
    if (gr_context == NULL) {
        return;
    }
    GGLContext *gl = gr_context;

    gl->bindTexture(gl, (GGLSurface*) source);
    gl->texEnvi(gl, GGL_TEXTURE_ENV, GGL_TEXTURE_ENV_MODE, GGL_REPLACE);
    gl->texGeni(gl, GGL_S, GGL_TEXTURE_GEN_MODE, GGL_ONE_TO_ONE);
    gl->texGeni(gl, GGL_T, GGL_TEXTURE_GEN_MODE, GGL_ONE_TO_ONE);
    gl->enable(gl, GGL_TEXTURE_2D);
    gl->texCoord2i(gl, sx - dx, sy - dy);
    gl->recti(gl, dx, dy, dx + w, dy + h);
}

unsigned int gr_get_width(gr_surface surface) {
    if (surface == NULL) {
        return 0;
    }
    return ((GGLSurface*) surface)->width;
}

unsigned int gr_get_height(gr_surface surface) {
    if (surface == NULL) {
        return 0;
    }
    return ((GGLSurface*) surface)->height;
}

static void gr_init_font(void)
{
    GGLSurface *ftex;
    unsigned char *bits, *rle;
    unsigned char *in, data;

    gr_font = calloc(sizeof(*gr_font), 1);
    ftex = &gr_font->texture; 

    bits = malloc(font.width * font.height);
    
    ftex->version = sizeof(*ftex);
    ftex->width = font.width;
    ftex->height = font.height;
    ftex->stride = font.width;
    ftex->data = (void*) bits;
    ftex->format = GGL_PIXEL_FORMAT_A_8;

    in = font.rundata;
    while((data = *in++)) {
        memset(bits, (data & 0x80) ? 255 : 0, data & 0x7f);
        bits += (data & 0x7f);
    }
    
    gr_font->cwidth = font.cwidth;
    gr_font->cheight = font.cheight;
    gr_font->ascent = font.cheight - 2;
}

int gr_init(void)
{
    GGLContext *gl;
    int fd;

    gglInit(&gr_context);
    gl = gr_context;

    gr_init_font();

    fd = open("/dev/tty0", O_RDWR | O_SYNC);
    if(fd < 0) return -1;

    if(ioctl(fd, KDSETMODE, (void*) KD_GRAPHICS)) {
        close(fd);
        return -1;
    }

    gr_fb_fd = get_framebuffer(gr_framebuffer);
    
    if(gr_fb_fd < 0) {
        ioctl(fd, KDSETMODE, (void*) KD_TEXT);
        close(fd);
        return -1;
    }

    gr_vt_fd = fd;

        /* start with 0 as front (displayed) and 1 as back (drawing) */
    gr_active_fb = 0;
    set_active_framebuffer(0);
    gl->colorBuffer(gl, gr_framebuffer + 1);

    gl->activeTexture(gl, 0);
    gl->enable(gl, GGL_BLEND);
    gl->blendFunc(gl, GGL_SRC_ALPHA, GGL_ONE_MINUS_SRC_ALPHA);

    return 0;
}

void gr_exit(void)
{
    close(gr_fb_fd);
    gr_fb_fd = -1;
    
    ioctl(gr_vt_fd, KDSETMODE, (void*) KD_TEXT);
    close(gr_vt_fd);
    gr_vt_fd = -1;
}

int gr_fb_width(void)
{
    return gr_framebuffer[0].width;
}

int gr_fb_height(void)
{
    return gr_framebuffer[0].height;
}

gr_pixel *gr_fb_data(void)
{
    return (unsigned short *) gr_framebuffer[1 - gr_active_fb].data;
}
