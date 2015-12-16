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

#include <map>
#include <string>

#include <stdio.h>
#include <unistd.h>

#include "minzip/Zip.h"
#include "config.h"
#include "ota_io.h"

#define OTAIO_MAX_FNAME_SIZE 128

static ZipArchive* archive;
static std::map<const char*, bool> should_inject_cache;

static const char* get_type_path(const char* io_type) {
    char* path = (char*)calloc(strlen(io_type) + strlen(OTAIO_BASE_DIR) + 2, sizeof(char));
    sprintf(path, "%s/%s", OTAIO_BASE_DIR, io_type);
    return path;
}

void ota_io_init(ZipArchive* za) {
    archive = za;
    ota_set_fault_files();
}

bool should_fault_inject(const char* io_type) {
    if (should_inject_cache.find(io_type) != should_inject_cache.end()) {
        return should_inject_cache[io_type];
    }
    const char* type_path = get_type_path(io_type);
    const ZipEntry* entry = mzFindZipEntry(archive, type_path);
    should_inject_cache[type_path] = entry != nullptr;
    free((void*)type_path);
    return entry != NULL;
}

bool should_hit_cache() {
    return should_fault_inject(OTAIO_CACHE);
}

std::string fault_fname(const char* io_type) {
    const char* type_path = get_type_path(io_type);
    char* fname = (char*) calloc(OTAIO_MAX_FNAME_SIZE, sizeof(char));
    const ZipEntry* entry = mzFindZipEntry(archive, type_path);
    mzReadZipEntry(archive, entry, fname, OTAIO_MAX_FNAME_SIZE);
    free((void*)type_path);
    return std::string(fname);
}
