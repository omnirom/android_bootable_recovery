/*
 * Copyright 2006 The Android Open Source Project
 *
 * System utilities.
 */
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <limits.h>
#include <errno.h>
#include <assert.h>

#define LOG_TAG "minzip"
#include "Log.h"
#include "SysUtil.h"

static int getFileStartAndLength(int fd, off_t *start_, size_t *length_)
{
    off_t start, end;
    size_t length;

    assert(start_ != NULL);
    assert(length_ != NULL);

    start = lseek(fd, 0L, SEEK_CUR);
    end = lseek(fd, 0L, SEEK_END);
    (void) lseek(fd, start, SEEK_SET);

    if (start == (off_t) -1 || end == (off_t) -1) {
        LOGE("could not determine length of file\n");
        return -1;
    }

    length = end - start;
    if (length == 0) {
        LOGE("file is empty\n");
        return -1;
    }

    *start_ = start;
    *length_ = length;

    return 0;
}

/*
 * Map a file (from fd's current offset) into a shared, read-only memory
 * segment.  The file offset must be a multiple of the page size.
 *
 * On success, returns 0 and fills out "pMap".  On failure, returns a nonzero
 * value and does not disturb "pMap".
 */
int sysMapFileInShmem(int fd, MemMapping* pMap)
{
    off_t start;
    size_t length;
    void* memPtr;

    assert(pMap != NULL);

    if (getFileStartAndLength(fd, &start, &length) < 0)
        return -1;

    memPtr = mmap(NULL, length, PROT_READ, MAP_FILE | MAP_SHARED, fd, start);
    if (memPtr == MAP_FAILED) {
        LOGW("mmap(%d, R, FILE|SHARED, %d, %d) failed: %s\n", (int) length,
            fd, (int) start, strerror(errno));
        return -1;
    }

    pMap->baseAddr = pMap->addr = memPtr;
    pMap->baseLength = pMap->length = length;

    return 0;
}

/*
 * Release a memory mapping.
 */
void sysReleaseShmem(MemMapping* pMap)
{
    if (pMap->baseAddr == NULL && pMap->baseLength == 0)
        return;

    if (munmap(pMap->baseAddr, pMap->baseLength) < 0) {
        LOGW("munmap(%p, %d) failed: %s\n",
            pMap->baseAddr, (int)pMap->baseLength, strerror(errno));
    } else {
        LOGV("munmap(%p, %d) succeeded\n", pMap->baseAddr, pMap->baseLength);
        pMap->baseAddr = NULL;
        pMap->baseLength = 0;
    }
}
