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
#include <ostream>
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

struct Partition {
  Partition() = default;

  Partition(const std::string& name, size_t size, const std::string& hash)
      : name(name), size(size), hash(hash) {}

  // Parses and returns the given string into a Partition object. The input string is of the form
  // "EMMC:<device>:<size>:<hash>". Returns the parsed Partition, or an empty object on error.
  static Partition Parse(const std::string& partition, std::string* err);

  std::string ToString() const;

  // Returns whether the current Partition object is valid.
  explicit operator bool() const {
    return !name.empty();
  }

  std::string name;
  size_t size;
  std::string hash;
};

std::ostream& operator<<(std::ostream& os, const Partition& partition);

// Applies the given 'patch' to the 'source' Partition, verifies then writes the patching result to
// the 'target' Partition. While patching, it will backup the data on the source partition to
// /cache, so that the patching could be resumed on interruption even if both of the source and
// target partitions refer to the same device. The function is idempotent if called multiple times.
// 'bonus' can be provided if the patch was generated with a bonus output, or nullptr.
// 'backup_source' indicates whether the source partition should be backed up prior to the update
// (e.g. when doing in-place update). Returns the patching result.
bool PatchPartition(const Partition& target, const Partition& source, const Value& patch,
                    const Value* bonus, bool backup_source);

// Returns whether the contents of the eMMC target or the cached file match the embedded hash.
// It will look for the backup on /cache if the given partition doesn't match the checksum.
bool PatchPartitionCheck(const Partition& target, const Partition& source);

// Checks whether the contents of the given partition has the desired hash. It will NOT look for
// the backup on /cache if the given partition doesn't have the expected checksum.
bool CheckPartition(const Partition& target);

// Flashes a given image in 'source_filename' to the eMMC target partition. It verifies the target
// checksum first, and will return if target already has the desired hash. Otherwise it checks the
// checksum of the given source image, flashes, and verifies the target partition afterwards. The
// function is idempotent. Returns the flashing result.
bool FlashPartition(const Partition& target, const std::string& source_filename);

// Reads a file into memory; stores the file contents and associated metadata in *file.
bool LoadFileContents(const std::string& filename, FileContents* file);

// Saves the given FileContents object to the given filename.
bool SaveFileContents(const std::string& filename, const FileContents* file);

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
