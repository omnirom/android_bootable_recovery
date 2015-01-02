/* Align/Truncate a string in a given screen width
   Copyright (C) 2009-2010 Free Software Foundation, Inc.
   Copyright (C) 2010-2013 Karel Zak <kzak@redhat.com>

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as published by
   the Free Software Foundation, either version 2.1 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */
#ifndef UTIL_LINUX_MBSALIGN_H
# define UTIL_LINUX_MBSALIGN_H
# include <stddef.h>

typedef enum { MBS_ALIGN_LEFT, MBS_ALIGN_RIGHT, MBS_ALIGN_CENTER } mbs_align_t;

enum {
  /* Use unibyte mode for invalid multibyte strings or
     or when heap memory is exhausted.  */
  MBA_UNIBYTE_FALLBACK = 0x0001,

#if 0 /* Other possible options.  */
  /* Skip invalid multibyte chars rather than failing  */
  MBA_IGNORE_INVALID   = 0x0002,

  /* Align multibyte strings using "figure space" (\u2007)  */
  MBA_USE_FIGURE_SPACE = 0x0004,

  /* Don't add any padding  */
  MBA_TRUNCATE_ONLY    = 0x0008,

  /* Don't truncate  */
  MBA_PAD_ONLY         = 0x0010,
#endif
};

extern size_t mbs_truncate(char *str, size_t *width);

extern size_t mbsalign (const char *src, char *dest,
			size_t dest_size,  size_t *width,
			mbs_align_t align, int flags);

extern size_t mbs_safe_nwidth(const char *buf, size_t bufsz, size_t *sz);
extern size_t mbs_safe_width(const char *s);

extern char *mbs_safe_encode(const char *s, size_t *width);
extern char *mbs_safe_encode_to_buffer(const char *s, size_t *width, char *buf);
extern size_t mbs_safe_encode_size(size_t bytes);

#endif /* UTIL_LINUX_MBSALIGN_H */
