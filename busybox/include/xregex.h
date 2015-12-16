/* vi: set sw=4 ts=4: */
/*
 * Busybox xregcomp utility routine.  This isn't in libbb.h because the
 * C library we're linking against may not support regex.h.
 *
 * Based in part on code from sash, Copyright (c) 1999 by David I. Bell
 * Permission has been granted to redistribute this code under GPL.
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#ifndef BB_REGEX_H
#define BB_REGEX_H 1

#if defined(ANDROID) && !defined(RECOVERY_VERSION)

#include <bb_regex.h>
#define regcomp bb_regcomp
#define re_compile_pattern bb_re_compile_pattern
#define re_search bb_re_search
#define regexec bb_regexec
#define regfree bb_regfree
#define regerror bb_regerror

#else
#include <regex.h>
#endif

PUSH_AND_SET_FUNCTION_VISIBILITY_TO_HIDDEN

char* regcomp_or_errmsg(regex_t *preg, const char *regex, int cflags) FAST_FUNC;
void xregcomp(regex_t *preg, const char *regex, int cflags) FAST_FUNC;

POP_SAVED_FUNCTION_VISIBILITY

#endif
