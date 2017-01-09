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

#include <android-base/stringprintf.h>
#include <ziparchive/zip_archive.h>

#include "config.h"
#include "ota_io.h"

#define OTAIO_MAX_FNAME_SIZE 128

static ZipArchiveHandle archive;
static bool is_retry = false;
static std::map<std::string, bool> should_inject_cache;

static std::string get_type_path(const char* io_type) {
    return android::base::StringPrintf("%s/%s", OTAIO_BASE_DIR, io_type);
}

void ota_io_init(ZipArchiveHandle za, bool retry) {
    archive = za;
    is_retry = retry;
    ota_set_fault_files();
}

bool should_fault_inject(const char* io_type) {
    // archive will be NULL if we used an entry point other
    // than updater/updater.cpp:main
    if (archive == nullptr || is_retry) {
        return false;
    }
    const std::string type_path = get_type_path(io_type);
    if (should_inject_cache.find(type_path) != should_inject_cache.end()) {
        return should_inject_cache[type_path];
    }
    ZipString zip_type_path(type_path.c_str());
    ZipEntry entry;
    int status = FindEntry(archive, zip_type_path, &entry);
    should_inject_cache[type_path] = (status == 0);
    return (status == 0);
}

bool should_hit_cache() {
    return should_fault_inject(OTAIO_CACHE);
}

std::string fault_fname(const char* io_type) {
    std::string type_path = get_type_path(io_type);
    std::string fname;
    fname.resize(OTAIO_MAX_FNAME_SIZE);
    ZipString zip_type_path(type_path.c_str());
    ZipEntry entry;
    int status = FindEntry(archive, zip_type_path, &entry);
    ExtractToMemory(archive, &entry, reinterpret_cast<uint8_t*>(&fname[0]), OTAIO_MAX_FNAME_SIZE);
    return fname;
}
