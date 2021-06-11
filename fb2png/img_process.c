/*
 *   -- http://android-fb2png.googlecode.com/svn/trunk/img_process.c --
 *
 *   Copyright 2011, Kyan He <kyan.ql.he@gmail.com>
 *
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <errno.h>
#include <png.h>

#include "img_process.h"
#include "log.h"

int rgb565_to_rgb888(const char* src, char* dst, size_t pixel)
{
    struct rgb565  *from;
    struct rgb888  *to;

    from = (struct rgb565 *) src;
    to = (struct rgb888 *) dst;

    size_t i = 0;
    /* traverse pixel of the row */
    while(i++ < pixel) {

        to->r = from->r;
        to->g = from->g;
        to->b = from->b;
        /* scale */
        to->r <<= 3;
        to->g <<= 2;
        to->b <<= 3;

        to++;
        from++;
    }

    return 0;
}

int argb8888_to_rgb888(const char* src, char* dst, size_t pixel)
{
    size_t i;
    struct argb8888  *from;
    struct rgb888  *to;

    from = (struct argb8888 *) src;
    to = (struct rgb888 *) dst;

    i = 0;
    /* traverse pixel of the row */
    while(i++ < pixel) {

        to->r = from->r;
        to->g = from->g;
        to->b = from->b;

        to++;
        from++;
    }

    return 0;
}

int abgr8888_to_rgb888(const char* src, char* dst, size_t pixel)
{
    size_t i;
    struct abgr8888  *from;
    struct rgb888  *to;

    from = (struct abgr8888 *) src;
    to = (struct rgb888 *) dst;

    i = 0;
    /* traverse pixel of the row */
    while(i++ < pixel) {

        to->r = from->r;
        to->g = from->g;
        to->b = from->b;

        to++;
        from++;
    }

    return 0;
}

int bgra8888_to_rgb888(const char* src, char* dst, size_t pixel)
{
    size_t i;
    struct bgra8888  *from;
    struct rgb888  *to;

    from = (struct bgra8888 *) src;
    to = (struct rgb888 *) dst;

    i = 0;
    /* traverse pixel of the row */
    while(i++ < pixel) {

        to->r = from->r;
        to->g = from->g;
        to->b = from->b;

        to++;
        from++;
    }

    return 0;
}

int rgba8888_to_rgb888(const char* src, char* dst, size_t pixel)
{
    size_t i;
    struct rgba8888  *from;
    struct rgb888  *to;

    from = (struct rgba8888 *) src;
    to = (struct rgb888 *) dst;

    i = 0;
    /* traverse pixel of the row */
    while(i++ < pixel) {

        to->r = from->r;
        to->g = from->g;
        to->b = from->b;

        to++;
        from++;
    }

    return 0;
}

static void
stdio_write_func (png_structp png, png_bytep data, png_size_t size)
{
    FILE *fp;
    size_t ret;

    fp = png_get_io_ptr (png);
    while (size) {
        ret = fwrite (data, 1, size, fp);
        size -= ret;
        data += ret;
      if (size && ferror (fp))
         E("write: %m\n");
   }
}

static void
png_simple_output_flush_fn (__attribute__((unused)) png_structp png_ptr)
{
}

static void
png_simple_error_callback (__attribute__((unused)) png_structp png,
                       png_const_charp error_msg)
{
    E("png error: %s\n", error_msg);
}

static void
png_simple_warning_callback (__attribute__((unused)) png_structp png,
                         png_const_charp error_msg)
{
    fprintf(stderr, "png warning: %s\n", error_msg);
}

/* save rgb888 to png format in fp */
int save_png(const char* path, const char* data, int width, int height)
{
    FILE *fp;
    png_byte **volatile rows;
    png_struct *png;
    png_info *info;

    fp = fopen(path, "w");
    if (!fp) {
        int errsv = errno;
        E("Cannot open file %s for writing.\n", path);
        return errsv;
    }

    rows = malloc(height * sizeof rows[0]);
    if (!rows) goto oops;

    int i;
    for (i = 0; i < height; i++)
        rows[i] = (png_byte *) data + i * width * 3 /*fb.stride*/;

    png = png_create_write_struct (PNG_LIBPNG_VER_STRING, NULL,
                               png_simple_error_callback,
                               png_simple_warning_callback);
    if (!png) {
        E("png_create_write_struct failed\n");
        goto oops;
    }

    info = png_create_info_struct (png);
    if (!info) {
        E("png_create_info_struct failed\n");
        png_destroy_write_struct (&png, NULL);
        goto oops;
    }

    png_set_write_fn (png, fp, stdio_write_func, png_simple_output_flush_fn);
    png_set_IHDR (png, info,
            width,
            height,
#define DEPTH 8
            DEPTH,
        PNG_COLOR_TYPE_RGB,
        PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_DEFAULT,
        PNG_FILTER_TYPE_DEFAULT);

    png_color_16 white;

    white.gray = (1 << DEPTH) - 1;
    white.red = white.blue = white.green = white.gray;

    png_set_bKGD (png, info, &white);
    png_write_info (png, info);

    png_write_image (png, rows);
    png_write_end (png, info);

    png_destroy_write_struct (&png, &info);

    fclose(fp);
    free (rows);
    return 0;

oops:
    fclose(fp);
    free (rows);
    return -1;
}
