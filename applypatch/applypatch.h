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

#include <sys/stat.h>

#include <vector>

#include "openssl/sha.h"
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

typedef ssize_t (*SinkFn)(const unsigned char*, ssize_t, void*);

// applypatch.c
int ShowLicenses();
size_t FreeSpaceForFile(const char* filename);
int CacheSizeCheck(size_t bytes);
int ParseSha1(const char* str, uint8_t* digest);

int applypatch_flash(const char* source_filename, const char* target_filename,
                     const char* target_sha1_str, size_t target_size);
int applypatch(const char* source_filename,
               const char* target_filename,
               const char* target_sha1_str,
               size_t target_size,
               int num_patches,
               char** const patch_sha1_str,
               Value** patch_data,
               Value* bonus_data);
int applypatch_check(const char* filename,
                     int num_patches,
                     char** const patch_sha1_str);

int LoadFileContents(const char* filename, FileContents* file);
int SaveFileContents(const char* filename, const FileContents* file);
void FreeFileContents(FileContents* file);
int FindMatchingPatch(uint8_t* sha1, char* const * const patch_sha1_str,
                      int num_patches);

// bsdiff.cpp
void ShowBSDiffLicense();
int ApplyBSDiffPatch(const unsigned char* old_data, ssize_t old_size,
                     const Value* patch, ssize_t patch_offset,
                     SinkFn sink, void* token, SHA_CTX* ctx);
int ApplyBSDiffPatchMem(const unsigned char* old_data, ssize_t old_size,
                        const Value* patch, ssize_t patch_offset,
                        std::vector<unsigned char>* new_data);

// imgpatch.cpp
int ApplyImagePatch(const unsigned char* old_data, ssize_t old_size,
                    const Value* patch,
                    SinkFn sink, void* token, SHA_CTX* ctx,
                    const Value* bonus_data);

// freecache.cpp
int MakeFreeSpaceOnCache(size_t bytes_needed);

#endif
