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

#include <stdbool.h>
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

#ifdef BOARD_USE_CUSTOM_RECOVERY_FONT
#include BOARD_USE_CUSTOM_RECOVERY_FONT
#else
#include "font_10x18.h"
#endif

#ifdef RECOVERY_BGRA
#define PIXEL_FORMAT GGL_PIXEL_FORMAT_BGRA_8888
#define PIXEL_SIZE 4
#endif
#ifdef RECOVERY_RGBX
#define PIXEL_FORMAT GGL_PIXEL_FORMAT_RGBX_8888
#define PIXEL_SIZE 4
#endif
#ifndef PIXEL_FORMAT
#define PIXEL_FORMAT GGL_PIXEL_FORMAT_RGB_565
#define PIXEL_SIZE 2
#endif

#define NUM_BUFFERS 2

// #define PRINT_SCREENINFO 1 // Enables printing of screen info to log

typedef struct {
    GGLSurface texture;
    unsigned offset[97];
    unsigned cheight;
    unsigned ascent;
} GRFont;

static GRFont *gr_font = 0;
static GGLContext *gr_context = 0;
static GGLSurface gr_font_texture;
static GGLSurface gr_framebuffer[NUM_BUFFERS];
static GGLSurface gr_mem_surface;
static unsigned gr_active_fb = 0;
static unsigned double_buffering = 0;
static int gr_is_curr_clr_opaque = 0;

static int gr_fb_fd = -1;
static int gr_vt_fd = -1;

static struct fb_var_screeninfo vi;
static struct fb_fix_screeninfo fi;

#ifdef PRINT_SCREENINFO
static void print_fb_var_screeninfo()
{
	printf("vi.xres: %d\n", vi.xres);
	printf("vi.yres: %d\n", vi.yres);
	printf("vi.xres_virtual: %d\n", vi.xres_virtual);
	printf("vi.yres_virtual: %d\n", vi.yres_virtual);
	printf("vi.xoffset: %d\n", vi.xoffset);
	printf("vi.yoffset: %d\n", vi.yoffset);
	printf("vi.bits_per_pixel: %d\n", vi.bits_per_pixel);
	printf("vi.grayscale: %d\n", vi.grayscale);
}
#endif

static int get_framebuffer(GGLSurface *fb)
{
    int fd;
    void *bits;

    fd = open("/dev/graphics/fb0", O_RDWR);
    if (fd < 0) {
        perror("cannot open fb0");
        return -1;
    }

    if (ioctl(fd, FBIOGET_VSCREENINFO, &vi) < 0) {
        perror("failed to get fb0 info");
        close(fd);
        return -1;
    }

    fprintf(stderr, "Pixel format: %dx%d @ %dbpp\n", vi.xres, vi.yres, vi.bits_per_pixel);

    vi.bits_per_pixel = PIXEL_SIZE * 8;
    if (PIXEL_FORMAT == GGL_PIXEL_FORMAT_BGRA_8888) {
        fprintf(stderr, "Pixel format: BGRA_8888\n");
        if (PIXEL_SIZE != 4)    fprintf(stderr, "E: Pixel Size mismatch!\n");
        vi.red.offset     = 8;
        vi.red.length     = 8;
        vi.green.offset   = 16;
        vi.green.length   = 8;
        vi.blue.offset    = 24;
        vi.blue.length    = 8;
        vi.transp.offset  = 0;
        vi.transp.length  = 8;
    } else if (PIXEL_FORMAT == GGL_PIXEL_FORMAT_RGBX_8888) {
        fprintf(stderr, "Pixel format: RGBX_8888\n");
        if (PIXEL_SIZE != 4)    fprintf(stderr, "E: Pixel Size mismatch!\n");
        vi.red.offset     = 24;
        vi.red.length     = 8;
        vi.green.offset   = 16;
        vi.green.length   = 8;
        vi.blue.offset    = 8;
        vi.blue.length    = 8;
        vi.transp.offset  = 0;
        vi.transp.length  = 8;
    } else if (PIXEL_FORMAT == GGL_PIXEL_FORMAT_RGB_565) {
#ifdef RECOVERY_RGB_565
		fprintf(stderr, "Pixel format: RGB_565\n");
		vi.blue.offset    = 0;
		vi.green.offset   = 5;
		vi.red.offset     = 11;
#else
        fprintf(stderr, "Pixel format: BGR_565\n");
		vi.blue.offset    = 11;
		vi.green.offset   = 5;
		vi.red.offset     = 0;
#endif
		if (PIXEL_SIZE != 2)    fprintf(stderr, "E: Pixel Size mismatch!\n");
		vi.blue.length    = 5;
		vi.green.length   = 6;
		vi.red.length     = 5;
        vi.blue.msb_right = 0;
        vi.green.msb_right = 0;
        vi.red.msb_right = 0;
        vi.transp.offset  = 0;
        vi.transp.length  = 0;
    }
    else
    {
        perror("unknown pixel format");
        close(fd);
        return -1;
    }

    vi.vmode = FB_VMODE_NONINTERLACED;
    vi.activate = FB_ACTIVATE_NOW | FB_ACTIVATE_FORCE;

    if (ioctl(fd, FBIOPUT_VSCREENINFO, &vi) < 0) {
        perror("failed to put fb0 info");
        close(fd);
        return -1;
    }

    if (ioctl(fd, FBIOGET_FSCREENINFO, &fi) < 0) {
        perror("failed to get fb0 info");
        close(fd);
        return -1;
    }

    bits = mmap(0, fi.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (bits == MAP_FAILED) {
        perror("failed to mmap framebuffer");
        close(fd);
        return -1;
    }

#ifdef RECOVERY_GRAPHICS_USE_LINELENGTH
    vi.xres_virtual = fi.line_length / PIXEL_SIZE;
#endif

    fb->version = sizeof(*fb);
    fb->width = vi.xres;
    fb->height = vi.yres;
#ifdef BOARD_HAS_JANKY_BACKBUFFER
    printf("setting JANKY BACKBUFFER\n");
    fb->stride = fi.line_length/2;
#else
    fb->stride = vi.xres_virtual;
#endif
    fb->data = bits;
    fb->format = PIXEL_FORMAT;
    memset(fb->data, 0, vi.yres * fb->stride * PIXEL_SIZE);

    fb++;

    /* check if we can use double buffering */
    if (vi.yres * fi.line_length * 2 > fi.smem_len)
        return fd;

    double_buffering = 1;

    fb->version = sizeof(*fb);
    fb->width = vi.xres;
    fb->height = vi.yres;
#ifdef BOARD_HAS_JANKY_BACKBUFFER
    fb->stride = fi.line_length/2;
    fb->data = (void*) (((unsigned) bits) + vi.yres * fi.line_length);
#else
    fb->stride = vi.xres_virtual;
    fb->data = (void*) (((unsigned) bits) + vi.yres * fb->stride * PIXEL_SIZE);
#endif
    fb->format = PIXEL_FORMAT;
    memset(fb->data, 0, vi.yres * fb->stride * PIXEL_SIZE);

#ifdef PRINT_SCREENINFO
	print_fb_var_screeninfo();
#endif

    return fd;
}

static void get_memory_surface(GGLSurface* ms) {
  ms->version = sizeof(*ms);
  ms->width = vi.xres;
  ms->height = vi.yres;
  ms->stride = vi.xres_virtual;
  ms->data = malloc(vi.xres_virtual * vi.yres * PIXEL_SIZE);
  ms->format = PIXEL_FORMAT;
}

static void set_active_framebuffer(unsigned n)
{
    if (n > 1  || !double_buffering) return;
    vi.yres_virtual = vi.yres * NUM_BUFFERS;
    vi.yoffset = n * vi.yres;
//    vi.bits_per_pixel = PIXEL_SIZE * 8;
    if (ioctl(gr_fb_fd, FBIOPUT_VSCREENINFO, &vi) < 0) {
        perror("active fb swap failed");
    }
}

void gr_flip(void)
{
    GGLContext *gl = gr_context;

    /* swap front and back buffers */
    if (double_buffering)
        gr_active_fb = (gr_active_fb + 1) & 1;

#ifdef BOARD_HAS_FLIPPED_SCREEN
    /* flip buffer 180 degrees for devices with physicaly inverted screens */
    unsigned int i;
    unsigned int j;
    uint8_t tmp;
    for (i = 0; i < ((vi.xres_virtual * vi.yres)/2); i++) {
	for (j = 0; j < PIXEL_SIZE; j++) {
		tmp = gr_mem_surface.data[i * PIXEL_SIZE + j];
		gr_mem_surface.data[i * PIXEL_SIZE + j] = gr_mem_surface.data[(vi.xres_virtual * vi.yres * PIXEL_SIZE) - ((i+1) * PIXEL_SIZE) + j];
		gr_mem_surface.data[(vi.xres_virtual * vi.yres * PIXEL_SIZE) - ((i+1) * PIXEL_SIZE) + j] = tmp;
	}
    }
#endif

    /* copy data from the in-memory surface to the buffer we're about
     * to make active. */
    memcpy(gr_framebuffer[gr_active_fb].data, gr_mem_surface.data,
           vi.xres_virtual * vi.yres * PIXEL_SIZE);

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

    gr_is_curr_clr_opaque = (a == 255);
}

int gr_measureEx(const char *s, void* font)
{
    GRFont* fnt = (GRFont*) font;
    int total = 0;
    unsigned pos;
    unsigned off;

    if (!fnt)   fnt = gr_font;

    while ((off = *s++))
    {
        off -= 32;
        if (off < 96)
            total += (fnt->offset[off+1] - fnt->offset[off]);
    }
    return total;
}

int gr_maxExW(const char *s, void* font, int max_width)
{
    GRFont* fnt = (GRFont*) font;
    int total = 0;
    unsigned pos;
    unsigned off;

    if (!fnt)   fnt = gr_font;

    while ((off = *s++))
    {
        off -= 32;
        if (off < 96) {
            max_width -= (fnt->offset[off+1] - fnt->offset[off]);
			if (max_width > 0) {
				total++;
			} else {
				return total;
			}
		}
    }
    return total;
}

unsigned character_width(const char *s, void* pFont)
{
	GRFont *font = (GRFont*) pFont;
	unsigned off;

	/* Handle default font */
    if (!font)  font = gr_font;

	off = *s - 32;
	if (off == 0)
		return 0;

	return font->offset[off+1] - font->offset[off];
}

int gr_textEx(int x, int y, const char *s, void* pFont)
{
    GGLContext *gl = gr_context;
    GRFont *font = (GRFont*) pFont;
    unsigned off;
    unsigned cwidth;

    /* Handle default font */
    if (!font)  font = gr_font;

    gl->bindTexture(gl, &font->texture);
    gl->texEnvi(gl, GGL_TEXTURE_ENV, GGL_TEXTURE_ENV_MODE, GGL_REPLACE);
    gl->texGeni(gl, GGL_S, GGL_TEXTURE_GEN_MODE, GGL_ONE_TO_ONE);
    gl->texGeni(gl, GGL_T, GGL_TEXTURE_GEN_MODE, GGL_ONE_TO_ONE);
    gl->enable(gl, GGL_TEXTURE_2D);

    while((off = *s++)) {
        off -= 32;
        cwidth = 0;
        if (off < 96) {
            cwidth = font->offset[off+1] - font->offset[off];
			gl->texCoord2i(gl, (font->offset[off]) - x, 0 - y);
			gl->recti(gl, x, y, x + cwidth, y + font->cheight);
			x += cwidth;
        }
    }

    return x;
}

int gr_textExW(int x, int y, const char *s, void* pFont, int max_width)
{
    GGLContext *gl = gr_context;
    GRFont *font = (GRFont*) pFont;
    unsigned off;
    unsigned cwidth;

    /* Handle default font */
    if (!font)  font = gr_font;

    gl->bindTexture(gl, &font->texture);
    gl->texEnvi(gl, GGL_TEXTURE_ENV, GGL_TEXTURE_ENV_MODE, GGL_REPLACE);
    gl->texGeni(gl, GGL_S, GGL_TEXTURE_GEN_MODE, GGL_ONE_TO_ONE);
    gl->texGeni(gl, GGL_T, GGL_TEXTURE_GEN_MODE, GGL_ONE_TO_ONE);
    gl->enable(gl, GGL_TEXTURE_2D);

    while((off = *s++)) {
        off -= 32;
        cwidth = 0;
        if (off < 96) {
            cwidth = font->offset[off+1] - font->offset[off];
			if ((x + (int)cwidth) < max_width) {
				gl->texCoord2i(gl, (font->offset[off]) - x, 0 - y);
				gl->recti(gl, x, y, x + cwidth, y + font->cheight);
				x += cwidth;
			} else {
				gl->texCoord2i(gl, (font->offset[off]) - x, 0 - y);
				gl->recti(gl, x, y, max_width, y + font->cheight);
				x = max_width;
				return x;
			}
        }
    }

    return x;
}

int gr_textExWH(int x, int y, const char *s, void* pFont, int max_width, int max_height)
{
    GGLContext *gl = gr_context;
    GRFont *font = (GRFont*) pFont;
    unsigned off;
    unsigned cwidth;
	int rect_x, rect_y;

    /* Handle default font */
    if (!font)  font = gr_font;

    gl->bindTexture(gl, &font->texture);
    gl->texEnvi(gl, GGL_TEXTURE_ENV, GGL_TEXTURE_ENV_MODE, GGL_REPLACE);
    gl->texGeni(gl, GGL_S, GGL_TEXTURE_GEN_MODE, GGL_ONE_TO_ONE);
    gl->texGeni(gl, GGL_T, GGL_TEXTURE_GEN_MODE, GGL_ONE_TO_ONE);
    gl->enable(gl, GGL_TEXTURE_2D);

    while((off = *s++)) {
        off -= 32;
        cwidth = 0;
        if (off < 96) {
            cwidth = font->offset[off+1] - font->offset[off];
			if ((x + (int)cwidth) < max_width)
				rect_x = x + cwidth;
			else
				rect_x = max_width;
			if (y + font->cheight < (unsigned int)(max_height))
				rect_y = y + font->cheight;
			else
				rect_y = max_height;

			gl->texCoord2i(gl, (font->offset[off]) - x, 0 - y);
			gl->recti(gl, x, y, rect_x, rect_y);
			x += cwidth;
			if (x > max_width)
				return x;
        }
    }

    return x;
}

int twgr_text(int x, int y, const char *s)
{
    GGLContext *gl = gr_context;
    GRFont *font = gr_font;
    unsigned off;
    unsigned cwidth = 0;

    y -= font->ascent;

    gl->bindTexture(gl, &font->texture);
    gl->texEnvi(gl, GGL_TEXTURE_ENV, GGL_TEXTURE_ENV_MODE, GGL_REPLACE);
    gl->texGeni(gl, GGL_S, GGL_TEXTURE_GEN_MODE, GGL_ONE_TO_ONE);
    gl->texGeni(gl, GGL_T, GGL_TEXTURE_GEN_MODE, GGL_ONE_TO_ONE);
    gl->enable(gl, GGL_TEXTURE_2D);

    while((off = *s++)) {
        off -= 32;
        if (off < 96) {
            cwidth = font->offset[off+1] - font->offset[off];
            gl->texCoord2i(gl, (off * cwidth) - x, 0 - y);
            gl->recti(gl, x, y, x + cwidth, y + font->cheight);
        }
        x += cwidth;
    }

    return x;
}

void gr_fill(int x, int y, int w, int h)
{
    GGLContext *gl = gr_context;

    if(gr_is_curr_clr_opaque)
        gl->disable(gl, GGL_BLEND);

    gl->disable(gl, GGL_TEXTURE_2D);
    gl->recti(gl, x, y, x + w, y + h);

    if(gr_is_curr_clr_opaque)
        gl->enable(gl, GGL_BLEND);
}

void gr_blit(gr_surface source, int sx, int sy, int w, int h, int dx, int dy) {
    if (gr_context == NULL) {
        return;
    }

    GGLContext *gl = gr_context;
    GGLSurface *surface = (GGLSurface*)source;

    if(surface->format == GGL_PIXEL_FORMAT_RGBX_8888)
        gl->disable(gl, GGL_BLEND);

    gl->bindTexture(gl, surface);
    gl->texEnvi(gl, GGL_TEXTURE_ENV, GGL_TEXTURE_ENV_MODE, GGL_REPLACE);
    gl->texGeni(gl, GGL_S, GGL_TEXTURE_GEN_MODE, GGL_ONE_TO_ONE);
    gl->texGeni(gl, GGL_T, GGL_TEXTURE_GEN_MODE, GGL_ONE_TO_ONE);
    gl->enable(gl, GGL_TEXTURE_2D);
    gl->texCoord2i(gl, sx - dx, sy - dy);
    gl->recti(gl, dx, dy, dx + w, dy + h);

    if(surface->format == GGL_PIXEL_FORMAT_RGBX_8888)
        gl->enable(gl, GGL_BLEND);
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

void* gr_loadFont(const char* fontName)
{
    int fd;
    GRFont *font = 0;
    GGLSurface *ftex;
    unsigned char *bits, *rle;
    unsigned char *in, data;
    unsigned width, height;
    unsigned element;

    fd = open(fontName, O_RDONLY);
    if (fd == -1)
    {
        char tmp[128];

        sprintf(tmp, "/res/fonts/%s.dat", fontName);
        fd = open(tmp, O_RDONLY);
        if (fd == -1)
            return NULL;
    }

    font = calloc(sizeof(*font), 1);
    ftex = &font->texture;

    read(fd, &width, sizeof(unsigned));
    read(fd, &height, sizeof(unsigned));
    read(fd, font->offset, sizeof(unsigned) * 96);
    font->offset[96] = width;

    bits = malloc(width * height);
    memset(bits, 0, width * height);

    unsigned pos = 0;
    while (pos < width * height)
    {
        int bit;

        read(fd, &data, 1);
        for (bit = 0; bit < 8; bit++)
        {
            if (data & (1 << (7-bit)))  bits[pos++] = 255;
            else                        bits[pos++] = 0;

            if (pos == width * height)  break;
        }
    }
    close(fd);

    ftex->version = sizeof(*ftex);
    ftex->width = width;
    ftex->height = height;
    ftex->stride = width;
    ftex->data = (void*) bits;
    ftex->format = GGL_PIXEL_FORMAT_A_8;
    font->cheight = height;
    font->ascent = height - 2;
    return (void*) font;
}

int gr_getFontDetails(void* font, unsigned* cheight, unsigned* maxwidth)
{
    GRFont *fnt = (GRFont*) font;

    if (!fnt)   fnt = gr_font;
    if (!fnt)   return -1;

    if (cheight)    *cheight = fnt->cheight;
    if (maxwidth)
    {
        int pos;
        *maxwidth = 0;
        for (pos = 0; pos < 96; pos++)
        {
            unsigned int width = fnt->offset[pos+1] - fnt->offset[pos];
            if (width > *maxwidth)
            {
                *maxwidth = width;
            }
        }
    }
    return 0;
}

static void gr_init_font(void)
{
    int fontRes;
    GGLSurface *ftex;
    unsigned char *bits, *rle;
    unsigned char *in, data;
    unsigned width, height;
    unsigned element;

    gr_font = calloc(sizeof(*gr_font), 1);
    ftex = &gr_font->texture;

    width = font.width;
    height = font.height;

    bits = malloc(width * height);
    rle = bits;

    in = font.rundata;
    while((data = *in++))
    {
        memset(rle, (data & 0x80) ? 255 : 0, data & 0x7f);
        rle += (data & 0x7f);
    }
    for (element = 0; element < 97; element++)
    {
        gr_font->offset[element] = (element * font.cwidth);
    }

    ftex->version = sizeof(*ftex);
    ftex->width = width;
    ftex->height = height;
    ftex->stride = width;
    ftex->data = (void*) bits;
    ftex->format = GGL_PIXEL_FORMAT_A_8;
    gr_font->cheight = height;
    gr_font->ascent = height - 2;
    return;
}

int gr_init(void)
{
    gglInit(&gr_context);
    GGLContext *gl = gr_context;

    gr_init_font();
    gr_vt_fd = open("/dev/tty0", O_RDWR | O_SYNC);
    if (gr_vt_fd < 0) {
        // This is non-fatal; post-Cupcake kernels don't have tty0.
    } else if (ioctl(gr_vt_fd, KDSETMODE, (void*) KD_GRAPHICS)) {
        // However, if we do open tty0, we expect the ioctl to work.
        perror("failed KDSETMODE to KD_GRAPHICS on tty0");
        gr_exit();
        return -1;
    }

    gr_fb_fd = get_framebuffer(gr_framebuffer);
    if (gr_fb_fd < 0) {
        perror("Unable to get framebuffer.\n");
        gr_exit();
        return -1;
    }

    get_memory_surface(&gr_mem_surface);

    fprintf(stderr, "framebuffer: fd %d (%d x %d)\n",
            gr_fb_fd, gr_framebuffer[0].width, gr_framebuffer[0].height);

    /* start with 0 as front (displayed) and 1 as back (drawing) */
    gr_active_fb = 0;
    set_active_framebuffer(0);
    gl->colorBuffer(gl, &gr_mem_surface);

    gl->activeTexture(gl, 0);
    gl->enable(gl, GGL_BLEND);
    gl->blendFunc(gl, GGL_SRC_ALPHA, GGL_ONE_MINUS_SRC_ALPHA);

//    gr_fb_blank(true);
//    gr_fb_blank(false);

    return 0;
}

void gr_exit(void)
{
    close(gr_fb_fd);
    gr_fb_fd = -1;

    free(gr_mem_surface.data);

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
    return (unsigned short *) gr_mem_surface.data;
}

int gr_fb_blank(int blank)
{
    int ret;

    ret = ioctl(gr_fb_fd, FBIOBLANK, blank ? FB_BLANK_POWERDOWN : FB_BLANK_UNBLANK);
    if (ret < 0)
        perror("ioctl(): blank");
	return ret;
}

int gr_get_surface(gr_surface* surface)
{
    GGLSurface* ms = malloc(sizeof(GGLSurface));
    if (!ms)    return -1;

    // Allocate the data
    get_memory_surface(ms);

    // Now, copy the data
    memcpy(ms->data, gr_mem_surface.data, vi.xres * vi.yres * vi.bits_per_pixel / 8);

    *surface = (gr_surface*) ms;
    return 0;
}

int gr_free_surface(gr_surface surface)
{
    if (!surface)
        return -1;

    GGLSurface* ms = (GGLSurface*) surface;
    free(ms->data);
    free(ms);
    return 0;
}

void gr_write_frame_to_file(int fd)
{
    write(fd, gr_mem_surface.data, vi.xres * vi.yres * vi.bits_per_pixel / 8);
}
