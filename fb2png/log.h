/**
 * fb2png  Save screenshot into .png.
 *
 * Copyright (C) 2012  Kyan <kyan.ql.he@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#ifndef __KYAN_LOG_H__
#define __KYAN_LOG_H__

#include <errno.h>
#include <string.h>

#ifdef ANDROID_XXX

#ifndef LOG_TAG
#define LOG_TAG "tag"
#endif

#include <android/log.h>

#define D LOGD
#define E LOGE

#define LOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG  , LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO   , LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN   , LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR  , LOG_TAG, __VA_ARGS__)

#else /* ANDROID */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#if DEBUG == 1

#define LOG_FUNCTION_NAME \
    fprintf(stderr, "\033[0;1;31m__func__: %s\033[0;0m\n", __FUNCTION__);

#else

#define LOG_FUNCTION_NAME

#endif

__attribute__((unused)) static void
D(const char *msg, ...)
{
    va_list ap;

    va_start (ap, msg);
    vfprintf(stdout, msg, ap);
    fprintf(stdout, "\n");
    va_end (ap);
    fflush(stdout);
}

__attribute__((unused)) static void
E(const char *msg, ...)
{
    va_list ap;

    va_start (ap, msg);
    vfprintf(stderr, msg, ap);
    fprintf(stderr, ", %s", strerror(errno));
    fprintf(stderr, "\n");
    va_end (ap);
}

#endif /* ANDROID */

#endif
