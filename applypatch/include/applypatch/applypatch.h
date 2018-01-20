/*
 * Copyright (C) 2008 The Android Open Source Project
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

#ifndef _APPLYPATCH_H
#define _APPLYPATCH_H

#include <stdint.h>
#include <sys/stat.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <openssl/sha.h>

#include "edify/expr.h"

struct FileContents {
  uint8_t sha1[SHA_DIGEST_LENGTH];
  std::vector<unsigned char> data;
  struct stat st;
};

// When there isn't enough room on the target filesystem to hold the
// patched version of the file, we copy the original here and delete
// it to free up space.  If the expected source file doesn't exist, or
// is corrupted, we look to see if this file contains the bits we want
// and use it as the source instead.
#define CACHE_TEMP_SOURCE "/cache/saved.file"

using SinkFn = std::function<size_t(const unsigned char*, size_t)>;

// applypatch.cpp
int ShowLicenses();
size_t FreeSpaceForFile(const char* filename);
int CacheSizeCheck(size_t bytes);
int ParseSha1(const char* str, uint8_t* digest);

int applypatch(const char* source_filename,
               const char* target_filename,
               const char* target_sha1_str,
               size_t target_size,
               const std::vector<std::string>& patch_sha1_str,
               const std::vector<std::unique_ptr<Value>>& patch_data,
               const Value* bonus_data);
int applypatch_check(const char* filename,
                     const std::vector<std::string>& patch_sha1_str);
int applypatch_flash(const char* source_filename, const char* target_filename,
                     const char* target_sha1_str, size_t target_size);

int LoadFileContents(const char* filename, FileContents* file);
int SaveFileContents(const char* filename, const FileContents* file);

// bspatch.cpp
void ShowBSDiffLicense();
int ApplyBSDiffPatch(const unsigned char* old_data, size_t old_size, const Value* patch,
                     size_t patch_offset, SinkFn sink, SHA_CTX* ctx);

// imgpatch.cpp
int ApplyImagePatch(const unsigned char* old_data, size_t old_size, const Value* patch, SinkFn sink,
                    SHA_CTX* ctx, const Value* bonus_data);

// freecache.cpp
int MakeFreeSpaceOnCache(size_t bytes_needed);

#endif
