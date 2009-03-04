/*
 * Copyright 2006 The Android Open Source Project
 *
 * Some handy functions for manipulating bits and bytes.
 */
#ifndef _MINZIP_BITS
#define _MINZIP_BITS

#include "inline_magic.h"

#include <stdlib.h>
#include <string.h>

/*
 * Get 1 byte.  (Included to make the code more legible.)
 */
INLINE unsigned char get1(unsigned const char* pSrc)
{
    return *pSrc;
}

/*
 * Get 2 big-endian bytes.
 */
INLINE unsigned short get2BE(unsigned char const* pSrc)
{
    unsigned short result;

    result = *pSrc++ << 8;
    result |= *pSrc++;

    return result;
}

/*
 * Get 4 big-endian bytes.
 */
INLINE unsigned int get4BE(unsigned char const* pSrc)
{
    unsigned int result;

    result = *pSrc++ << 24;
    result |= *pSrc++ << 16;
    result |= *pSrc++ << 8;
    result |= *pSrc++;

    return result;
}

/*
 * Get 8 big-endian bytes.
 */
INLINE unsigned long long get8BE(unsigned char const* pSrc)
{
    unsigned long long result;

    result = (unsigned long long) *pSrc++ << 56;
    result |= (unsigned long long) *pSrc++ << 48;
    result |= (unsigned long long) *pSrc++ << 40;
    result |= (unsigned long long) *pSrc++ << 32;
    result |= (unsigned long long) *pSrc++ << 24;
    result |= (unsigned long long) *pSrc++ << 16;
    result |= (unsigned long long) *pSrc++ << 8;
    result |= (unsigned long long) *pSrc++;

    return result;
}

/*
 * Get 2 little-endian bytes.
 */
INLINE unsigned short get2LE(unsigned char const* pSrc)
{
    unsigned short result;

    result = *pSrc++;
    result |= *pSrc++ << 8;

    return result;
}

/*
 * Get 4 little-endian bytes.
 */
INLINE unsigned int get4LE(unsigned char const* pSrc)
{
    unsigned int result;

    result = *pSrc++;
    result |= *pSrc++ << 8;
    result |= *pSrc++ << 16;
    result |= *pSrc++ << 24;

    return result;
}

/*
 * Get 8 little-endian bytes.
 */
INLINE unsigned long long get8LE(unsigned char const* pSrc)
{
    unsigned long long result;

    result = (unsigned long long) *pSrc++;
    result |= (unsigned long long) *pSrc++ << 8;
    result |= (unsigned long long) *pSrc++ << 16;
    result |= (unsigned long long) *pSrc++ << 24;
    result |= (unsigned long long) *pSrc++ << 32;
    result |= (unsigned long long) *pSrc++ << 40;
    result |= (unsigned long long) *pSrc++ << 48;
    result |= (unsigned long long) *pSrc++ << 56;

    return result;
}

/*
 * Grab 1 byte and advance the data pointer.
 */
INLINE unsigned char read1(unsigned const char** ppSrc)
{
    return *(*ppSrc)++;
}

/*
 * Grab 2 big-endian bytes and advance the data pointer.
 */
INLINE unsigned short read2BE(unsigned char const** ppSrc)
{
    unsigned short result;

    result = *(*ppSrc)++ << 8;
    result |= *(*ppSrc)++;

    return result;
}

/*
 * Grab 4 big-endian bytes and advance the data pointer.
 */
INLINE unsigned int read4BE(unsigned char const** ppSrc)
{
    unsigned int result;

    result = *(*ppSrc)++ << 24;
    result |= *(*ppSrc)++ << 16;
    result |= *(*ppSrc)++ << 8;
    result |= *(*ppSrc)++;

    return result;
}

/*
 * Get 8 big-endian bytes.
 */
INLINE unsigned long long read8BE(unsigned char const** ppSrc)
{
    unsigned long long result;

    result = (unsigned long long) *(*ppSrc)++ << 56;
    result |= (unsigned long long) *(*ppSrc)++ << 48;
    result |= (unsigned long long) *(*ppSrc)++ << 40;
    result |= (unsigned long long) *(*ppSrc)++ << 32;
    result |= (unsigned long long) *(*ppSrc)++ << 24;
    result |= (unsigned long long) *(*ppSrc)++ << 16;
    result |= (unsigned long long) *(*ppSrc)++ << 8;
    result |= (unsigned long long) *(*ppSrc)++;

    return result;
}

/*
 * Grab 2 little-endian bytes and advance the data pointer.
 */
INLINE unsigned short read2LE(unsigned char const** ppSrc)
{
    unsigned short result;

    result = *(*ppSrc)++;
    result |= *(*ppSrc)++ << 8;

    return result;
}

/*
 * Grab 4 little-endian bytes and advance the data pointer.
 */
INLINE unsigned int read4LE(unsigned char const** ppSrc)
{
    unsigned int result;

    result = *(*ppSrc)++;
    result |= *(*ppSrc)++ << 8;
    result |= *(*ppSrc)++ << 16;
    result |= *(*ppSrc)++ << 24;

    return result;
}

/*
 * Get 8 little-endian bytes.
 */
INLINE unsigned long long read8LE(unsigned char const** ppSrc)
{
    unsigned long long result;

    result = (unsigned long long) *(*ppSrc)++;
    result |= (unsigned long long) *(*ppSrc)++ << 8;
    result |= (unsigned long long) *(*ppSrc)++ << 16;
    result |= (unsigned long long) *(*ppSrc)++ << 24;
    result |= (unsigned long long) *(*ppSrc)++ << 32;
    result |= (unsigned long long) *(*ppSrc)++ << 40;
    result |= (unsigned long long) *(*ppSrc)++ << 48;
    result |= (unsigned long long) *(*ppSrc)++ << 56;

    return result;
}

/*
 * Skip over a UTF-8 string.
 */
INLINE void skipUtf8String(unsigned char const** ppSrc)
{
    unsigned int length = read4BE(ppSrc);

    (*ppSrc) += length;
}

/*
 * Read a UTF-8 string into a fixed-size buffer, and null-terminate it.
 *
 * Returns the length of the original string.
 */
INLINE int readUtf8String(unsigned char const** ppSrc, char* buf, size_t bufLen)
{
    unsigned int length = read4BE(ppSrc);
    size_t copyLen = (length < bufLen) ? length : bufLen-1;

    memcpy(buf, *ppSrc, copyLen);
    buf[copyLen] = '\0';

    (*ppSrc) += length;
    return length;
}

/*
 * Read a UTF-8 string into newly-allocated storage, and null-terminate it.
 *
 * Returns the string and its length.  (The latter is probably unnecessary
 * for the way we're using UTF8.)
 */
INLINE char* readNewUtf8String(unsigned char const** ppSrc, size_t* pLength)
{
    unsigned int length = read4BE(ppSrc);
    char* buf;

    buf = (char*) malloc(length+1);

    memcpy(buf, *ppSrc, length);
    buf[length] = '\0';

    (*ppSrc) += length;

    *pLength = length;
    return buf;
}


/*
 * Set 1 byte.  (Included to make the code more legible.)
 */
INLINE void set1(unsigned char* buf, unsigned char val)
{
    *buf = (unsigned char)(val);
}

/*
 * Set 2 big-endian bytes.
 */
INLINE void set2BE(unsigned char* buf, unsigned short val)
{
    *buf++ = (unsigned char)(val >> 8);
    *buf = (unsigned char)(val);
}

/*
 * Set 4 big-endian bytes.
 */
INLINE void set4BE(unsigned char* buf, unsigned int val)
{
    *buf++ = (unsigned char)(val >> 24);
    *buf++ = (unsigned char)(val >> 16);
    *buf++ = (unsigned char)(val >> 8);
    *buf = (unsigned char)(val);
}

/*
 * Set 8 big-endian bytes.
 */
INLINE void set8BE(unsigned char* buf, unsigned long long val)
{
    *buf++ = (unsigned char)(val >> 56);
    *buf++ = (unsigned char)(val >> 48);
    *buf++ = (unsigned char)(val >> 40);
    *buf++ = (unsigned char)(val >> 32);
    *buf++ = (unsigned char)(val >> 24);
    *buf++ = (unsigned char)(val >> 16);
    *buf++ = (unsigned char)(val >> 8);
    *buf = (unsigned char)(val);
}

/*
 * Set 2 little-endian bytes.
 */
INLINE void set2LE(unsigned char* buf, unsigned short val)
{
    *buf++ = (unsigned char)(val);
    *buf = (unsigned char)(val >> 8);
}

/*
 * Set 4 little-endian bytes.
 */
INLINE void set4LE(unsigned char* buf, unsigned int val)
{
    *buf++ = (unsigned char)(val);
    *buf++ = (unsigned char)(val >> 8);
    *buf++ = (unsigned char)(val >> 16);
    *buf = (unsigned char)(val >> 24);
}

/*
 * Set 8 little-endian bytes.
 */
INLINE void set8LE(unsigned char* buf, unsigned long long val)
{
    *buf++ = (unsigned char)(val);
    *buf++ = (unsigned char)(val >> 8);
    *buf++ = (unsigned char)(val >> 16);
    *buf++ = (unsigned char)(val >> 24);
    *buf++ = (unsigned char)(val >> 32);
    *buf++ = (unsigned char)(val >> 40);
    *buf++ = (unsigned char)(val >> 48);
    *buf = (unsigned char)(val >> 56);
}

/*
 * Stuff a UTF-8 string into the buffer.
 */
INLINE void setUtf8String(unsigned char* buf, const unsigned char* str)
{
    unsigned int strLen = strlen((const char*)str);

    set4BE(buf, strLen);
    memcpy(buf + sizeof(unsigned int), str, strLen);
}

#endif /*_MINZIP_BITS*/
