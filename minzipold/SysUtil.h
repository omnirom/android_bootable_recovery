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
 * Load a file into a new shared memory segment.  All data from the current
 * offset to the end of the file is pulled in.
 *
 * The segment is read-write, allowing VM fixups.  (It should be modified
 * to support .gz/.zip compressed data.)
 *
 * On success, "pMap" is filled in, and zero is returned.
 */
int sysLoadFileInShmem(int fd, MemMapping* pMap);

/*
 * Map a file (from fd's current offset) into a shared,
 * read-only memory segment.
 *
 * On success, "pMap" is filled in, and zero is returned.
 */
int sysMapFileInShmem(int fd, MemMapping* pMap);

/*
 * Like sysMapFileInShmem, but on only part of a file.
 */
int sysMapFileSegmentInShmem(int fd, off_t start, long length,
    MemMapping* pMap);

/*
 * Release the pages associated with a shared memory segment.
 *
 * This does not free "pMap"; it just releases the memory.
 */
void sysReleaseShmem(MemMapping* pMap);

#endif /*_MINZIP_SYSUTIL*/
