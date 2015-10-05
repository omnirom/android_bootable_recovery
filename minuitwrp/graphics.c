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

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>

#include <linux/fb.h>
#include <linux/kd.h>

#include <pixelflinger/pixelflinger.h>

#include "minui.h"
#include "../gui/placement.h"

#ifdef RECOVERY_BGRA
#define PIXEL_FORMAT GGL_PIXEL_FORMAT_BGRA_8888
#define PIXEL_SIZE 4
#endif
#ifdef RECOVERY_RGBA
#define PIXEL_FORMAT GGL_PIXEL_FORMAT_RGBA_8888
#define PIXEL_SIZE   4
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
#define MAX_DISPLAY_DIM  2048

// #define PRINT_SCREENINFO 1 // Enables printing of screen info to log

typedef struct {
    int type;
    GGLSurface texture;
    unsigned offset[97];
    unsigned cheight;
    unsigned ascent;
} GRFont;

static GRFont *gr_font = 0;
static GGLContext *gr_context = 0;
static GGLSurface gr_font_texture;
static GGLSurface gr_framebuffer[NUM_BUFFERS];
GGLSurface gr_mem_surface;
static unsigned gr_active_fb = 0;
static unsigned double_buffering = 0;
static int gr_is_curr_clr_opaque = 0;

static int gr_fb_fd = -1;
static int gr_vt_fd = -1;

struct fb_var_screeninfo vi;
static struct fb_fix_screeninfo fi;

static bool has_overlay = false;
static int leftSplit = 0;
static int rightSplit = 0;

bool target_has_overlay(char *version);
int free_ion_mem(void);
int alloc_ion_mem(unsigned int size);
int allocate_overlay(int fd, GGLSurface gr_fb[]);
int free_overlay(int fd);
int overlay_display_frame(int fd, GGLubyte* data, size_t size);

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

#ifdef MSM_BSP
int getLeftSplit(void) {
   //Default even split for all displays with high res
   int lSplit = vi.xres / 2;

   //Override if split published by driver
   if (leftSplit)
       lSplit = leftSplit;

   return lSplit;
}

int getRightSplit(void) {
   return rightSplit;
}


void setDisplaySplit(void) {
    char split[64] = {0};
    FILE* fp = fopen("/sys/class/graphics/fb0/msm_fb_split", "r");
    if (fp) {
        //Format "left right" space as delimiter
        if(fread(split, sizeof(char), 64, fp)) {
            leftSplit = atoi(split);
            printf("Left Split=%d\n",leftSplit);
            char *rght = strpbrk(split, " ");
            if (rght)
                rightSplit = atoi(rght + 1);
            printf("Right Split=%d\n", rightSplit);
        }
    } else {
        printf("Failed to open mdss_fb_split node\n");
    }
    if (fp)
        fclose(fp);
}

bool isDisplaySplit(void) {
    if (vi.xres > MAX_DISPLAY_DIM)
        return true;
    //check if right split is set by driver
    if (getRightSplit())
        return true;

    return false;
}

int getFbXres(void) {
    return vi.xres;
}

int getFbYres (void) {
    return vi.yres;
}
#endif // MSM_BSP

static int get_framebuffer(GGLSurface *fb)
{
    int fd, index = 0;
    void *bits;

    fd = open("/dev/graphics/fb0", O_RDWR);
    
    while (fd < 0 && index < 30) {
        usleep(1000);
        fd = open("/dev/graphics/fb0", O_RDWR);
        index++;
    }
    if (fd < 0) {
        perror("cannot open fb0\n");
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
    } else if (PIXEL_FORMAT == GGL_PIXEL_FORMAT_RGBA_8888) {
        fprintf(stderr, "Pixel format: RGBA_8888\n");
        if (PIXEL_SIZE != 4)    fprintf(stderr, "E: Pixel Size mismatch!\n");
        vi.red.offset     = 0;
        vi.red.length     = 8;
        vi.green.offset   = 8;
        vi.green.length   = 8;
        vi.blue.offset    = 16;
        vi.blue.length    = 8;
        vi.transp.offset  = 24;
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

#ifdef MSM_BSP
    has_overlay = target_has_overlay(fi.id);

    if (isTargetMdp5())
        setDisplaySplit();
#else
    has_overlay = false;
#endif

    if (!has_overlay) {
        printf("Not using qualcomm overlay, '%s'\n", fi.id);
        bits = mmap(0, fi.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (bits == MAP_FAILED) {
            perror("failed to mmap framebuffer");
            close(fd);
            return -1;
        }
    } else {
        printf("Using qualcomm overlay\n");
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
    fb->format = PIXEL_FORMAT;
    if (!has_overlay) {
        fb->data = bits;
        memset(fb->data, 0, vi.yres * fb->stride * PIXEL_SIZE);
    }

    fb++;

#ifndef TW_DISABLE_DOUBLE_BUFFERING
    /* check if we can use double buffering */
    if (vi.yres * fi.line_length * 2 > fi.smem_len)
#else
    printf("TW_DISABLE_DOUBLE_BUFFERING := true\n");
#endif
        return fd;

    double_buffering = 1;

    fb->version = sizeof(*fb);
    fb->width = vi.xres;
    fb->height = vi.yres;
#ifdef BOARD_HAS_JANKY_BACKBUFFER
    fb->stride = fi.line_length/2;
    fb->data = (GGLubyte*) (((unsigned long) bits) + vi.yres * fi.line_length);
#else
    fb->stride = vi.xres_virtual;
    fb->data = (GGLubyte*) (((unsigned long) bits) + vi.yres * fb->stride * PIXEL_SIZE);
#endif
    fb->format = PIXEL_FORMAT;
    if (!has_overlay) {
        memset(fb->data, 0, vi.yres * fb->stride * PIXEL_SIZE);
    }

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
    if (-EINVAL == overlay_display_frame(gr_fb_fd, gr_mem_surface.data,
                                         (fi.line_length * vi.yres))) {
        GGLContext *gl = gr_context;

        /* swap front and back buffers */
        if (double_buffering)
            gr_active_fb = (gr_active_fb + 1) & 1;

#ifdef BOARD_HAS_FLIPPED_SCREEN
        /* flip buffer 180 degrees for devices with physicaly inverted screens */
        unsigned int i;
        unsigned int j;
        for (i = 0; i < vi.yres; i++) {
            for (j = 0; j < vi.xres; j++) {
                memcpy(gr_framebuffer[gr_active_fb].data + (i * vi.xres_virtual + j) * PIXEL_SIZE,
                       gr_mem_surface.data + ((vi.yres - i - 1) * vi.xres_virtual + vi.xres - j - 1) * PIXEL_SIZE, PIXEL_SIZE);
            }
        }
#else
        /* copy data from the in-memory surface to the buffer we're about
         * to make active. */
        memcpy(gr_framebuffer[gr_active_fb].data, gr_mem_surface.data,
               vi.xres_virtual * vi.yres * PIXEL_SIZE);
#endif

        /* inform the display driver */
        set_active_framebuffer(gr_active_fb);
    }
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

    if (!fnt)
        return 0;

    return gr_ttf_measureEx(s, font);
}

int gr_maxExW(const char *s, void* font, int max_width)
{
    GRFont* fnt = (GRFont*) font;

    if (!fnt)
        return 0;

    return gr_ttf_maxExW(s, font, max_width);
}

int gr_textEx(int x, int y, const char *s, void* pFont)
{
    GGLContext *gl = gr_context;
    GRFont *font = (GRFont*) pFont;

    if (!font)
        return 0;

    return gr_ttf_textExWH(gl, x, y, s, pFont, -1, -1);
}

int gr_textEx_scaleW(int x, int y, const char *s, void* pFont, int max_width, int placement, int scale)
{
    GGLContext *gl = gr_context;
    void* vfont = pFont;
    GRFont *font = (GRFont*) pFont;
    unsigned off;
    unsigned cwidth;
    int y_scale = 0, measured_width, measured_height, ret, new_height;

    if (!s || strlen(s) == 0 || !font)
        return 0;

    measured_height = gr_ttf_getMaxFontHeight(font);

    if (scale) {
        measured_width = gr_ttf_measureEx(s, vfont);
        if (measured_width > max_width) {
            // Adjust font size down until the text fits
            void *new_font = gr_ttf_scaleFont(vfont, max_width, measured_width);
            if (!new_font) {
                printf("gr_textEx_scaleW new_font is NULL\n");
                return 0;
            }
            measured_width = gr_ttf_measureEx(s, new_font);
            // These next 2 lines adjust the y point based on the new font's height
            new_height = gr_ttf_getMaxFontHeight(new_font);
            y_scale = (measured_height - new_height) / 2;
            vfont = new_font;
        }
    } else
        measured_width = gr_ttf_measureEx(s, vfont);

    int x_adj = measured_width;
    if (measured_width > max_width)
        x_adj = max_width;

    if (placement != TOP_LEFT && placement != BOTTOM_LEFT && placement != TEXT_ONLY_RIGHT) {
        if (placement == CENTER || placement == CENTER_X_ONLY)
            x -= (x_adj / 2);
        else
            x -= x_adj;
    }

    if (placement != TOP_LEFT && placement != TOP_RIGHT) {
        if (placement == CENTER || placement == TEXT_ONLY_RIGHT)
            y -= (measured_height / 2);
        else if (placement == BOTTOM_LEFT || placement == BOTTOM_RIGHT)
            y -= measured_height;
    }
    return gr_ttf_textExWH(gl, x, y + y_scale, s, vfont, measured_width + x, -1);
}

int gr_textExW(int x, int y, const char *s, void* pFont, int max_width)
{
    GGLContext *gl = gr_context;
    GRFont *font = (GRFont*) pFont;

    if (!font)
        return 0;

    return gr_ttf_textExWH(gl, x, y, s, pFont, max_width, -1);
}

int gr_textExWH(int x, int y, const char *s, void* pFont, int max_width, int max_height)
{
    GGLContext *gl = gr_context;
    GRFont *font = (GRFont*) pFont;

    if (!font)
        return 0;

    return gr_ttf_textExWH(gl, x, y, s, pFont, max_width, max_height);
}

void gr_clip(int x, int y, int w, int h)
{
    GGLContext *gl = gr_context;
    gl->scissor(gl, x, y, w, h);
    gl->enable(gl, GGL_SCISSOR_TEST);
}

void gr_noclip()
{
    GGLContext *gl = gr_context;
    gl->scissor(gl, 0, 0, gr_fb_width(), gr_fb_height());
    gl->disable(gl, GGL_SCISSOR_TEST);
}

void gr_fill(int x, int y, int w, int h)
{
    GGLContext *gl = gr_context;

    if(gr_is_curr_clr_opaque)
        gl->disable(gl, GGL_BLEND);

    gl->recti(gl, x, y, x + w, y + h);

    if(gr_is_curr_clr_opaque)
        gl->enable(gl, GGL_BLEND);
}

void gr_line(int x0, int y0, int x1, int y1, int width)
{
    GGLContext *gl = gr_context;

    if(gr_is_curr_clr_opaque)
        gl->disable(gl, GGL_BLEND);

    const int coords0[2] = { x0 << 4, y0 << 4 };
    const int coords1[2] = { x1 << 4, y1 << 4 };
    gl->linex(gl, coords0, coords1, width << 4);

    if(gr_is_curr_clr_opaque)
        gl->enable(gl, GGL_BLEND);
}

gr_surface gr_render_circle(int radius, unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
    int rx, ry;
    GGLSurface *surface;
    const int diameter = radius*2 + 1;
    const int radius_check = radius*radius + radius*0.8;
    const uint32_t px = (a << 24) | (b << 16) | (g << 8) | r;
    uint32_t *data;

    surface = malloc(sizeof(GGLSurface));
    memset(surface, 0, sizeof(GGLSurface));

    data = malloc(diameter * diameter * 4);
    memset(data, 0, diameter * diameter * 4);

    surface->version = sizeof(surface);
    surface->width = diameter;
    surface->height = diameter;
    surface->stride = diameter;
    surface->data = (GGLubyte*)data;
    surface->format = GGL_PIXEL_FORMAT_RGBA_8888;

    for(ry = -radius; ry <= radius; ++ry)
        for(rx = -radius; rx <= radius; ++rx)
            if(rx*rx+ry*ry <= radius_check)
                *(data + diameter*(radius + ry) + (radius+rx)) = px;

    return surface;
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
    gl->disable(gl, GGL_TEXTURE_2D);

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

int gr_getMaxFontHeight(void *font)
{
    GRFont *fnt = (GRFont*) font;

    if (!fnt)
        return -1;

    return gr_ttf_getMaxFontHeight(font);
}

int gr_init(void)
{
    gglInit(&gr_context);
    GGLContext *gl = gr_context;

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
    if (!has_overlay)
        set_active_framebuffer(0);
    gl->colorBuffer(gl, &gr_mem_surface);

    gl->activeTexture(gl, 0);
    gl->enable(gl, GGL_BLEND);
    gl->blendFunc(gl, GGL_SRC_ALPHA, GGL_ONE_MINUS_SRC_ALPHA);

#ifdef TW_SCREEN_BLANK_ON_BOOT
    printf("TW_SCREEN_BLANK_ON_BOOT := true\n");
    gr_fb_blank(true);
    gr_fb_blank(false);
#endif

    if (!alloc_ion_mem(fi.line_length * vi.yres))
        allocate_overlay(gr_fb_fd, gr_framebuffer);

    return 0;
}

void gr_exit(void)
{
    free_overlay(gr_fb_fd);
    free_ion_mem();

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
    //if (blank)
        //free_overlay(gr_fb_fd);

    ret = ioctl(gr_fb_fd, FBIOBLANK, blank ? FB_BLANK_POWERDOWN : FB_BLANK_UNBLANK);
    if (ret < 0)
        perror("ioctl(): blank");

    //if (!blank)
        //allocate_overlay(gr_fb_fd, gr_framebuffer);
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
