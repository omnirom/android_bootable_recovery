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

#include "ota_io.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include <map>
#include <memory>

#include "config.h"

static std::map<intptr_t, const char*> filename_cache;
static std::string read_fault_file_name = "";
static std::string write_fault_file_name = "";
static std::string fsync_fault_file_name = "";

static bool get_hit_file(const char* cached_path, const std::string& ffn) {
    return should_hit_cache()
        ? !strncmp(cached_path, OTAIO_CACHE_FNAME, strlen(cached_path))
        : !strncmp(cached_path, ffn.c_str(), strlen(cached_path));
}

void ota_set_fault_files() {
    if (should_fault_inject(OTAIO_READ)) {
        read_fault_file_name = fault_fname(OTAIO_READ);
    }
    if (should_fault_inject(OTAIO_WRITE)) {
        write_fault_file_name = fault_fname(OTAIO_WRITE);
    }
    if (should_fault_inject(OTAIO_FSYNC)) {
        fsync_fault_file_name = fault_fname(OTAIO_FSYNC);
    }
}

bool have_eio_error = false;

int ota_open(const char* path, int oflags) {
    // Let the caller handle errors; we do not care if open succeeds or fails
    int fd = open(path, oflags);
    filename_cache[fd] = path;
    return fd;
}

int ota_open(const char* path, int oflags, mode_t mode) {
    int fd = open(path, oflags, mode);
    filename_cache[fd] = path;
    return fd; }

FILE* ota_fopen(const char* path, const char* mode) {
    FILE* fh = fopen(path, mode);
    filename_cache[(intptr_t)fh] = path;
    return fh;
}

static int __ota_close(int fd) {
    // descriptors can be reused, so make sure not to leave them in the cache
    filename_cache.erase(fd);
    return close(fd);
}

void OtaCloser::Close(int fd) {
    __ota_close(fd);
}

int ota_close(unique_fd& fd) {
    return __ota_close(fd.release());
}

static int __ota_fclose(FILE* fh) {
    filename_cache.erase(reinterpret_cast<intptr_t>(fh));
    return fclose(fh);
}

void OtaFcloser::operator()(FILE* f) const {
    __ota_fclose(f);
};

int ota_fclose(unique_file& fh) {
  return __ota_fclose(fh.release());
}

size_t ota_fread(void* ptr, size_t size, size_t nitems, FILE* stream) {
    if (should_fault_inject(OTAIO_READ)) {
        auto cached = filename_cache.find((intptr_t)stream);
        const char* cached_path = cached->second;
        if (cached != filename_cache.end() &&
                get_hit_file(cached_path, read_fault_file_name)) {
            read_fault_file_name = "";
            errno = EIO;
            have_eio_error = true;
            return 0;
        }
    }
    size_t status = fread(ptr, size, nitems, stream);
    // If I/O error occurs, set the retry-update flag.
    if (status != nitems && errno == EIO) {
        have_eio_error = true;
    }
    return status;
}

ssize_t ota_read(int fd, void* buf, size_t nbyte) {
    if (should_fault_inject(OTAIO_READ)) {
        auto cached = filename_cache.find(fd);
        const char* cached_path = cached->second;
        if (cached != filename_cache.end()
                && get_hit_file(cached_path, read_fault_file_name)) {
            read_fault_file_name = "";
            errno = EIO;
            have_eio_error = true;
            return -1;
        }
    }
    ssize_t status = read(fd, buf, nbyte);
    if (status == -1 && errno == EIO) {
        have_eio_error = true;
    }
    return status;
}

size_t ota_fwrite(const void* ptr, size_t size, size_t count, FILE* stream) {
    if (should_fault_inject(OTAIO_WRITE)) {
        auto cached = filename_cache.find((intptr_t)stream);
        const char* cached_path = cached->second;
        if (cached != filename_cache.end() &&
                get_hit_file(cached_path, write_fault_file_name)) {
            write_fault_file_name = "";
            errno = EIO;
            have_eio_error = true;
            return 0;
        }
    }
    size_t status = fwrite(ptr, size, count, stream);
    if (status != count && errno == EIO) {
        have_eio_error = true;
    }
    return status;
}

ssize_t ota_write(int fd, const void* buf, size_t nbyte) {
    if (should_fault_inject(OTAIO_WRITE)) {
        auto cached = filename_cache.find(fd);
        const char* cached_path = cached->second;
        if (cached != filename_cache.end() &&
                get_hit_file(cached_path, write_fault_file_name)) {
            write_fault_file_name = "";
            errno = EIO;
            have_eio_error = true;
            return -1;
        }
    }
    ssize_t status = write(fd, buf, nbyte);
    if (status == -1 && errno == EIO) {
        have_eio_error = true;
    }
    return status;
}

int ota_fsync(int fd) {
    if (should_fault_inject(OTAIO_FSYNC)) {
        auto cached = filename_cache.find(fd);
        const char* cached_path = cached->second;
        if (cached != filename_cache.end() &&
                get_hit_file(cached_path, fsync_fault_file_name)) {
            fsync_fault_file_name = "";
            errno = EIO;
            have_eio_error = true;
            return -1;
        }
    }
    int status = fsync(fd);
    if (status == -1 && errno == EIO) {
        have_eio_error = true;
    }
    return status;
}

