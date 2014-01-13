/*
 * Copyright 2006 The Android Open Source Project
 *
 * System utilities.
 */
#ifndef _MINZIP_SYSUTIL
#define _MINZIP_SYSUTIL

#include "inline_magic.h"

#include <sys/types.h>

/*
 * Use this to keep track of mapped segments.
 */
typedef struct MemMapping {
    void*   addr;           /* start of data */
    size_t  length;         /* length of data */

    void*   baseAddr;       /* page-aligned base address */
    size_t  baseLength;     /* length of mapping */
} MemMapping;

/* copy a map */
INLINE void sysCopyMap(MemMapping* dst, const MemMapping* src) {
    *dst = *src;
}

/*
 * Map a file (from fd's current offset) into a shared,
 * read-only memory segment.
 *
 * On success, "pMap" is filled in, and zero is returned.
 */
int sysMapFileInShmem(int fd, MemMapping* pMap);

/*
 * Release the pages associated with a shared memory segment.
 *
 * This does not free "pMap"; it just releases the memory.
 */
void sysReleaseShmem(MemMapping* pMap);

#endif /*_MINZIP_SYSUTIL*/
