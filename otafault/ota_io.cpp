/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#if defined (TARGET_INJECT_FAULTS)
#include <map>
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ota_io.h"

#if defined (TARGET_INJECT_FAULTS)
static std::map<int, const char*> FilenameCache;
static std::string FaultFileName =
#if defined (TARGET_READ_FAULT)
        TARGET_READ_FAULT;
#elif defined (TARGET_WRITE_FAULT)
        TARGET_WRITE_FAULT;
#elif defined (TARGET_FSYNC_FAULT)
        TARGET_FSYNC_FAULT;
#endif // defined (TARGET_READ_FAULT)
#endif // defined (TARGET_INJECT_FAULTS)

bool have_eio_error = false;

int ota_open(const char* path, int oflags) {
#if defined (TARGET_INJECT_FAULTS)
    // Let the caller handle errors; we do not care if open succeeds or fails
    int fd = open(path, oflags);
    FilenameCache[fd] = path;
    return fd;
#else
    return open(path, oflags);
#endif
}

int ota_open(const char* path, int oflags, mode_t mode) {
#if defined (TARGET_INJECT_FAULTS)
    int fd = open(path, oflags, mode);
    FilenameCache[fd] = path;
    return fd;
#else
    return open(path, oflags, mode);
#endif
}

FILE* ota_fopen(const char* path, const char* mode) {
#if defined (TARGET_INJECT_FAULTS)
    FILE* fh = fopen(path, mode);
    FilenameCache[(intptr_t)fh] = path;
    return fh;
#else
    return fopen(path, mode);
#endif
}

int ota_close(int fd) {
#if defined (TARGET_INJECT_FAULTS)
    // descriptors can be reused, so make sure not to leave them in the cahce
    FilenameCache.erase(fd);
#endif
    return close(fd);
}

int ota_fclose(FILE* fh) {
#if defined (TARGET_INJECT_FAULTS)
    FilenameCache.erase((intptr_t)fh);
#endif
    return fclose(fh);
}

size_t ota_fread(void* ptr, size_t size, size_t nitems, FILE* stream) {
#if defined (TARGET_READ_FAULT)
    if (FilenameCache.find((intptr_t)stream) != FilenameCache.end()
            && FilenameCache[(intptr_t)stream] == FaultFileName) {
        FaultFileName = "";
        errno = EIO;
        have_eio_error = true;
        return 0;
    } else {
        size_t status = fread(ptr, size, nitems, stream);
        // If I/O error occurs, set the retry-update flag.
        if (status != nitems && errno == EIO) {
            have_eio_error = true;
        }
        return status;
    }
#else
    size_t status = fread(ptr, size, nitems, stream);
    // If I/O error occurs, set the retry-update flag.
    if (status != nitems && errno == EIO) {
        have_eio_error = true;
    }
    return status;
#endif
}

ssize_t ota_read(int fd, void* buf, size_t nbyte) {
#if defined (TARGET_READ_FAULT)
    if (FilenameCache.find(fd) != FilenameCache.end()
            && FilenameCache[fd] == FaultFileName) {
        FaultFileName = "";
        errno = EIO;
        have_eio_error = true;
        return -1;
    } else {
        ssize_t status = read(fd, buf, nbyte);
        if (status == -1 && errno == EIO) {
            have_eio_error = true;
        }
        return status;
    }
#else
    ssize_t status = read(fd, buf, nbyte);
    if (status == -1 && errno == EIO) {
        have_eio_error = true;
    }
    return status;
#endif
}

size_t ota_fwrite(const void* ptr, size_t size, size_t count, FILE* stream) {
#if defined (TARGET_WRITE_FAULT)
    if (FilenameCache.find((intptr_t)stream) != FilenameCache.end()
            && FilenameCache[(intptr_t)stream] == FaultFileName) {
        FaultFileName = "";
        errno = EIO;
        have_eio_error = true;
        return 0;
    } else {
        size_t status = fwrite(ptr, size, count, stream);
        if (status != count && errno == EIO) {
            have_eio_error = true;
        }
        return status;
    }
#else
    size_t status = fwrite(ptr, size, count, stream);
    if (status != count && errno == EIO) {
        have_eio_error = true;
    }
    return status;
#endif
}

ssize_t ota_write(int fd, const void* buf, size_t nbyte) {
#if defined (TARGET_WRITE_FAULT)
    if (FilenameCache.find(fd) != FilenameCache.end()
            && FilenameCache[fd] == FaultFileName) {
        FaultFileName = "";
        errno = EIO;
        have_eio_error = true;
        return -1;
    } else {
        ssize_t status = write(fd, buf, nbyte);
        if (status == -1 && errno == EIO) {
            have_eio_error = true;
        }
        return status;
    }
#else
    ssize_t status = write(fd, buf, nbyte);
    if (status == -1 && errno == EIO) {
        have_eio_error = true;
    }
    return status;
#endif
}

int ota_fsync(int fd) {
#if defined (TARGET_FSYNC_FAULT)
    if (FilenameCache.find(fd) != FilenameCache.end()
            && FilenameCache[fd] == FaultFileName) {
        FaultFileName = "";
        errno = EIO;
        have_eio_error = true;
        return -1;
    } else {
        int status = fsync(fd);
        if (status == -1 && errno == EIO) {
            have_eio_error = true;
        }
        return status;
    }
#else
    int status = fsync(fd);
    if (status == -1 && errno == EIO) {
        have_eio_error = true;
    }
    return status;
#endif
}
