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

// Parses a given string of 40 hex digits into 20-byte array 'digest'. 'str' may contain only the
// digest or be of the form "<digest>:<anything>". Returns 0 on success, or -1 on any error.
int ParseSha1(const std::string& str, uint8_t* digest);

// Applies binary patches to eMMC target files in a way that is safe (the original file is not
// touched until we have the desired replacement for it) and idempotent (it's okay to run this
// program multiple times).
//
// - If the SHA-1 hash of 'target_filename' is 'target_sha1_string', does nothing and returns
//   successfully.
//
// - Otherwise, if the SHA-1 hash of 'source_filename' is one of the entries in 'patch_sha1s', the
//   corresponding patch from 'patch_data' (which must be a VAL_BLOB) is applied to produce a new
//   file (the type of patch is automatically detected from the blob data). If that new file has
//   SHA-1 hash 'target_sha1_str', moves it to replace 'target_filename', and exits successfully.
//   Note that if 'source_filename' and 'target_filename' are not the same, 'source_filename' is
//   NOT deleted on success. 'target_filename' may be the string "-" to mean
//   "the same as 'source_filename'".
//
// - Otherwise, or if any error is encountered, exits with non-zero status.
//
// 'source_filename' must refer to an eMMC partition to read the source data. See the comments for
// the LoadPartitionContents() function for the format of such a filename. 'target_size' has become
// obsolete since we have dropped the support for patching non-eMMC targets (eMMC targets have the
// size embedded in the filename).
int applypatch(const char* source_filename, const char* target_filename,
               const char* target_sha1_str, size_t target_size,
               const std::vector<std::string>& patch_sha1s,
               const std::vector<std::unique_ptr<Value>>& patch_data, const Value* bonus_data);

// Returns 0 if the contents of the eMMC target or the cached file match any of the given SHA-1's.
// Returns nonzero otherwise. 'filename' must refer to an eMMC partition target. It would only use
// 'sha1s' to find a match on /cache if the hashes embedded in the filename fail to match.
int applypatch_check(const std::string& filename, const std::vector<std::string>& sha1s);

// Flashes a given image to the target partition. It verifies the target cheksum first, and will
// return if target already has the desired hash. Otherwise it checks the checksum of the given
// source image before flashing, and verifies the target partition afterwards. The function is
// idempotent. Returns zero on success.
int applypatch_flash(const char* source_filename, const char* target_filename,
                     const char* target_sha1_str, size_t target_size);

// Reads a file into memory; stores the file contents and associated metadata in *file. Returns 0
// on success, or -1 on error.
int LoadFileContents(const std::string& filename, FileContents* file);

// Saves the given FileContents object to the given filename. Returns 0 on success, or -1 on error.
int SaveFileContents(const std::string& filename, const FileContents* file);

// bspatch.cpp

void ShowBSDiffLicense();

// Applies the bsdiff-patch given in 'patch' (from offset 'patch_offset' to the end) to the source
// data given by (old_data, old_size). Writes the patched output through the given 'sink'. Returns
// 0 on success.
int ApplyBSDiffPatch(const unsigned char* old_data, size_t old_size, const Value& patch,
                     size_t patch_offset, SinkFn sink);

// imgpatch.cpp

// Applies the imgdiff-patch given in 'patch' to the source data given by (old_data, old_size), with
// the optional bonus data. Writes the patched output through the given 'sink'. Returns 0 on
// success.
int ApplyImagePatch(const unsigned char* old_data, size_t old_size, const Value& patch, SinkFn sink,
                    const Value* bonus_data);

// freecache.cpp

// Checks whether /cache partition has at least 'bytes'-byte free space. Returns true immediately
// if so. Otherwise, it will try to free some space by removing older logs, checks again and
// returns the checking result.
bool CheckAndFreeSpaceOnCache(size_t bytes);

// Removes the files in |dirname| until we have at least |bytes_needed| bytes of free space on the
// partition. |space_checker| should return the size of the free space, or -1 on error.
bool RemoveFilesInDirectory(size_t bytes_needed, const std::string& dirname,
                            const std::function<int64_t(const std::string&)>& space_checker);
#endif
