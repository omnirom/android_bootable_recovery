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

#include "applypatch/applypatch.h"

#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>
#include <openssl/sha.h>

#include "edify/expr.h"
#include "otafault/ota_io.h"
#include "otautil/paths.h"
#include "otautil/print_sha1.h"

static int LoadPartitionContents(const std::string& filename, FileContents* file);
static size_t FileSink(const unsigned char* data, size_t len, int fd);
static int GenerateTarget(const FileContents& source_file, const std::unique_ptr<Value>& patch,
                          const std::string& target_filename,
                          const uint8_t target_sha1[SHA_DIGEST_LENGTH], const Value* bonus_data);

int LoadFileContents(const std::string& filename, FileContents* file) {
  // A special 'filename' beginning with "EMMC:" means to load the contents of a partition.
  if (android::base::StartsWith(filename, "EMMC:")) {
    return LoadPartitionContents(filename, file);
  }

  struct stat sb;
  if (stat(filename.c_str(), &sb) == -1) {
    PLOG(ERROR) << "Failed to stat \"" << filename << "\"";
    return -1;
  }

  std::vector<unsigned char> data(sb.st_size);
  unique_file f(ota_fopen(filename.c_str(), "rb"));
  if (!f) {
    PLOG(ERROR) << "Failed to open \"" << filename << "\"";
    return -1;
  }

  size_t bytes_read = ota_fread(data.data(), 1, data.size(), f.get());
  if (bytes_read != data.size()) {
    LOG(ERROR) << "Short read of \"" << filename << "\" (" << bytes_read << " bytes of "
               << data.size() << ")";
    return -1;
  }
  file->data = std::move(data);
  SHA1(file->data.data(), file->data.size(), file->sha1);
  return 0;
}

// Loads the contents of an EMMC partition into the provided FileContents. filename should be a
// string of the form "EMMC:<partition_device>:...". The smallest size_n bytes for which that prefix
// of the partition contents has the corresponding sha1 hash will be loaded. It is acceptable for a
// size value to be repeated with different sha1s. Returns 0 on success.
//
// This complexity is needed because if an OTA installation is interrupted, the partition might
// contain either the source or the target data, which might be of different lengths. We need to
// know the length in order to read from a partition (there is no "end-of-file" marker), so the
// caller must specify the possible lengths and the hash of the data, and we'll do the load
// expecting to find one of those hashes.
static int LoadPartitionContents(const std::string& filename, FileContents* file) {
  std::vector<std::string> pieces = android::base::Split(filename, ":");
  if (pieces.size() < 4 || pieces.size() % 2 != 0 || pieces[0] != "EMMC") {
    LOG(ERROR) << "LoadPartitionContents called with bad filename \"" << filename << "\"";
    return -1;
  }

  size_t pair_count = (pieces.size() - 2) / 2;  // # of (size, sha1) pairs in filename
  std::vector<std::pair<size_t, std::string>> pairs;
  for (size_t i = 0; i < pair_count; ++i) {
    size_t size;
    if (!android::base::ParseUint(pieces[i * 2 + 2], &size) || size == 0) {
      LOG(ERROR) << "LoadPartitionContents called with bad size \"" << pieces[i * 2 + 2] << "\"";
      return -1;
    }
    pairs.push_back({ size, pieces[i * 2 + 3] });
  }

  // Sort the pairs array so that they are in order of increasing size.
  std::sort(pairs.begin(), pairs.end());

  const char* partition = pieces[1].c_str();
  unique_file dev(ota_fopen(partition, "rb"));
  if (!dev) {
    PLOG(ERROR) << "Failed to open eMMC partition \"" << partition << "\"";
    return -1;
  }

  SHA_CTX sha_ctx;
  SHA1_Init(&sha_ctx);

  // Allocate enough memory to hold the largest size.
  std::vector<unsigned char> buffer(pairs[pair_count - 1].first);
  unsigned char* buffer_ptr = buffer.data();
  size_t buffer_size = 0;  // # bytes read so far
  bool found = false;

  for (const auto& pair : pairs) {
    size_t current_size = pair.first;
    const std::string& current_sha1 = pair.second;

    // Read enough additional bytes to get us up to the next size. (Again,
    // we're trying the possibilities in order of increasing size).
    size_t next = current_size - buffer_size;
    if (next > 0) {
      size_t read = ota_fread(buffer_ptr, 1, next, dev.get());
      if (next != read) {
        LOG(ERROR) << "Short read (" << read << " bytes of " << next << ") for partition \""
                   << partition << "\"";
        return -1;
      }
      SHA1_Update(&sha_ctx, buffer_ptr, read);
      buffer_size += read;
      buffer_ptr += read;
    }

    // Duplicate the SHA context and finalize the duplicate so we can
    // check it against this pair's expected hash.
    SHA_CTX temp_ctx;
    memcpy(&temp_ctx, &sha_ctx, sizeof(SHA_CTX));
    uint8_t sha_so_far[SHA_DIGEST_LENGTH];
    SHA1_Final(sha_so_far, &temp_ctx);

    uint8_t parsed_sha[SHA_DIGEST_LENGTH];
    if (ParseSha1(current_sha1, parsed_sha) != 0) {
      LOG(ERROR) << "Failed to parse SHA-1 \"" << current_sha1 << "\" in " << filename;
      return -1;
    }

    if (memcmp(sha_so_far, parsed_sha, SHA_DIGEST_LENGTH) == 0) {
      // We have a match. Stop reading the partition; we'll return the data we've read so far.
      LOG(INFO) << "Partition read matched size " << current_size << " SHA-1 " << current_sha1;
      found = true;
      break;
    }
  }

  if (!found) {
    // Ran off the end of the list of (size, sha1) pairs without finding a match.
    LOG(ERROR) << "Contents of partition \"" << partition << "\" didn't match " << filename;
    return -1;
  }

  SHA1_Final(file->sha1, &sha_ctx);

  buffer.resize(buffer_size);
  file->data = std::move(buffer);

  return 0;
}

int SaveFileContents(const std::string& filename, const FileContents* file) {
  unique_fd fd(
      ota_open(filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_SYNC, S_IRUSR | S_IWUSR));
  if (fd == -1) {
    PLOG(ERROR) << "Failed to open \"" << filename << "\" for write";
    return -1;
  }

  size_t bytes_written = FileSink(file->data.data(), file->data.size(), fd);
  if (bytes_written != file->data.size()) {
    PLOG(ERROR) << "Short write of \"" << filename << "\" (" << bytes_written << " bytes of "
                << file->data.size();
    return -1;
  }
  if (ota_fsync(fd) != 0) {
    PLOG(ERROR) << "Failed to fsync \"" << filename << "\"";
    return -1;
  }
  if (ota_close(fd) != 0) {
    PLOG(ERROR) << "Failed to close \"" << filename << "\"";
    return -1;
  }

  return 0;
}

// Writes a memory buffer to 'target' partition, a string of the form
// "EMMC:<partition_device>[:...]". The target name might contain multiple colons, but
// WriteToPartition() only uses the first two and ignores the rest. Returns 0 on success.
static int WriteToPartition(const unsigned char* data, size_t len, const std::string& target) {
  std::vector<std::string> pieces = android::base::Split(target, ":");
  if (pieces.size() < 2 || pieces[0] != "EMMC") {
    LOG(ERROR) << "WriteToPartition called with bad target \"" << target << "\"";
    return -1;
  }

  const char* partition = pieces[1].c_str();
  unique_fd fd(ota_open(partition, O_RDWR));
  if (fd == -1) {
    PLOG(ERROR) << "Failed to open \"" << partition << "\"";
    return -1;
  }

  size_t start = 0;
  bool success = false;
  for (size_t attempt = 0; attempt < 2; ++attempt) {
    if (TEMP_FAILURE_RETRY(lseek(fd, start, SEEK_SET)) == -1) {
      PLOG(ERROR) << "Failed to seek to " << start << " on \"" << partition << "\"";
      return -1;
    }
    while (start < len) {
      size_t to_write = len - start;
      if (to_write > 1 << 20) to_write = 1 << 20;

      ssize_t written = TEMP_FAILURE_RETRY(ota_write(fd, data + start, to_write));
      if (written == -1) {
        PLOG(ERROR) << "Failed to write to \"" << partition << "\"";
        return -1;
      }
      start += written;
    }

    if (ota_fsync(fd) != 0) {
      PLOG(ERROR) << "Failed to sync \"" << partition << "\"";
      return -1;
    }
    if (ota_close(fd) != 0) {
      PLOG(ERROR) << "Failed to close \"" << partition << "\"";
      return -1;
    }

    fd.reset(ota_open(partition, O_RDONLY));
    if (fd == -1) {
      PLOG(ERROR) << "Failed to reopen \"" << partition << "\" for verification";
      return -1;
    }

    // Drop caches so our subsequent verification read won't just be reading the cache.
    sync();
    unique_fd dc(ota_open("/proc/sys/vm/drop_caches", O_WRONLY));
    if (TEMP_FAILURE_RETRY(ota_write(dc, "3\n", 2)) == -1) {
      PLOG(ERROR) << "Failed to write to /proc/sys/vm/drop_caches";
    } else {
      LOG(INFO) << "  caches dropped";
    }
    ota_close(dc);
    sleep(1);

    // Verify.
    if (TEMP_FAILURE_RETRY(lseek(fd, 0, SEEK_SET)) == -1) {
      PLOG(ERROR) << "Failed to seek to 0 on " << partition;
      return -1;
    }

    unsigned char buffer[4096];
    start = len;
    for (size_t p = 0; p < len; p += sizeof(buffer)) {
      size_t to_read = len - p;
      if (to_read > sizeof(buffer)) {
        to_read = sizeof(buffer);
      }

      size_t so_far = 0;
      while (so_far < to_read) {
        ssize_t read_count = TEMP_FAILURE_RETRY(ota_read(fd, buffer + so_far, to_read - so_far));
        if (read_count == -1) {
          PLOG(ERROR) << "Failed to verify-read " << partition << " at " << p;
          return -1;
        } else if (read_count == 0) {
          LOG(ERROR) << "Verify-reading " << partition << " reached unexpected EOF at " << p;
          return -1;
        }
        if (static_cast<size_t>(read_count) < to_read) {
          LOG(INFO) << "Short verify-read " << partition << " at " << p << ": expected " << to_read
                    << " actual " << read_count;
        }
        so_far += read_count;
      }

      if (memcmp(buffer, data + p, to_read) != 0) {
        LOG(ERROR) << "Verification failed starting at " << p;
        start = p;
        break;
      }
    }

    if (start == len) {
      LOG(INFO) << "Verification read succeeded (attempt " << attempt + 1 << ")";
      success = true;
      break;
    }

    if (ota_close(fd) != 0) {
      PLOG(ERROR) << "Failed to close " << partition;
      return -1;
    }

    fd.reset(ota_open(partition, O_RDWR));
    if (fd == -1) {
      PLOG(ERROR) << "Failed to reopen " << partition << " for next attempt";
      return -1;
    }
  }

  if (!success) {
    LOG(ERROR) << "Failed to verify after all attempts";
    return -1;
  }

  if (ota_close(fd) == -1) {
    PLOG(ERROR) << "Failed to close " << partition;
    return -1;
  }
  sync();

  return 0;
}

int ParseSha1(const std::string& str, uint8_t* digest) {
  const char* ps = str.c_str();
  uint8_t* pd = digest;
  for (int i = 0; i < SHA_DIGEST_LENGTH * 2; ++i, ++ps) {
    int digit;
    if (*ps >= '0' && *ps <= '9') {
      digit = *ps - '0';
    } else if (*ps >= 'a' && *ps <= 'f') {
      digit = *ps - 'a' + 10;
    } else if (*ps >= 'A' && *ps <= 'F') {
      digit = *ps - 'A' + 10;
    } else {
      return -1;
    }
    if (i % 2 == 0) {
      *pd = digit << 4;
    } else {
      *pd |= digit;
      ++pd;
    }
  }
  if (*ps != '\0') return -1;
  return 0;
}

// Searches a vector of SHA-1 strings for one matching the given SHA-1. Returns the index of the
// match on success, or -1 if no match is found.
static int FindMatchingPatch(const uint8_t* sha1, const std::vector<std::string>& patch_sha1s) {
  for (size_t i = 0; i < patch_sha1s.size(); ++i) {
    uint8_t patch_sha1[SHA_DIGEST_LENGTH];
    if (ParseSha1(patch_sha1s[i], patch_sha1) == 0 &&
        memcmp(patch_sha1, sha1, SHA_DIGEST_LENGTH) == 0) {
      return i;
    }
  }
  return -1;
}

int applypatch_check(const std::string& filename, const std::vector<std::string>& sha1s) {
  if (!android::base::StartsWith(filename, "EMMC:")) {
    return 1;
  }

  // The check will pass if LoadPartitionContents is successful, because the filename already
  // encodes the desired SHA-1s.
  FileContents file;
  if (LoadPartitionContents(filename, &file) != 0) {
    LOG(INFO) << "\"" << filename << "\" doesn't have any of expected SHA-1 sums; checking cache";

    // If the partition is corrupted, it might be because we were killed in the middle of patching
    // it. A copy should have been made in cache_temp_source. If that file exists and matches the
    // SHA-1 we're looking for, the check still passes.
    if (LoadFileContents(Paths::Get().cache_temp_source(), &file) != 0) {
      LOG(ERROR) << "Failed to load cache file";
      return 1;
    }

    if (FindMatchingPatch(file.sha1, sha1s) < 0) {
      LOG(ERROR) << "The cache bits don't match any SHA-1 for \"" << filename << "\"";
      return 1;
    }
  }
  return 0;
}

int ShowLicenses() {
  ShowBSDiffLicense();
  return 0;
}

static size_t FileSink(const unsigned char* data, size_t len, int fd) {
  size_t done = 0;
  while (done < len) {
    ssize_t wrote = TEMP_FAILURE_RETRY(ota_write(fd, data + done, len - done));
    if (wrote == -1) {
      PLOG(ERROR) << "Failed to write " << len - done << " bytes";
      return done;
    }
    done += wrote;
  }
  return done;
}

int applypatch(const char* source_filename, const char* target_filename,
               const char* target_sha1_str, size_t /* target_size */,
               const std::vector<std::string>& patch_sha1s,
               const std::vector<std::unique_ptr<Value>>& patch_data, const Value* bonus_data) {
  LOG(INFO) << "Patching " << source_filename;

  if (target_filename[0] == '-' && target_filename[1] == '\0') {
    target_filename = source_filename;
  }

  if (strncmp(target_filename, "EMMC:", 5) != 0) {
    LOG(ERROR) << "Supporting patching EMMC targets only";
    return 1;
  }

  uint8_t target_sha1[SHA_DIGEST_LENGTH];
  if (ParseSha1(target_sha1_str, target_sha1) != 0) {
    LOG(ERROR) << "Failed to parse target SHA-1 \"" << target_sha1_str << "\"";
    return 1;
  }

  // We try to load the target file into the source_file object.
  FileContents source_file;
  if (LoadFileContents(target_filename, &source_file) == 0) {
    if (memcmp(source_file.sha1, target_sha1, SHA_DIGEST_LENGTH) == 0) {
      // The early-exit case: the patch was already applied, this file has the desired hash, nothing
      // for us to do.
      LOG(INFO) << "  already " << short_sha1(target_sha1);
      return 0;
    }
  }

  if (source_file.data.empty() ||
      (target_filename != source_filename && strcmp(target_filename, source_filename) != 0)) {
    // Need to load the source file: either we failed to load the target file, or we did but it's
    // different from the expected.
    source_file.data.clear();
    LoadFileContents(source_filename, &source_file);
  }

  if (!source_file.data.empty()) {
    int to_use = FindMatchingPatch(source_file.sha1, patch_sha1s);
    if (to_use != -1) {
      return GenerateTarget(source_file, patch_data[to_use], target_filename, target_sha1,
                            bonus_data);
    }
  }

  LOG(INFO) << "Source file is bad; trying copy";

  FileContents copy_file;
  if (LoadFileContents(Paths::Get().cache_temp_source(), &copy_file) < 0) {
    LOG(ERROR) << "Failed to read copy file";
    return 1;
  }

  int to_use = FindMatchingPatch(copy_file.sha1, patch_sha1s);
  if (to_use == -1) {
    LOG(ERROR) << "The copy on /cache doesn't match source SHA-1s either";
    return 1;
  }

  return GenerateTarget(copy_file, patch_data[to_use], target_filename, target_sha1, bonus_data);
}

int applypatch_flash(const char* source_filename, const char* target_filename,
                     const char* target_sha1_str, size_t target_size) {
  LOG(INFO) << "Flashing " << target_filename;

  uint8_t target_sha1[SHA_DIGEST_LENGTH];
  if (ParseSha1(target_sha1_str, target_sha1) != 0) {
    LOG(ERROR) << "Failed to parse target SHA-1 \"" << target_sha1_str << "\"";
    return 1;
  }

  std::vector<std::string> pieces = android::base::Split(target_filename, ":");
  if (pieces.size() != 4 || pieces[0] != "EMMC") {
    LOG(ERROR) << "Invalid target name \"" << target_filename << "\"";
    return 1;
  }

  // Load the target into the source_file object to see if already applied.
  FileContents source_file;
  if (LoadPartitionContents(target_filename, &source_file) == 0 &&
      memcmp(source_file.sha1, target_sha1, SHA_DIGEST_LENGTH) == 0) {
    // The early-exit case: the image was already applied, this partition has the desired hash,
    // nothing for us to do.
    LOG(INFO) << "  already " << short_sha1(target_sha1);
    return 0;
  }

  if (LoadFileContents(source_filename, &source_file) == 0) {
    if (memcmp(source_file.sha1, target_sha1, SHA_DIGEST_LENGTH) != 0) {
      // The source doesn't have desired checksum.
      LOG(ERROR) << "source \"" << source_filename << "\" doesn't have expected SHA-1 sum";
      LOG(ERROR) << "expected: " << short_sha1(target_sha1)
                 << ", found: " << short_sha1(source_file.sha1);
      return 1;
    }
  }

  if (WriteToPartition(source_file.data.data(), target_size, target_filename) != 0) {
    LOG(ERROR) << "Failed to write copied data to " << target_filename;
    return 1;
  }
  return 0;
}

static int GenerateTarget(const FileContents& source_file, const std::unique_ptr<Value>& patch,
                          const std::string& target_filename,
                          const uint8_t target_sha1[SHA_DIGEST_LENGTH], const Value* bonus_data) {
  if (patch->type != Value::Type::BLOB) {
    LOG(ERROR) << "patch is not a blob";
    return 1;
  }

  const char* header = &patch->data[0];
  size_t header_bytes_read = patch->data.size();
  bool use_bsdiff = false;
  if (header_bytes_read >= 8 && memcmp(header, "BSDIFF40", 8) == 0) {
    use_bsdiff = true;
  } else if (header_bytes_read >= 8 && memcmp(header, "IMGDIFF2", 8) == 0) {
    use_bsdiff = false;
  } else {
    LOG(ERROR) << "Unknown patch file format";
    return 1;
  }

  CHECK(android::base::StartsWith(target_filename, "EMMC:"));

  // We write the original source to cache, in case the partition write is interrupted.
  if (!CheckAndFreeSpaceOnCache(source_file.data.size())) {
    LOG(ERROR) << "Not enough free space on /cache";
    return 1;
  }
  if (SaveFileContents(Paths::Get().cache_temp_source(), &source_file) < 0) {
    LOG(ERROR) << "Failed to back up source file";
    return 1;
  }

  // We store the decoded output in memory.
  std::string memory_sink_str;  // Don't need to reserve space.
  SHA_CTX ctx;
  SHA1_Init(&ctx);
  SinkFn sink = [&memory_sink_str, &ctx](const unsigned char* data, size_t len) {
    SHA1_Update(&ctx, data, len);
    memory_sink_str.append(reinterpret_cast<const char*>(data), len);
    return len;
  };

  int result;
  if (use_bsdiff) {
    result = ApplyBSDiffPatch(source_file.data.data(), source_file.data.size(), *patch, 0, sink);
  } else {
    result =
        ApplyImagePatch(source_file.data.data(), source_file.data.size(), *patch, sink, bonus_data);
  }

  if (result != 0) {
    LOG(ERROR) << "Failed to apply the patch: " << result;
    return 1;
  }

  uint8_t current_target_sha1[SHA_DIGEST_LENGTH];
  SHA1_Final(current_target_sha1, &ctx);
  if (memcmp(current_target_sha1, target_sha1, SHA_DIGEST_LENGTH) != 0) {
    LOG(ERROR) << "Patching did not produce the expected SHA-1 of " << short_sha1(target_sha1);

    LOG(ERROR) << "target size " << memory_sink_str.size() << " SHA-1 "
               << short_sha1(current_target_sha1);
    LOG(ERROR) << "source size " << source_file.data.size() << " SHA-1 "
               << short_sha1(source_file.sha1);

    uint8_t patch_digest[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const uint8_t*>(patch->data.data()), patch->data.size(), patch_digest);
    LOG(ERROR) << "patch size " << patch->data.size() << " SHA-1 " << short_sha1(patch_digest);

    if (bonus_data != nullptr) {
      uint8_t bonus_digest[SHA_DIGEST_LENGTH];
      SHA1(reinterpret_cast<const uint8_t*>(bonus_data->data.data()), bonus_data->data.size(),
           bonus_digest);
      LOG(ERROR) << "bonus size " << bonus_data->data.size() << " SHA-1 "
                 << short_sha1(bonus_digest);
    }

    return 1;
  } else {
    LOG(INFO) << "  now " << short_sha1(target_sha1);
  }

  // Write back the temp file to the partition.
  if (WriteToPartition(reinterpret_cast<const unsigned char*>(memory_sink_str.c_str()),
                       memory_sink_str.size(), target_filename) != 0) {
    LOG(ERROR) << "Failed to write patched data to " << target_filename;
    return 1;
  }

  // Delete the backup copy of the source.
  unlink(Paths::Get().cache_temp_source().c_str());

  // Success!
  return 0;
}
