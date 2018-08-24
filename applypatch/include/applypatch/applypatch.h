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

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <openssl/sha.h>

// Forward declaration to avoid including "edify/expr.h" in the header.
struct Value;

struct FileContents {
  uint8_t sha1[SHA_DIGEST_LENGTH];
  std::vector<unsigned char> data;
};

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

// Applies the bsdiff-patch given in 'patch' (from offset 'patch_offset' to the end) to the source
// data given by (old_data, old_size). Writes the patched output through the given 'sink', and
// updates the SHA-1 context with the output data. Returns 0 on success.
int ApplyBSDiffPatch(const unsigned char* old_data, size_t old_size, const Value& patch,
                     size_t patch_offset, SinkFn sink, SHA_CTX* ctx);

// imgpatch.cpp

// Applies the imgdiff-patch given in 'patch' to the source data given by (old_data, old_size), with
// the optional bonus data. Writes the patched output through the given 'sink', and updates the
// SHA-1 context with the output data. Returns 0 on success.
int ApplyImagePatch(const unsigned char* old_data, size_t old_size, const Value& patch, SinkFn sink,
                    SHA_CTX* ctx, const Value* bonus_data);

// freecache.cpp

int MakeFreeSpaceOnCache(size_t bytes_needed);

#endif
