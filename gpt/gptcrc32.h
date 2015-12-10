/*
    libparted - a library for manipulating disk partitions
    Copyright (C) 1998-2000 Free Software Foundation, Inc.

    crc32.h

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

// For TWRP purposes, we'll be opting for version 3 of the GPL

#ifndef _GPTCRC32_H
#define _GPTCRC32_H

#include <stdint.h>

/*
 * This computes a 32 bit CRC of the data in the buffer, and returns the CRC.
 * The polynomial used is 0xedb88320.
 */

extern uint32_t gptcrc32 (const void *buf, unsigned long len, uint32_t seed);

#endif /* _GPTCRC32_H */
