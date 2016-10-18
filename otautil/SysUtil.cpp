/*
 * Copyright 2006 The Android Open Source Project
 *
 * System utilities.
 */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <android-base/logging.h>

#include "SysUtil.h"

static bool sysMapFD(int fd, MemMapping* pMap) {
    assert(pMap != NULL);

    struct stat sb;
    if (fstat(fd, &sb) == -1) {
        PLOG(ERROR) << "fstat(" << fd << ") failed";
        return false;
    }

    void* memPtr = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (memPtr == MAP_FAILED) {
        PLOG(ERROR) << "mmap(" << sb.st_size << ", R, PRIVATE, " << fd << ", 0) failed";
        return false;
    }

    pMap->addr = reinterpret_cast<unsigned char*>(memPtr);
    pMap->length = sb.st_size;
    pMap->range_count = 1;
    pMap->ranges = reinterpret_cast<MappedRange*>(malloc(sizeof(MappedRange)));
    if (pMap->ranges == NULL) {
        PLOG(ERROR) << "malloc failed";
        munmap(memPtr, sb.st_size);
        return false;
    }
    pMap->ranges[0].addr = memPtr;
    pMap->ranges[0].length = sb.st_size;

    return true;
}

static int sysMapBlockFile(FILE* mapf, MemMapping* pMap)
{
    char block_dev[PATH_MAX+1];
    size_t size;
    unsigned int blksize;
    size_t blocks;
    unsigned int range_count;
    unsigned int i;

    if (fgets(block_dev, sizeof(block_dev), mapf) == NULL) {
        PLOG(ERROR) << "failed to read block device from header";
        return -1;
    }
    for (i = 0; i < sizeof(block_dev); ++i) {
        if (block_dev[i] == '\n') {
            block_dev[i] = 0;
            break;
        }
    }

    if (fscanf(mapf, "%zu %u\n%u\n", &size, &blksize, &range_count) != 3) {
        LOG(ERROR) << "failed to parse block map header";
        return -1;
    }
    if (blksize != 0) {
        blocks = ((size-1) / blksize) + 1;
    }
    if (size == 0 || blksize == 0 || blocks > SIZE_MAX / blksize || range_count == 0) {
        LOG(ERROR) << "invalid data in block map file: size " << size << ", blksize " << blksize
                   << ", range_count " << range_count;
        return -1;
    }

    pMap->range_count = range_count;
    pMap->ranges = reinterpret_cast<MappedRange*>(calloc(range_count, sizeof(MappedRange)));
    if (pMap->ranges == NULL) {
        PLOG(ERROR) << "calloc(" << range_count << ", " << sizeof(MappedRange) << ") failed";
        return -1;
    }

    // Reserve enough contiguous address space for the whole file.
    unsigned char* reserve = reinterpret_cast<unsigned char*>(mmap64(NULL, blocks * blksize,
            PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0));
    if (reserve == MAP_FAILED) {
        PLOG(ERROR) << "failed to reserve address space";
        free(pMap->ranges);
        return -1;
    }

    int fd = open(block_dev, O_RDONLY);
    if (fd < 0) {
        PLOG(ERROR) << "failed to open block device " << block_dev;
        munmap(reserve, blocks * blksize);
        free(pMap->ranges);
        return -1;
    }

    unsigned char* next = reserve;
    size_t remaining_size = blocks * blksize;
    bool success = true;
    for (i = 0; i < range_count; ++i) {
        size_t start, end;
        if (fscanf(mapf, "%zu %zu\n", &start, &end) != 2) {
            LOG(ERROR) << "failed to parse range " << i << " in block map";
            success = false;
            break;
        }
        size_t length = (end - start) * blksize;
        if (end <= start || (end - start) > SIZE_MAX / blksize || length > remaining_size) {
          LOG(ERROR) << "unexpected range in block map: " << start << " " << end;
          success = false;
          break;
        }

        void* addr = mmap64(next, length, PROT_READ, MAP_PRIVATE | MAP_FIXED, fd, ((off64_t)start)*blksize);
        if (addr == MAP_FAILED) {
            PLOG(ERROR) << "failed to map block " << i;
            success = false;
            break;
        }
        pMap->ranges[i].addr = addr;
        pMap->ranges[i].length = length;

        next += length;
        remaining_size -= length;
    }
    if (success && remaining_size != 0) {
      LOG(ERROR) << "ranges in block map are invalid: remaining_size = " << remaining_size;
      success = false;
    }
    if (!success) {
      close(fd);
      munmap(reserve, blocks * blksize);
      free(pMap->ranges);
      return -1;
    }

    close(fd);
    pMap->addr = reserve;
    pMap->length = size;

    LOG(INFO) << "mmapped " << range_count << " ranges";

    return 0;
}

int sysMapFile(const char* fn, MemMapping* pMap)
{
    memset(pMap, 0, sizeof(*pMap));

    if (fn && fn[0] == '@') {
        // A map of blocks
        FILE* mapf = fopen(fn+1, "r");
        if (mapf == NULL) {
            PLOG(ERROR) << "Unable to open '" << (fn+1) << "'";
            return -1;
        }

        if (sysMapBlockFile(mapf, pMap) != 0) {
            LOG(ERROR) << "Map of '" << fn << "' failed";
            fclose(mapf);
            return -1;
        }

        fclose(mapf);
    } else {
        // This is a regular file.
        int fd = open(fn, O_RDONLY);
        if (fd == -1) {
            PLOG(ERROR) << "Unable to open '" << fn << "'";
            return -1;
        }

        if (!sysMapFD(fd, pMap)) {
            LOG(ERROR) << "Map of '" << fn << "' failed";
            close(fd);
            return -1;
        }

        close(fd);
    }
    return 0;
}

/*
 * Release a memory mapping.
 */
void sysReleaseMap(MemMapping* pMap)
{
    int i;
    for (i = 0; i < pMap->range_count; ++i) {
        if (munmap(pMap->ranges[i].addr, pMap->ranges[i].length) < 0) {
            PLOG(ERROR) << "munmap(" << pMap->ranges[i].addr << ", " << pMap->ranges[i].length
                        << ") failed";
        }
    }
    free(pMap->ranges);
    pMap->ranges = NULL;
    pMap->range_count = 0;
}
