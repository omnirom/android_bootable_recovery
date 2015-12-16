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

/*
 * Provide a series of proxy functions for basic file accessors.
 * The behavior of these functions can be changed to return different
 * errors under a variety of conditions.
 */

#ifndef _UPDATER_OTA_IO_H_
#define _UPDATER_OTA_IO_H_

#include <stdio.h>
#include <sys/stat.h>

#define OTAIO_CACHE_FNAME "/cache/saved.file"

void ota_set_fault_files();

int ota_open(const char* path, int oflags);

int ota_open(const char* path, int oflags, mode_t mode);

FILE* ota_fopen(const char* filename, const char* mode);

int ota_close(int fd);

int ota_fclose(FILE* fh);

size_t ota_fread(void* ptr, size_t size, size_t nitems, FILE* stream);

ssize_t ota_read(int fd, void* buf, size_t nbyte);

size_t ota_fwrite(const void* ptr, size_t size, size_t count, FILE* stream);

ssize_t ota_write(int fd, const void* buf, size_t nbyte);

int ota_fsync(int fd);

#endif
