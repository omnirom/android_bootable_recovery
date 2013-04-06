/*
 *   -- http://android-fb2png.googlecode.com/svn/trunk/img_process.h --
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

#ifndef __IMG_PROCESS_H__
#define __IMG_PROCESS_H__

/**
 * rgba8888 is found on Desire HD Linux localhost 2.6.35.10-g931a37e #1
 *      PREEMPT Wed Nov 9 14:04:03 CST 2011 armv7l GNU/Linux
 */


typedef struct rgb888 {
        char r;
        char g;
        char b;
} rgb888_t;

typedef rgb888_t rgb24_t;

typedef struct argb8888 {
        char a;
        char r;
        char g;
        char b;
} argb8888_t;

typedef struct abgr8888 {
        char a;
        char b;
        char g;
        char r;
} abgr8888_t;

typedef struct bgra8888 {
        char b;
        char g;
        char r;
        char a;
} bgra8888_t;

typedef struct rgba8888 {
        char r;
        char g;
        char b;
        char a;
} rgba8888_t;

typedef struct rgb565 {
        short b:5;
        short g:6;
        short r:5;
} rgb565_t;

int rgb565_to_rgb888(const char* src, char* dst, size_t pixel);

int argb8888_to_rgb888(const char* src, char* dst, size_t pixel);

int abgr8888_to_rgb888(const char* src, char* dst, size_t pixel);

int bgra8888_to_rgb888(const char* src, char* dst, size_t pixel);

int rgba8888_to_rgb888(const char* src, char* dst, size_t pixel);

int save_png(const char* path, const char* data, int width, int height);

#endif
