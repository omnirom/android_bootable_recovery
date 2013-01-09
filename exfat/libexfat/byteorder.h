/*
	byteorder.h (12.01.10)
	Endianness stuff. exFAT uses little-endian byte order.

	Copyright (C) 2010-2012  Andrew Nayenko

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef BYTEORDER_H_INCLUDED
#define BYTEORDER_H_INCLUDED

#define __GLIBC__
#include <stdint.h>

#if defined(__GLIBC__)

#include <endian.h>
#include <byteswap.h>

#elif defined(__APPLE__)

#include <machine/endian.h>
#include <libkern/OSByteOrder.h>
#define bswap_16(x) OSSwapInt16(x)
#define bswap_32(x) OSSwapInt32(x)
#define bswap_64(x) OSSwapInt64(x)
#define __BYTE_ORDER BYTE_ORDER
#define __LITTLE_ENDIAN LITTLE_ENDIAN
#define __BIG_ENDIAN BIG_ENDIAN

#elif defined(__FreeBSD__) || defined(__DragonFlyBSD__) || defined(__NetBSD__)

#include <sys/endian.h>
#define bswap_16(x) bswap16(x)
#define bswap_32(x) bswap32(x)
#define bswap_64(x) bswap64(x)
#define __BYTE_ORDER _BYTE_ORDER
#define __LITTLE_ENDIAN _LITTLE_ENDIAN
#define __BIG_ENDIAN _BIG_ENDIAN

#elif defined(__OpenBSD__)

#include <machine/endian.h>
#define bswap_16(x) swap16(x)
#define bswap_32(x) swap32(x)
#define bswap_64(x) swap64(x)
#define __BYTE_ORDER _BYTE_ORDER
#define __LITTLE_ENDIAN _LITTLE_ENDIAN
#define __BIG_ENDIAN _BIG_ENDIAN

#elif defined(__sun)

#include <sys/byteorder.h>
#define bswap_16(x) BSWAP_16(x)
#define bswap_32(x) BSWAP_32(x)
#define bswap_64(x) BSWAP_64(x)
#define __LITTLE_ENDIAN 1234
#define __BIG_ENDIAN 4321
#ifdef _LITTLE_ENDIAN
#define __BYTE_ORDER __LITTLE_ENDIAN
#else
#define __BYTE_ORDER __BIG_ENDIAN
#endif

#else 
#error No byte order macros available for your platform
#endif

#define __BYTE_ORDER __LITTLE_ENDIAN
typedef struct { uint16_t __u16; } le16_t;
typedef struct { uint32_t __u32; } le32_t;
typedef struct { uint64_t __u64; } le64_t;

#if __BYTE_ORDER == __LITTLE_ENDIAN
static inline uint16_t le16_to_cpu(le16_t v) { return v.__u16; }
static inline uint32_t le32_to_cpu(le32_t v) { return v.__u32; }
static inline uint64_t le64_to_cpu(le64_t v) { return v.__u64; }

static inline le16_t cpu_to_le16(uint16_t v) { le16_t t = {v}; return t; }
static inline le32_t cpu_to_le32(uint32_t v) { le32_t t = {v}; return t; }
static inline le64_t cpu_to_le64(uint64_t v) { le64_t t = {v}; return t; }
#elif __BYTE_ORDER == __BIG_ENDIAN
static inline uint16_t le16_to_cpu(le16_t v) { return bswap_16(v.__u16); }
static inline uint32_t le32_to_cpu(le32_t v) { return bswap_32(v.__u32); }
static inline uint64_t le64_to_cpu(le64_t v) { return bswap_64(v.__u64); }

static inline le16_t cpu_to_le16(uint16_t v)
	{ le16_t t = {bswap_16(v)}; return t; }
static inline le32_t cpu_to_le32(uint32_t v)
	{ le32_t t = {bswap_32(v)}; return t; }
static inline le64_t cpu_to_le64(uint64_t v)
	{ le64_t t = {bswap_64(v)}; return t; }
#else
#error Wow! You have a PDP machine?!
#endif

#endif /* ifndef BYTEORDER_H_INCLUDED */
